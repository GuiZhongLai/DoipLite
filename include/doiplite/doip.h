#ifndef DOIPLITE_DOIP_H
#define DOIPLITE_DOIP_H

#include "doiplite/error.h"
#include <stdint.h>

DOIPLITE_EXTERN_C_BEGIN

/** ISO 13400 默认 UDP/TCP 端口；现场若不同，由调用参数或配置覆盖。 */
#define DOIPLITE_DEFAULT_TCP_PORT 13400
#define DOIPLITE_DEFAULT_UDP_PORT DOIPLITE_DEFAULT_TCP_PORT

/**
 * 路由激活等控制报文的默认超时（毫秒）。
 * 量级对齐 ISO 13400-2 的 A_DoIP_Ctrl，不是现场定制值。
 */
#define DOIPLITE_DEFAULT_TIMEOUT_MS 2000

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

/**
 * 车辆发现，可指定目的 IPv4 与 UDP 端口（0 表示默认 13400）。
 * destIp 为 NULL 或空字符串时使用有限广播 255.255.255.255。
 * 写入结果的 tcpPort 为本次使用的目的端口（ISO 默认 UDP/TCP 同为 13400）。
 */
DOIPLITE_API DoipLiteStatus DoipLiteDiscoverEx(
    uint32_t timeoutMs,
    const char *destIp,
    uint16_t udpPort,
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
 * 与 DoipLiteRoutingActivation 相同，但可指定超时。
 * timeoutMs 为 0 时使用 DOIPLITE_DEFAULT_TIMEOUT_MS。
 * activationType 按协议取低 8 位。
 */
DOIPLITE_API DoipLiteStatus DoipLiteRoutingActivationEx(
    DoipLiteSession *session,
    uint16_t testerAddr,
    uint16_t activationType,
    const uint8_t *oemData,
    uint32_t oemLen,
    uint32_t timeoutMs);

/** 最近一次通用 NACK 或路由激活响应码；无记录则为 0。对空会话返回 0。 */
DOIPLITE_API uint8_t DoipLiteLastDoipCode(const DoipLiteSession *session);

/** 最近一次成功路由激活得到的实体逻辑地址；尚未成功或会话为空则为 0。 */
DOIPLITE_API uint16_t DoipLiteEntityAddress(const DoipLiteSession *session);

/**
 * 发送一条 UDS 请求并等待应答。本函数只负责 DoIP 诊断消息托运，
 * 不解释 SID 语义（由 uds 层或调用方组装 sid + data）。
 *
 * 必须先完成路由激活；源/目标逻辑地址取自该次激活（测试仪地址与
 * 实体地址）。未连接或尚未激活时返回 DOIPLITE_ERR_NETWORK。
 * timeoutMs 为 0 时使用 DOIPLITE_DEFAULT_TIMEOUT_MS。
 * 收到 0x8003 或通用 NACK 时返回 DOIPLITE_ERR_DOIP_NACK，代码见
 * DoipLiteLastDoipCode。UDS 否定应答（0x7F）映射为 DOIPLITE_ERR_UDS_NRC。
 * 其中 NRC 0x78（response pending）视为中间状态，在剩余超时内继续等待最终应答。
 * 接收超时或传输中途失败会关闭 TCP，避免字节流错位；再次发送需重新连接并激活。
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

/** 关闭会话。对空指针安全。释放后请勿再次传入同一指针。 */
DOIPLITE_API void DoipLiteClose(DoipLiteSession *session);

DOIPLITE_EXTERN_C_END

#endif
