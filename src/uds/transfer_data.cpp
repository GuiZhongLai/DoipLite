#include "transfer_data.h"

#include "doiplite/uds.h"
#include "uds_result.h"

#include <vector>

namespace doiplite {
namespace uds {

DoipLiteStatus TransferData::execute(
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
    if (args.dataLen > 0 && !args.data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    // Request data after SID: blockSequenceCounter | transferRequestParameterRecord
    std::vector<uint8_t> req;
    req.reserve(1u + args.dataLen);
    req.push_back(args.blockSequenceCounter);
    if (args.dataLen > 0)
    {
        req.insert(req.end(), args.data, args.data + args.dataLen);
    }

    uint32_t respCap = cap < 64u ? 64u : cap;
    std::vector<uint8_t> resp(respCap);
    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUds(
        session,
        0x36,
        req.data(),
        static_cast<uint32_t>(req.size()),
        resp.data(),
        respCap,
        &respLen,
        timeoutMs);

    if (st != DOIPLITE_OK)
    {
        CopyNrcIfNeeded(st, resp.data(), respLen, out, cap, len);
        return st;
    }

    // Positive: 0x76 | blockSequenceCounter | optional response record
    if (respLen < 2 || resp[0] != 0x76 ||
        resp[1] != args.blockSequenceCounter)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    return CopyPositive(resp.data(), respLen, out, cap, len);
}

} // namespace uds
} // namespace doiplite

DoipLiteStatus UdsTransferData(
    DoipLiteSession *session,
    uint8_t blockSequenceCounter,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    doiplite::uds::TransferData::Args args;
    args.blockSequenceCounter = blockSequenceCounter;
    args.data = data;
    args.dataLen = dataLen;
    return doiplite::uds::TransferData::execute(
        session, args, out, cap, len, timeoutMs);
}
