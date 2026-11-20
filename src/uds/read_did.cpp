#include "doiplite/uds.h"

DoipLiteStatus UdsReadDataByIdentifier(
    DoipLiteSession *session,
    uint16_t did,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (cap > 0 && !out)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (len)
    {
        *len = 0;
    }
    uint8_t data[2];
    data[0] = static_cast<uint8_t>((did >> 8) & 0xff);
    data[1] = static_cast<uint8_t>(did & 0xff);
    return DoipLiteSendUds(session, 0x22, data, 2, out, cap, len, timeoutMs);
}
