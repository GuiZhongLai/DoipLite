#include "flash_orchestrator.h"

#include "doiplite/uds.h"

#include <algorithm>

namespace doiplite {
namespace flash {

FlashOrchestrator::FlashOrchestrator(DoipLiteSession *session, FlashJobParams params)
    : session_(session)
    , params_(std::move(params))
{
}

bool FlashOrchestrator::keepAliveEnabled() const
{
    return params_.haveKeepAliveSubFunction && params_.keepAliveIntervalMs > 0;
}

void FlashOrchestrator::markActivity()
{
    lastActivity_ = std::chrono::steady_clock::now();
    haveActivityStamp_ = true;
}

DoipLiteStatus FlashOrchestrator::maybeKeepAlive()
{
    if (!keepAliveEnabled())
    {
        return DOIPLITE_OK;
    }
    if (!haveActivityStamp_)
    {
        // First poll: establish the idle clock without sending 0x3E yet.
        markActivity();
        return DOIPLITE_OK;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastActivity_);
    if (elapsed.count() < static_cast<std::chrono::milliseconds::rep>(
            params_.keepAliveIntervalMs))
    {
        return DOIPLITE_OK;
    }

    // Idle between completed steps / blocks only — never mid-response.
    uint8_t resp[16];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsTesterPresent(
        session_,
        params_.keepAliveSubFunction,
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
    }
    return st;
}

void FlashOrchestrator::fillProgress(DoipLiteFlashProgress *progress) const
{
    if (!progress)
    {
        return;
    }
    progress->phase = phase_;
    progress->percent = percent_;
    progress->err = lastError_;
}

void FlashOrchestrator::fail(DoipLiteStatus err)
{
    lastError_ = err;
    phase_ = DOIPLITE_FLASH_FAILED;
    // Keep percent as-is so callers see how far it got.
}

void FlashOrchestrator::advanceAfterOptional(DoipLiteFlashPhase next)
{
    phase_ = next;
}

void FlashOrchestrator::updateTransferPercent()
{
    if (params_.payload.empty())
    {
        return;
    }
    const uint64_t done = payloadOffset_;
    const uint64_t total = static_cast<uint64_t>(params_.payload.size());
    // Map transfer progress into 20%..90% with 64-bit intermediates so packages
    // larger than ~61 MiB (UINT32_MAX / 70) do not overflow.
    percent_ = static_cast<uint32_t>(20u + (done * 70u) / total);
}

DoipLiteStatus FlashOrchestrator::stepSession()
{
    if (!params_.haveFlashSessionType)
    {
        advanceAfterOptional(DOIPLITE_FLASH_SECURITY);
        return DOIPLITE_OK;
    }
    uint8_t resp[64];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsDiagnosticSessionControl(
        session_,
        params_.flashSessionType,
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
        return st;
    }
    percent_ = 5;
    advanceAfterOptional(DOIPLITE_FLASH_SECURITY);
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::stepSecurity()
{
    // 0x27 is intentionally not implemented in this PR.
    // Seed-key callback may be registered on the session but is not wired here.
    if (params_.haveSecurity)
    {
        fail(DOIPLITE_ERR_NOT_IMPLEMENTED);
        return DOIPLITE_ERR_NOT_IMPLEMENTED;
    }
    advanceAfterOptional(DOIPLITE_FLASH_ERASE);
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::stepErase()
{
    if (!params_.haveEraseRoutine)
    {
        advanceAfterOptional(DOIPLITE_FLASH_REQUEST_DOWNLOAD);
        return DOIPLITE_OK;
    }
    uint8_t resp[64];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsRoutineControl(
        session_,
        params_.eraseRoutineSubFunction,
        params_.eraseRoutineId,
        params_.eraseRoutineOption.empty() ? nullptr : params_.eraseRoutineOption.data(),
        static_cast<uint32_t>(params_.eraseRoutineOption.size()),
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
        return st;
    }
    percent_ = 15;
    advanceAfterOptional(DOIPLITE_FLASH_REQUEST_DOWNLOAD);
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::stepRequestDownload()
{
    uint8_t resp[64];
    uint32_t len = 0;
    uint32_t maxBlock = 0;
    const DoipLiteStatus st = UdsRequestDownload(
        session_,
        params_.dataFormatId,
        params_.addressAndLengthFormatId,
        params_.memoryAddress,
        params_.memorySize,
        &maxBlock,
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
        return st;
    }
    // ISO 14229: maxNumberOfBlockLength includes SID (1) + blockSequenceCounter (1).
    if (maxBlock < 3)
    {
        fail(DOIPLITE_ERR_NETWORK);
        return DOIPLITE_ERR_NETWORK;
    }
    maxBlockLength_ = maxBlock;
    uint32_t dataCap = maxBlock - 2u;
    if (params_.maxBlockDataCap > 0 && params_.maxBlockDataCap < dataCap)
    {
        dataCap = params_.maxBlockDataCap;
    }
    blockDataCap_ = dataCap;
    payloadOffset_ = 0;
    blockSequenceCounter_ = 1;
    percent_ = 20;
    advanceAfterOptional(DOIPLITE_FLASH_TRANSFER);
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::stepTransfer()
{
    if (payloadOffset_ >= params_.payload.size())
    {
        percent_ = 90;
        advanceAfterOptional(DOIPLITE_FLASH_TRANSFER_EXIT);
        return DOIPLITE_OK;
    }
    if (blockDataCap_ == 0)
    {
        fail(DOIPLITE_ERR_INVALID_ARG);
        return DOIPLITE_ERR_INVALID_ARG;
    }

    const uint64_t remaining =
        static_cast<uint64_t>(params_.payload.size()) - payloadOffset_;
    const uint32_t chunk = static_cast<uint32_t>(
        std::min(remaining, static_cast<uint64_t>(blockDataCap_)));
    uint8_t resp[64];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsTransferData(
        session_,
        blockSequenceCounter_,
        params_.payload.data() + static_cast<std::size_t>(payloadOffset_),
        chunk,
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
        return st;
    }

    payloadOffset_ += chunk;
    blockSequenceCounter_ = static_cast<uint8_t>(blockSequenceCounter_ + 1u);
    updateTransferPercent();

    if (payloadOffset_ >= params_.payload.size())
    {
        percent_ = 90;
        advanceAfterOptional(DOIPLITE_FLASH_TRANSFER_EXIT);
    }
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::stepTransferExit()
{
    uint8_t resp[64];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsRequestTransferExit(
        session_,
        params_.transferExitParam.empty() ? nullptr : params_.transferExitParam.data(),
        static_cast<uint32_t>(params_.transferExitParam.size()),
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
        return st;
    }
    percent_ = 95;
    advanceAfterOptional(DOIPLITE_FLASH_RESET);
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::stepReset()
{
    if (!params_.haveEcuReset)
    {
        percent_ = 100;
        phase_ = DOIPLITE_FLASH_DONE;
        lastError_ = DOIPLITE_OK;
        return DOIPLITE_OK;
    }
    uint8_t resp[16];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsEcuReset(
        session_,
        params_.resetType,
        resp,
        sizeof(resp),
        &len,
        params_.timeoutMs);
    markActivity();
    if (st != DOIPLITE_OK)
    {
        fail(st);
        return st;
    }
    percent_ = 100;
    phase_ = DOIPLITE_FLASH_DONE;
    lastError_ = DOIPLITE_OK;
    return DOIPLITE_OK;
}

DoipLiteStatus FlashOrchestrator::poll(DoipLiteFlashProgress *progress)
{
    if (phase_ == DOIPLITE_FLASH_DONE || phase_ == DOIPLITE_FLASH_FAILED)
    {
        fillProgress(progress);
        return lastError_ == DOIPLITE_OK ? DOIPLITE_OK : lastError_;
    }
    if (phase_ == DOIPLITE_FLASH_IDLE)
    {
        phase_ = DOIPLITE_FLASH_SESSION;
    }

    // Keep-alive only when idle between steps — before starting the next UDS
    // exchange, never while a previous request (including 0x78) is outstanding.
    const DoipLiteStatus keepSt = maybeKeepAlive();
    if (keepSt != DOIPLITE_OK)
    {
        fillProgress(progress);
        return keepSt;
    }

    DoipLiteStatus st = DOIPLITE_OK;
    switch (phase_)
    {
    case DOIPLITE_FLASH_SESSION:
        st = stepSession();
        break;
    case DOIPLITE_FLASH_SECURITY:
        st = stepSecurity();
        break;
    case DOIPLITE_FLASH_ERASE:
        st = stepErase();
        break;
    case DOIPLITE_FLASH_REQUEST_DOWNLOAD:
        st = stepRequestDownload();
        break;
    case DOIPLITE_FLASH_TRANSFER:
        st = stepTransfer();
        break;
    case DOIPLITE_FLASH_TRANSFER_EXIT:
        st = stepTransferExit();
        break;
    case DOIPLITE_FLASH_RESET:
        st = stepReset();
        break;
    default:
        fail(DOIPLITE_ERR_INVALID_ARG);
        st = DOIPLITE_ERR_INVALID_ARG;
        break;
    }

    fillProgress(progress);
    return st;
}

} // namespace flash
} // namespace doiplite
