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

/**
 * ISO 14229 默认 P2Server_max（毫秒）：首应答或首个 0x78 的等待窗口。
 * 量级对齐标准常见默认，不是现场 / OEM 定制值。
 */
#define DOIPLITE_DEFAULT_P2_MS 50

/**
 * ISO 14229 默认 P2*Server_max（毫秒）：收到 0x78 之后到下一应答的等待窗口。
 * 线格式中 P2* 常以 10ms 为单位；本 API 一律使用毫秒。
 */
#define DOIPLITE_DEFAULT_P2_STAR_MS 5000

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
 *
 * 超时或网络失败（含半帧、断连，以及无法解析的帧）会关闭 TCP，并清除
 * 本会话的激活状态与 EntityAddress。同一会话再次调用会重新连接，不会把
 * 上一次迟到或残留的应答当成新的结果，也不会复用陈旧实体地址。完整的
 * 路由激活否定或通用 NACK 仍返回 DOIPLITE_ERR_DOIP_NACK，响应码保留在
 * DoipLiteLastDoipCode，连接保持打开。
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

/**
 * 最近一次成功路由激活得到的实体逻辑地址；尚未成功、会话为空、或诊断 TCP
 * 已因传输失败/超时关闭时为 0（避免重连复用陈旧地址）。
 */
DOIPLITE_API uint16_t DoipLiteEntityAddress(const DoipLiteSession *session);

/**
 * 设置会话的 P2 / P2* 等待窗口（毫秒）。对空会话无操作。
 * p2Ms 或 p2StarMs 为 0 时回落到对应默认值。
 * 也可由 UdsDiagnosticSessionControl 在收到带时序参数的 0x50 后自动更新。
 */
DOIPLITE_API void DoipLiteSetP2Timing(
    DoipLiteSession *session,
    uint32_t p2Ms,
    uint32_t p2StarMs);

/**
 * 读取会话当前 P2 / P2*（毫秒）。对空会话或空输出指针安全（对应项不写）。
 */
DOIPLITE_API void DoipLiteGetP2Timing(
    const DoipLiteSession *session,
    uint32_t *p2Ms,
    uint32_t *p2StarMs);

/**
 * 发送一条 UDS 请求并等待应答。本函数只负责 DoIP 诊断消息托运，
 * 不解释 SID 语义（由 uds 层或调用方组装 sid + data）。
 *
 * 必须先完成路由激活；源/目标逻辑地址取自该次激活（测试仪地址与
 * 实体地址）。未连接或尚未激活时返回 DOIPLITE_ERR_NETWORK。
 *
 * 等待时序（ISO 14229 P2 / P2*）：
 * - 首应答（或首个 0x78）窗口：timeoutMs 非 0 时用 timeoutMs，否则用会话 P2；
 * - 每收到 NRC 0x78（response pending）后，将窗口重置为完整的会话 P2*，
 *   继续等待下一 0x78 或最终应答；0x78 不是最终 NRC；
 * - 最终否定应答（其它 NRC）仍映射为 DOIPLITE_ERR_UDS_NRC。
 *
 * 收到 0x8003 或通用 NACK 时返回 DOIPLITE_ERR_DOIP_NACK，代码见
 * DoipLiteLastDoipCode。接收超时或传输中途失败会关闭 TCP 并清除激活状态
 * 与 EntityAddress，避免字节流错位与陈旧地址；再次发送需重新连接并激活。
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
