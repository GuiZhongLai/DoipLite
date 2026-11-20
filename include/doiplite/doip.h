#ifndef DOIPLITE_DOIP_H
#define DOIPLITE_DOIP_H

#include "doiplite/error.h"
#include <stdint.h>

DOIPLITE_EXTERN_C_BEGIN

/** ISO 13400 默认 UDP/TCP 端口；现场若不同，由调用参数或配置覆盖。 */
#define DOIPLITE_DEFAULT_TCP_PORT 13400

typedef struct DoipLiteVehicle {
    char ip[64];
    uint16_t tcpPort;
    uint16_t logicalAddress;
    uint8_t vin[18];
} DoipLiteVehicle;

typedef struct DoipLiteSession DoipLiteSession;

DOIPLITE_API DoipLiteStatus DoipLiteDiscover(
    uint32_t timeoutMs,
    DoipLiteVehicle *out,
    uint32_t cap,
    uint32_t *count);

DOIPLITE_API DoipLiteSession *DoipLiteOpen(const char *ip, uint16_t port);

DOIPLITE_API DoipLiteStatus DoipLiteRoutingActivation(
    DoipLiteSession *session,
    uint16_t testerAddr,
    uint16_t activationType,
    const uint8_t *oemData,
    uint32_t oemLen);

/**
 * 发送一条 UDS 请求并等待应答。本函数只负责 DoIP 诊断消息托运，
 * 不解释 SID 语义（由 uds 层或调用方组装 sid + data）。
 */
DOIPLITE_API DoipLiteStatus DoipLiteSendUds(
    DoipLiteSession *session,
    uint8_t sid,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *resp,
    uint32_t respCap,
    uint32_t *respLen,
    uint32_t timeoutMs);

DOIPLITE_API void DoipLiteClose(DoipLiteSession *session);

DOIPLITE_EXTERN_C_END

#endif
