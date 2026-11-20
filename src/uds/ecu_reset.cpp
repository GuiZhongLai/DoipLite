#include "ecu_reset.h"

#include "doiplite/uds.h"
#include "doip/send_uds_internal.h"
#include "uds_result.h"

#include <vector>

namespace doiplite {
namespace uds {

DoipLiteStatus EcuReset::execute(
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

    // ISO 14229: bit7 of resetType is suppressPosRspMsgIndicationBit.
    const bool suppressPositive = (args.resetType & 0x80u) != 0;

    uint32_t respCap = cap < 16u ? 16u : cap;
    std::vector<uint8_t> resp(respCap);
    const uint8_t typeByte[1] = {args.resetType};

    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUdsInternal(
        session,
        0x11,
        typeByte,
        1,
        resp.data(),
        respCap,
        &respLen,
        timeoutMs,
        suppressPositive);

    if (st != DOIPLITE_OK)
    {
        CopyNrcIfNeeded(st, resp.data(), respLen, out, cap, len);
        return st;
    }

    if (suppressPositive && respLen == 0)
    {
        if (len)
        {
            *len = 0;
        }
        return DOIPLITE_OK;
    }

    // Positive: 0x51 | resetType (low 7 bits match)
    if (respLen < 2 || resp[0] != 0x51 ||
        (resp[1] & 0x7F) != (args.resetType & 0x7F))
    {
        return DOIPLITE_ERR_NETWORK;
    }

    return CopyPositive(resp.data(), respLen, out, cap, len);
}

} // namespace uds
} // namespace doiplite

DoipLiteStatus UdsEcuReset(
    DoipLiteSession *session,
    uint8_t resetType,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    doiplite::uds::EcuReset::Args args;
    args.resetType = resetType;
    return doiplite::uds::EcuReset::execute(session, args, out, cap, len, timeoutMs);
}
