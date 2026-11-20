#include "routine_control.h"

#include "doiplite/uds.h"
#include "uds_result.h"

#include <vector>

namespace doiplite {
namespace uds {

DoipLiteStatus RoutineControl::execute(
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
    if (args.optionLen > 0 && !args.optionRecord)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    // Request data after SID: subFunction | routineId(2) | optionRecord
    std::vector<uint8_t> req;
    req.reserve(3u + args.optionLen);
    req.push_back(args.subFunction);
    req.push_back(static_cast<uint8_t>((args.routineId >> 8) & 0xff));
    req.push_back(static_cast<uint8_t>(args.routineId & 0xff));
    if (args.optionLen > 0)
    {
        req.insert(req.end(), args.optionRecord, args.optionRecord + args.optionLen);
    }

    uint32_t respCap = cap < 64u ? 64u : cap;
    std::vector<uint8_t> resp(respCap);
    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUds(
        session,
        0x31,
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

    // Positive: 0x71 | subFunction | routineId | optional status record
    if (respLen < 3 || resp[0] != 0x71 || resp[1] != args.subFunction)
    {
        return DOIPLITE_ERR_NETWORK;
    }
    const uint16_t gotId = static_cast<uint16_t>(
        (static_cast<uint16_t>(resp[2]) << 8) | resp[3]);
    if (respLen < 4 || gotId != args.routineId)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    return CopyPositive(resp.data(), respLen, out, cap, len);
}

} // namespace uds
} // namespace doiplite

DoipLiteStatus UdsRoutineControl(
    DoipLiteSession *session,
    uint8_t subFunction,
    uint16_t routineId,
    const uint8_t *optionRecord,
    uint32_t optionLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    doiplite::uds::RoutineControl::Args args;
    args.subFunction = subFunction;
    args.routineId = routineId;
    args.optionRecord = optionRecord;
    args.optionLen = optionLen;
    return doiplite::uds::RoutineControl::execute(
        session, args, out, cap, len, timeoutMs);
}
