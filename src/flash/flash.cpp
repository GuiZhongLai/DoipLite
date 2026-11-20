#include "doiplite/flash.h"

#include "doip/session_internal.h"
#include "flash_orchestrator.h"

#include <fstream>
#include <vector>

namespace {

DoipLiteStatus LoadPayload(const DoipLiteFlashJob *job, std::vector<uint8_t> *out)
{
    if (!job || !out)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    out->clear();

    if (job->data && job->dataLen > 0)
    {
        out->assign(job->data, job->data + job->dataLen);
        return DOIPLITE_OK;
    }
    if (job->dataLen > 0 && !job->data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (!job->packagePath || job->packagePath[0] == '\0')
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    std::ifstream in(job->packagePath, std::ios::binary);
    if (!in)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    in.seekg(0, std::ios::beg);
    out->resize(static_cast<std::size_t>(size));
    if (size > 0)
    {
        in.read(reinterpret_cast<char *>(out->data()), size);
        if (!in)
        {
            out->clear();
            return DOIPLITE_ERR_INVALID_ARG;
        }
    }
    return DOIPLITE_OK;
}

DoipLiteStatus BuildParams(const DoipLiteFlashJob *job, doiplite::flash::FlashJobParams *params)
{
    if (!job || !params)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    params->haveFlashSessionType = job->haveFlashSessionType != 0;
    params->flashSessionType = job->flashSessionType;
    params->haveSecurity = job->haveSecurity != 0;
    params->securityLevel = job->securityLevel;

    params->haveEraseRoutine = job->haveEraseRoutine != 0;
    params->eraseRoutineSubFunction = job->eraseRoutineSubFunction;
    params->eraseRoutineId = job->eraseRoutineId;
    if (job->eraseRoutineOptionLen > 0)
    {
        if (!job->eraseRoutineOption)
        {
            return DOIPLITE_ERR_INVALID_ARG;
        }
        params->eraseRoutineOption.assign(
            job->eraseRoutineOption,
            job->eraseRoutineOption + job->eraseRoutineOptionLen);
    }

    params->dataFormatId = job->dataFormatId;
    params->addressAndLengthFormatId = job->addressAndLengthFormatId;
    params->memoryAddress = job->memoryAddress;
    params->maxBlockDataCap = job->maxBlockDataCap;

    if (job->transferExitParamLen > 0)
    {
        if (!job->transferExitParam)
        {
            return DOIPLITE_ERR_INVALID_ARG;
        }
        params->transferExitParam.assign(
            job->transferExitParam,
            job->transferExitParam + job->transferExitParamLen);
    }

    params->haveEcuReset = job->haveEcuReset != 0;
    params->resetType = job->resetType;
    params->keepAliveIntervalMs = job->keepAliveIntervalMs;
    params->haveKeepAliveSubFunction = job->haveKeepAliveSubFunction != 0;
    params->keepAliveSubFunction = job->keepAliveSubFunction;
    params->timeoutMs = job->timeoutMs;

    const DoipLiteStatus loadSt = LoadPayload(job, &params->payload);
    if (loadSt != DOIPLITE_OK)
    {
        return loadSt;
    }
    if (params->payload.empty())
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    if (job->memorySize == 0)
    {
        params->memorySize = params->payload.size();
    }
    else if (job->memorySize != params->payload.size())
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    else
    {
        params->memorySize = job->memorySize;
    }

    // ALFID must be usable for RequestDownload.
    const uint8_t addrBytes =
        static_cast<uint8_t>(params->addressAndLengthFormatId & 0x0f);
    const uint8_t sizeBytes =
        static_cast<uint8_t>((params->addressAndLengthFormatId >> 4) & 0x0f);
    if (addrBytes < 1 || addrBytes > 8 || sizeBytes < 1 || sizeBytes > 8)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    return DOIPLITE_OK;
}

} // namespace

DoipLiteStatus DoipLiteSetSeedKeyCallback(
    DoipLiteSession *session,
    DoipLiteSeedKeyFn fn,
    void *user)
{
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    // Stored for a future 0x27 implementation; flash orchestration does not call it yet.
    session->seedKeyFn = fn;
    session->seedKeyUser = user;
    return DOIPLITE_OK;
}

DoipLiteStatus DoipLiteFlashStart(
    DoipLiteSession *session,
    const DoipLiteFlashJob *job)
{
    if (!session || !job)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (!session->routingActivated)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    doiplite::flash::FlashJobParams params;
    const DoipLiteStatus built = BuildParams(job, &params);
    if (built != DOIPLITE_OK)
    {
        return built;
    }

    session->flash = std::make_unique<doiplite::flash::FlashOrchestrator>(
        session, std::move(params));
    return DOIPLITE_OK;
}

DoipLiteStatus DoipLiteFlashPoll(
    DoipLiteSession *session,
    DoipLiteFlashProgress *progress)
{
    if (!session || !progress)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (!session->flash)
    {
        progress->phase = DOIPLITE_FLASH_IDLE;
        progress->percent = 0;
        progress->err = DOIPLITE_ERR_INVALID_ARG;
        return DOIPLITE_ERR_INVALID_ARG;
    }
    return session->flash->poll(progress);
}
