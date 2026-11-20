#include "doiplite/doip.h"

#include "protocol.h"
#include "send_uds_internal.h"
#include "net/net_internal.h"

#include <chrono>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr char kLimitedBroadcast[] = "255.255.255.255";
constexpr uint32_t kMaxDiscoverCollect = 1024;
constexpr uint32_t kMaxOemData = 1024;

uint32_t RemainingMs(std::chrono::steady_clock::time_point deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
    {
        return 0;
    }
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
}

bool SameVehicle(const DoipLiteVehicle &a, const DoipLiteVehicle &b)
{
    return a.logicalAddress == b.logicalAddress && std::strcmp(a.ip, b.ip) == 0;
}

} // namespace

struct DoipLiteSession {
    std::string ip;
    uint16_t port = DOIPLITE_DEFAULT_TCP_PORT;
    uint8_t lastDoipCode = 0;
    uint16_t entityAddress = 0;
    uint16_t testerAddress = 0;
    bool routingActivated = false;
    uint32_t p2Ms = DOIPLITE_DEFAULT_P2_MS;
    uint32_t p2StarMs = DOIPLITE_DEFAULT_P2_STAR_MS;
    doiplite::net::TcpChannel tcp;
};

// 字节流已无法与对端对齐（超时、半帧、断连，或帧头/长度无法解析）。
// 关闭 TCP 并清除激活，避免同一会话的下一次读复用残留或迟到的字节。
static void MarkTransportBroken(DoipLiteSession *session)
{
    if (!session)
    {
        return;
    }
    session->tcp.close();
    session->routingActivated = false;
}

static bool IsTransportFailure(DoipLiteStatus status)
{
    return status == DOIPLITE_ERR_TIMEOUT || status == DOIPLITE_ERR_NETWORK;
}

DoipLiteStatus DoipLiteDiscover(
    uint32_t timeoutMs,
    DoipLiteVehicle *out,
    uint32_t cap,
    uint32_t *count)
{
    return DoipLiteDiscoverEx(timeoutMs, nullptr, 0, out, cap, count);
}

DoipLiteStatus DoipLiteDiscoverEx(
    uint32_t timeoutMs,
    const char *destIp,
    uint16_t udpPort,
    DoipLiteVehicle *out,
    uint32_t cap,
    uint32_t *count)
{
    if ((cap > 0 && !out) || !count)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    *count = 0;

    const char *ip = (destIp && destIp[0] != '\0') ? destIp : kLimitedBroadcast;
    const uint16_t port = udpPort == 0 ? DOIPLITE_DEFAULT_UDP_PORT : udpPort;

    asio::ip::address_v4 parsed;
    if (!doiplite::net::ParseIpv4(ip, &parsed))
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    doiplite::net::UdpChannel udp;
    const DoipLiteStatus opened = udp.open();
    if (opened != DOIPLITE_OK)
    {
        return opened;
    }

    const std::vector<uint8_t> request = doiplite::proto::EncodeFrame(
        doiplite::proto::kVersionDefault,
        doiplite::proto::kVidRequest,
        nullptr,
        0);
    const DoipLiteStatus sent =
        udp.send_to(ip, port, request.data(), request.size());
    if (sent != DOIPLITE_OK)
    {
        return sent;
    }

    std::vector<DoipLiteVehicle> found;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    for (;;)
    {
        if (timeoutMs > 0 && std::chrono::steady_clock::now() >= deadline)
        {
            break;
        }

        uint8_t buf[512];
        std::size_t n = 0;
        char srcIp[64] = {0};
        uint16_t srcPort = 0;
        const uint32_t waitMs = (timeoutMs == 0) ? 0 : RemainingMs(deadline);
        const DoipLiteStatus rec = udp.receive_from(
            waitMs, buf, sizeof(buf), &n, srcIp, sizeof(srcIp), &srcPort);
        if (rec == DOIPLITE_ERR_TIMEOUT)
        {
            break;
        }
        if (rec != DOIPLITE_OK)
        {
            if (found.empty())
            {
                return rec;
            }
            break;
        }

        uint8_t version = 0;
        uint16_t type = 0;
        uint32_t payloadLen = 0;
        if (!doiplite::proto::DecodeHeader(buf, n, &version, &type, &payloadLen))
        {
            if (timeoutMs == 0)
            {
                break;
            }
            continue;
        }
        if (type != doiplite::proto::kVidResponse)
        {
            if (timeoutMs == 0)
            {
                break;
            }
            continue;
        }
        if (payloadLen > doiplite::proto::kMaxControlPayload ||
            n < doiplite::proto::kHeaderSize + payloadLen)
        {
            if (timeoutMs == 0)
            {
                break;
            }
            continue;
        }

        DoipLiteVehicle vehicle;
        DoipLiteClearVehicle(&vehicle);
        uint8_t vin[17];
        if (!doiplite::proto::ParseVidResponse(
                buf + doiplite::proto::kHeaderSize, payloadLen, vin, &vehicle.logicalAddress))
        {
            if (timeoutMs == 0)
            {
                break;
            }
            continue;
        }
        std::strncpy(vehicle.ip, srcIp, sizeof(vehicle.ip) - 1);
        vehicle.tcpPort = port;
        std::memcpy(vehicle.vin, vin, 17);
        vehicle.vin[17] = 0;

        bool duplicate = false;
        for (const auto &existing : found)
        {
            if (SameVehicle(existing, vehicle))
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && found.size() < kMaxDiscoverCollect)
        {
            found.push_back(vehicle);
        }

        if (timeoutMs == 0)
        {
            break;
        }
    }

    const uint32_t total = static_cast<uint32_t>(found.size());
    const uint32_t nwrite = total < cap ? total : cap;
    for (uint32_t i = 0; i < nwrite; ++i)
    {
        out[i] = found[i];
    }
    *count = total;
    return total > 0 ? DOIPLITE_OK : DOIPLITE_ERR_TIMEOUT;
}

DoipLiteSession *DoipLiteOpen(const char *ip, uint16_t port)
{
    if (!ip || ip[0] == '\0')
    {
        return nullptr;
    }
    asio::ip::address_v4 parsed;
    if (!doiplite::net::ParseIpv4(ip, &parsed))
    {
        return nullptr;
    }

    DoipLiteSession *session = new (std::nothrow) DoipLiteSession();
    if (!session)
    {
        return nullptr;
    }
    session->ip = ip;
    session->port = port == 0 ? DOIPLITE_DEFAULT_TCP_PORT : port;
    return session;
}

static DoipLiteStatus RecvDoipFrame(doiplite::net::TcpChannel &tcp,
                                    std::chrono::steady_clock::time_point deadline,
                                    uint16_t *payloadType,
                                    std::vector<uint8_t> *payload,
                                    uint32_t maxPayload)
{
    uint8_t header[doiplite::proto::kHeaderSize];
    DoipLiteStatus st = tcp.recv_exact(header, sizeof(header), RemainingMs(deadline));
    if (st != DOIPLITE_OK)
    {
        return st;
    }

    uint8_t version = 0;
    uint32_t payloadLen = 0;
    if (!doiplite::proto::DecodeHeader(
            header, sizeof(header), &version, payloadType, &payloadLen))
    {
        return DOIPLITE_ERR_NETWORK;
    }
    if (payloadLen > maxPayload)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    payload->assign(payloadLen, 0);
    if (payloadLen > 0)
    {
        st = tcp.recv_exact(payload->data(), payloadLen, RemainingMs(deadline));
        if (st != DOIPLITE_OK)
        {
            return st;
        }
    }
    return DOIPLITE_OK;
}

DoipLiteStatus DoipLiteRoutingActivation(
    DoipLiteSession *session,
    uint16_t testerAddr,
    uint16_t activationType,
    const uint8_t *oemData,
    uint32_t oemLen)
{
    return DoipLiteRoutingActivationEx(
        session, testerAddr, activationType, oemData, oemLen, 0);
}

DoipLiteStatus DoipLiteRoutingActivationEx(
    DoipLiteSession *session,
    uint16_t testerAddr,
    uint16_t activationType,
    const uint8_t *oemData,
    uint32_t oemLen,
    uint32_t timeoutMs)
{
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (oemLen > 0 && !oemData)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (oemLen > kMaxOemData)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    session->lastDoipCode = 0;
    const uint32_t budget =
        timeoutMs == 0 ? DOIPLITE_DEFAULT_TIMEOUT_MS : timeoutMs;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budget);

    if (!session->tcp.is_open())
    {
        const DoipLiteStatus conn =
            session->tcp.connect(session->ip.c_str(), session->port, RemainingMs(deadline));
        if (conn != DOIPLITE_OK)
        {
            // connect() 失败时套接字已经关闭，这里只把激活状态收成未激活。
            MarkTransportBroken(session);
            return conn;
        }
    }

    std::vector<uint8_t> body(doiplite::proto::kRoutingRequestMinLen + oemLen, 0);
    doiplite::proto::WriteU16Be(body.data(), testerAddr);
    body[2] = static_cast<uint8_t>(activationType & 0xff);
    // bytes 3-6 reserved, already zero
    if (oemLen > 0)
    {
        std::memcpy(body.data() + doiplite::proto::kRoutingRequestMinLen, oemData, oemLen);
    }

    const std::vector<uint8_t> request = doiplite::proto::EncodeFrame(
        doiplite::proto::kVersionDefault,
        doiplite::proto::kRoutingRequest,
        body.data(),
        static_cast<uint32_t>(body.size()));

    const DoipLiteStatus sent =
        session->tcp.send_all(request.data(), request.size(), RemainingMs(deadline));
    if (sent != DOIPLITE_OK)
    {
        // 发送超时或中断时可能已经写出部分请求，不能留着这条 TCP 再读。
        if (IsTransportFailure(sent))
        {
            MarkTransportBroken(session);
        }
        return sent;
    }

    uint16_t type = 0;
    std::vector<uint8_t> payload;
    const DoipLiteStatus rec = RecvDoipFrame(
        session->tcp, deadline, &type, &payload, doiplite::proto::kMaxControlPayload);
    if (rec != DOIPLITE_OK)
    {
        // 超时或断连时，头或载荷可能只到了一部分，内核缓冲里也可能还有迟到的帧。
        if (IsTransportFailure(rec))
        {
            MarkTransportBroken(session);
        }
        return rec;
    }

    if (type == doiplite::proto::kGenericNack)
    {
        // 完整的通用 NACK：流仍然对齐，保留响应码，不把连接当成传输失败关掉。
        if (!payload.empty())
        {
            session->lastDoipCode = payload[0];
        }
        return DOIPLITE_ERR_DOIP_NACK;
    }
    if (type != doiplite::proto::kRoutingResponse ||
        payload.size() < doiplite::proto::kRoutingResponseMinLen)
    {
        // 帧已读完但不是可用的路由激活响应。后面还可能跟着无法预期的字节。
        MarkTransportBroken(session);
        return DOIPLITE_ERR_NETWORK;
    }

    // testers addr at [0..1], entity addr at [2..3], code at [4]
    session->lastDoipCode = payload[4];
    const uint16_t entity = doiplite::proto::ReadU16Be(&payload[2]);
    if (session->lastDoipCode != doiplite::proto::kRoutingSuccess)
    {
        return DOIPLITE_ERR_DOIP_NACK;
    }
    session->entityAddress = entity;
    session->testerAddress = testerAddr;
    session->routingActivated = true;
    return DOIPLITE_OK;
}

uint8_t DoipLiteLastDoipCode(const DoipLiteSession *session)
{
    return session ? session->lastDoipCode : 0;
}

uint16_t DoipLiteEntityAddress(const DoipLiteSession *session)
{
    return session ? session->entityAddress : 0;
}

void DoipLiteSetP2Timing(DoipLiteSession *session, uint32_t p2Ms, uint32_t p2StarMs)
{
    if (!session)
    {
        return;
    }
    session->p2Ms = p2Ms == 0 ? DOIPLITE_DEFAULT_P2_MS : p2Ms;
    session->p2StarMs = p2StarMs == 0 ? DOIPLITE_DEFAULT_P2_STAR_MS : p2StarMs;
}

void DoipLiteGetP2Timing(
    const DoipLiteSession *session,
    uint32_t *p2Ms,
    uint32_t *p2StarMs)
{
    if (!session)
    {
        return;
    }
    if (p2Ms)
    {
        *p2Ms = session->p2Ms;
    }
    if (p2StarMs)
    {
        *p2StarMs = session->p2StarMs;
    }
}

DoipLiteStatus DoipLiteSendUdsInternal(
    DoipLiteSession *session,
    uint8_t sid,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *resp,
    uint32_t respCap,
    uint32_t *respLen,
    uint32_t timeoutMs,
    bool optionalResponse)
{
    if (respLen)
    {
        *respLen = 0;
    }
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (dataLen > 0 && !data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (respCap > 0 && !resp)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    // 4-byte addresses + SID + data must fit in the diagnostic payload limit.
    if (dataLen > doiplite::proto::kMaxDiagnosticPayload -
                      (doiplite::proto::kDiagnosticMessageMinLen + 1))
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    session->lastDoipCode = 0;
    if (!session->routingActivated || !session->tcp.is_open())
    {
        // SendUds 只走已激活的 TCP 会话，未连接或尚未路由激活时返回 NETWORK。
        return DOIPLITE_ERR_NETWORK;
    }

    // 首窗口：调用方 timeoutMs 可覆盖本请求的 P2；其后每个 0x78 重置为完整 P2*。
    const uint32_t firstWindowMs =
        timeoutMs == 0 ? session->p2Ms : timeoutMs;
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(firstWindowMs);
    bool sawPending = false;

    const uint32_t userLen = 1 + dataLen;
    std::vector<uint8_t> body(doiplite::proto::kDiagnosticMessageMinLen + userLen, 0);
    doiplite::proto::WriteU16Be(body.data(), session->testerAddress);
    doiplite::proto::WriteU16Be(body.data() + 2, session->entityAddress);
    body[4] = sid;
    if (dataLen > 0)
    {
        std::memcpy(body.data() + 5, data, dataLen);
    }

    const std::vector<uint8_t> request = doiplite::proto::EncodeFrame(
        doiplite::proto::kVersionDefault,
        doiplite::proto::kDiagnosticMessage,
        body.data(),
        static_cast<uint32_t>(body.size()));

    // 发送使用控制超时量级，避免极短 P2 导致请求写不出去。
    const uint32_t sendBudgetMs =
        firstWindowMs < DOIPLITE_DEFAULT_TIMEOUT_MS ? DOIPLITE_DEFAULT_TIMEOUT_MS
                                                    : firstWindowMs;
    const auto sendDeadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(sendBudgetMs);
    const DoipLiteStatus sent =
        session->tcp.send_all(request.data(), request.size(), RemainingMs(sendDeadline));
    if (sent != DOIPLITE_OK)
    {
        // 发送失败可能已写出部分字节，关闭套接字以免后续错位。
        MarkTransportBroken(session);
        return sent;
    }

    for (;;)
    {
        uint16_t type = 0;
        std::vector<uint8_t> payload;
        const DoipLiteStatus rec = RecvDoipFrame(
            session->tcp,
            deadline,
            &type,
            &payload,
            doiplite::proto::kMaxDiagnosticPayload);
        if (rec != DOIPLITE_OK)
        {
            // optionalResponse 且尚未见 0x78：合法静默 → 成功且保持连接。
            // 已见 0x78 后的静默是 P2* 超时，不是 suppress 成功。
            if (optionalResponse && rec == DOIPLITE_ERR_TIMEOUT && !sawPending)
            {
                if (respLen)
                {
                    *respLen = 0;
                }
                return DOIPLITE_OK;
            }
            // 默认：超时或中途失败时可能已消耗部分帧，关闭以免字节流错位。
            MarkTransportBroken(session);
            return rec;
        }

        if (type == doiplite::proto::kGenericNack)
        {
            if (!payload.empty())
            {
                session->lastDoipCode = payload[0];
            }
            return DOIPLITE_ERR_DOIP_NACK;
        }
        if (type == doiplite::proto::kDiagnosticNack)
        {
            if (payload.size() < doiplite::proto::kDiagnosticNackMinLen)
            {
                return DOIPLITE_ERR_NETWORK;
            }
            session->lastDoipCode = payload[4];
            return DOIPLITE_ERR_DOIP_NACK;
        }
        if (type == doiplite::proto::kDiagnosticAck)
        {
            if (payload.size() < doiplite::proto::kDiagnosticAckMinLen)
            {
                return DOIPLITE_ERR_NETWORK;
            }
            session->lastDoipCode = payload[4];
            continue;
        }
        if (type != doiplite::proto::kDiagnosticMessage)
        {
            // 忽略非诊断帧（例如在途控制报文），继续等到当前窗口结束。
            continue;
        }
        if (payload.size() < doiplite::proto::kDiagnosticMessageMinLen)
        {
            return DOIPLITE_ERR_NETWORK;
        }

        const uint8_t *uds = payload.data() + doiplite::proto::kDiagnosticMessageMinLen;
        const uint32_t udsLen = static_cast<uint32_t>(
            payload.size() - doiplite::proto::kDiagnosticMessageMinLen);
        // ISO 14229 NRC 0x78：不是最终结果；重置为完整 P2* 再等下一应答。
        if (udsLen >= 3 && uds[0] == 0x7F && uds[2] == 0x78)
        {
            sawPending = true;
            deadline = std::chrono::steady_clock::now() +
                       std::chrono::milliseconds(session->p2StarMs);
            continue;
        }
        if (respLen)
        {
            *respLen = udsLen;
        }
        const uint32_t ncopy = udsLen < respCap ? udsLen : respCap;
        if (ncopy > 0 && resp)
        {
            std::memcpy(resp, uds, ncopy);
        }
        if (udsLen > respCap)
        {
            return DOIPLITE_ERR_INVALID_ARG;
        }
        if (udsLen >= 1 && uds[0] == 0x7F)
        {
            return DOIPLITE_ERR_UDS_NRC;
        }
        return DOIPLITE_OK;
    }
}

DoipLiteStatus DoipLiteSendUds(
    DoipLiteSession *session,
    uint8_t sid,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *resp,
    uint32_t respCap,
    uint32_t *respLen,
    uint32_t timeoutMs)
{
    return DoipLiteSendUdsInternal(
        session, sid, data, dataLen, resp, respCap, respLen, timeoutMs, false);
}

void DoipLiteClose(DoipLiteSession *session)
{
    if (!session)
    {
        return;
    }
    session->tcp.close();
    delete session;
}

