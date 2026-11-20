#include "request_transfer_exit.h"

#include "doiplite/uds.h"
#include "uds_result.h"

#include <vector>

namespace doiplite {
namespace uds {

DoipLiteStatus RequestTransferExit::execute(
    DoipLiteSession *session,
    const Args &args,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    if (len)
    {
        *len = 0;
    }
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (cap > 0 && !out)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (args.parameterLen > 0 && !args.parameterRecord)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    const uint8_t *data = args.parameterRecord;
    const uint32_t dataLen = args.parameterLen;

    uint32_t respCap = cap < 64u ? 64u : cap;
    std::vector<uint8_t> resp(respCap);
    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUds(
        session,
        0x37,
        dataLen > 0 ? data : nullptr,
        dataLen,
        resp.data(),
        respCap,
        &respLen,
        timeoutMs);

    if (st != DOIPLITE_OK)
    {
        CopyNrcIfNeeded(st, resp.data(), respLen, out, cap, len);
        return st;
    }

    // Positive: 0x77 | optional transferResponseParameterRecord
    if (respLen < 1 || resp[0] != 0x77)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    return CopyPositive(resp.data(), respLen, out, cap, len);
}

} // namespace uds
} // namespace doiplite

DoipLiteStatus UdsRequestTransferExit(
    DoipLiteSession *session,
    const uint8_t *parameterRecord,
    uint32_t parameterLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    doiplite::uds::RequestTransferExit::Args args;
    args.parameterRecord = parameterRecord;
    args.parameterLen = parameterLen;
    return doiplite::uds::RequestTransferExit::execute(
        session, args, out, cap, len, timeoutMs);
}
