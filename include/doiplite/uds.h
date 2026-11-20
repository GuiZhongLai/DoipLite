#ifndef DOIPLITE_UDS_H
#define DOIPLITE_UDS_H

#include "doiplite/doip.h"

DOIPLITE_EXTERN_C_BEGIN

/** 通过 SendUds 读取 0x22 ReadDataByIdentifier。DID 由调用方/配置提供。 */
DOIPLITE_API DoipLiteStatus UdsReadDataByIdentifier(
    DoipLiteSession *session,
    uint16_t did,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

DOIPLITE_EXTERN_C_END

#endif
