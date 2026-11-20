#include "doiplite/uds.h"

#include "doip/send_uds_internal.h"

#include <cstring>
#include <vector>

DoipLiteStatus UdsTesterPresent(
    DoipLiteSession *session,
    uint8_t subFunction,
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

    // ISO 14229: bit7 of the sub-function is suppressPosRspMsgIndicationBit.
    // When set, a positive response is not required; NRC must still be reported.
    const bool suppressPositive = (subFunction & 0x80u) != 0;

    // 内部缓冲需容纳完整 0x7E 应答（子功能；偶有额外记录）。
    uint32_t respCap = cap < 16u ? 16u : cap;
    std::vector<uint8_t> resp(respCap);
    const uint8_t subByte[1] = {subFunction};

    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUdsInternal(
        session,
        0x3E,
        subByte,
        1,
        resp.data(),
        respCap,
        &respLen,
        timeoutMs,
        suppressPositive);

    if (st != DOIPLITE_OK)
    {
        if (st == DOIPLITE_ERR_UDS_NRC)
        {
            if (len)
            {
                *len = respLen;
            }
            const uint32_t ncopy = respLen < cap ? respLen : cap;
            if (ncopy > 0 && out)
            {
                std::memcpy(out, resp.data(), ncopy);
            }
        }
        return st;
    }

    // suppressPositive 且对端完全静默：SendUdsInternal 已返回 OK 且 respLen=0。
    // 非 suppress 时空载荷仍落到下方校验，返回 NETWORK。
    if (suppressPositive && respLen == 0)
    {
        if (len)
        {
            *len = 0;
        }
        return DOIPLITE_OK;
    }

    if (respLen < 2 || resp[0] != 0x7E ||
        (resp[1] & 0x7F) != (subFunction & 0x7F))
    {
        return DOIPLITE_ERR_NETWORK;
    }

    if (len)
    {
        *len = respLen;
    }
    if (respLen > cap)
    {
        if (cap > 0 && out)
        {
            std::memcpy(out, resp.data(), cap);
        }
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (respLen > 0 && out)
    {
        std::memcpy(out, resp.data(), respLen);
    }
    return DOIPLITE_OK;
}
