#pragma once

#include "flash_job_params.h"

#include "doiplite/flash.h"

#include <chrono>

namespace doiplite {
namespace flash {

/**
 * Flash state machine: orchestrates UDS services only.
 * Does not talk to sockets or assemble DoIP / UDS wire framing itself.
 *
 * Critical path (skips omitted optional steps):
 *   SESSION(0x10?) -> SECURITY(skip/fail) -> ERASE(0x31?) ->
 *   REQUEST_DOWNLOAD(0x34) -> TRANSFER(0x36 x N) ->
 *   TRANSFER_EXIT(0x37) -> RESET(0x11?) -> DONE
 *
 * Optional periodic TesterPresent (0x3E) is issued only between completed
 * steps / 0x36 blocks when keep-alive is configured — never while a UDS
 * request (including 0x78 pending) is outstanding.
 */
class FlashOrchestrator final
{
public:
    FlashOrchestrator(DoipLiteSession *session, FlashJobParams params);

    FlashOrchestrator(const FlashOrchestrator &) = delete;
    FlashOrchestrator &operator=(const FlashOrchestrator &) = delete;

    DoipLiteStatus poll(DoipLiteFlashProgress *progress);

    DoipLiteFlashPhase phase() const { return phase_; }
    DoipLiteStatus lastError() const { return lastError_; }
    uint32_t percent() const { return percent_; }

private:
    DoipLiteStatus stepSession();
    DoipLiteStatus stepSecurity();
    DoipLiteStatus stepErase();
    DoipLiteStatus stepRequestDownload();
    DoipLiteStatus stepTransfer();
    DoipLiteStatus stepTransferExit();
    DoipLiteStatus stepReset();

    DoipLiteStatus maybeKeepAlive();
    void markActivity();
    bool keepAliveEnabled() const;

    void fail(DoipLiteStatus err);
    void fillProgress(DoipLiteFlashProgress *progress) const;
    void advanceAfterOptional(DoipLiteFlashPhase next);
    void updateTransferPercent();

    DoipLiteSession *session_; // non-owning; lifetime owned by C API session
    FlashJobParams params_;
    DoipLiteFlashPhase phase_ = DOIPLITE_FLASH_SESSION;
    DoipLiteStatus lastError_ = DOIPLITE_OK;
    uint32_t percent_ = 0;

    uint32_t maxBlockLength_ = 0; // from 0x74, includes SID + counter
    uint32_t blockDataCap_ = 0;   // bytes of payload per 0x36
    uint64_t payloadOffset_ = 0;
    uint8_t blockSequenceCounter_ = 1; // ISO: first TransferData uses 0x01

    bool haveActivityStamp_ = false;
    std::chrono::steady_clock::time_point lastActivity_{};
};

} // namespace flash
} // namespace doiplite
