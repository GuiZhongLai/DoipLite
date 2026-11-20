#include "doiplite/uds.h"

DoipLiteStatus UdsReadDataByIdentifier(
    DoipLiteSession *session,
    uint16_t did,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    (void)did;
    (void)timeoutMs;
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
    // 0x22 / 读版本留给下一个 PR，本函数暂不调用 DoipLiteSendUds。
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}
