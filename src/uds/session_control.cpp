#include "doiplite/uds.h"

#include "doip/send_uds_internal.h"

#include <cstring>
#include <vector>

DoipLiteStatus UdsDiagnosticSessionControl(
    DoipLiteSession *session,
    uint8_t sessionType,
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
    const bool suppressPositive = (sessionType & 0x80u) != 0;

    // 内部缓冲需容纳完整 0x50 应答（子功能 + 可选 P2/P2*，偶有额外记录）。
    uint32_t respCap = cap < 64u ? 64u : cap;
    std::vector<uint8_t> resp(respCap);
    const uint8_t typeByte[1] = {sessionType};

    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUdsInternal(
        session,
        0x10,
        typeByte,
        1,
        resp.data(),
        respCap,
        &respLen,
        timeoutMs,
        suppressPositive);

    if (st != DOIPLITE_OK)
    {
        if (suppressPositive && st == DOIPLITE_ERR_TIMEOUT)
        {
            // Bounded wait for a possible NRC elapsed with intentional silence.
            // Treat as success; transport stays open for the next request.
            if (len)
            {
                *len = 0;
            }
            return DOIPLITE_OK;
        }
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

    if (respLen < 2 || resp[0] != 0x50 ||
        (resp[1] & 0x7F) != (sessionType & 0x7F))
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
