#include "doiplite/uds.h"

#include <cstring>
#include <limits>
#include <vector>

DoipLiteStatus UdsReadDataByIdentifier(
    DoipLiteSession *session,
    uint16_t did,
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

    // 内部缓冲需容纳 0x62 + DID(2) + 数据记录。
    uint32_t respCap = cap;
    if (respCap <= std::numeric_limits<uint32_t>::max() - 3u)
    {
        respCap += 3u;
    }
    else
    {
        respCap = std::numeric_limits<uint32_t>::max();
    }

    std::vector<uint8_t> resp(respCap);
    const uint8_t didBytes[2] = {
        static_cast<uint8_t>((did >> 8) & 0xff),
        static_cast<uint8_t>(did & 0xff),
    };

    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUds(
        session,
        0x22,
        didBytes,
        2,
        resp.empty() ? nullptr : resp.data(),
        respCap,
        &respLen,
        timeoutMs);

    if (st != DOIPLITE_OK)
    {
        // NRC 时把完整 0x7F 应答交给调用方，便于打印否定响应码。
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

    if (respLen < 3 || resp[0] != 0x62)
    {
        return DOIPLITE_ERR_NETWORK;
    }
    const uint16_t gotDid = static_cast<uint16_t>(
        (static_cast<uint16_t>(resp[1]) << 8) | resp[2]);
    if (gotDid != did)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    const uint32_t dataLen = respLen - 3;
    if (len)
    {
        *len = dataLen;
    }
    if (dataLen > cap)
    {
        if (cap > 0 && out)
        {
            std::memcpy(out, resp.data() + 3, cap);
        }
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (dataLen > 0 && out)
    {
        std::memcpy(out, resp.data() + 3, dataLen);
    }
    return DOIPLITE_OK;
}
