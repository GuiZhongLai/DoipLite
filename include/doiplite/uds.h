#ifndef DOIPLITE_UDS_H
#define DOIPLITE_UDS_H

#include "doiplite/doip.h"

DOIPLITE_EXTERN_C_BEGIN

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x22 ReadDataByIdentifier。
 * DID 只来自调用参数，库不提供默认 DID。
 * 成功时 out 写入 0x62 与 DID 之后的数据记录，*len 为该记录长度。
 * 收到 UDS 否定应答时返回 DOIPLITE_ERR_UDS_NRC，out 写入完整 0x7F 应答（含 NRC）。
 * 未激活会话由 SendUds 返回 DOIPLITE_ERR_NETWORK。
 */
DOIPLITE_API DoipLiteStatus UdsReadDataByIdentifier(
    DoipLiteSession *session,
    uint16_t did,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

DOIPLITE_EXTERN_C_END

#endif
