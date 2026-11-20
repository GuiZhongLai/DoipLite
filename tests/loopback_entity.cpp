#include "loopback_entity.h"

#include <chrono>
#include <cstring>
#include <functional>

namespace {

void WriteU16Be(uint8_t *p, uint16_t value)
{
    p[0] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[1] = static_cast<uint8_t>(value & 0xff);
}

void WriteU32Be(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    p[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    p[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[3] = static_cast<uint8_t>(value & 0xff);
}

uint16_t ReadU16Be(const uint8_t *p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t ReadU32Be(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool VersionOk(uint8_t version, uint8_t inverse)
{
    if (inverse != static_cast<uint8_t>(~version))
    {
        return false;
    }
    return version == 0x01 || version == 0x02 || version == 0x03 || version == 0xff;
}

std::vector<uint8_t> Encode(uint16_t type, const uint8_t *payload, uint32_t len, uint8_t ver = 0x02)
{
    std::vector<uint8_t> frame(8 + len, 0);
    frame[0] = ver;
    frame[1] = static_cast<uint8_t>(~ver);
    WriteU16Be(&frame[2], type);
    WriteU32Be(&frame[4], len);
    if (len > 0 && payload)
    {
        std::memcpy(frame.data() + 8, payload, len);
    }
    return frame;
}

} // namespace

struct LoopbackEntity::Client : std::enable_shared_from_this<LoopbackEntity::Client>
{
    explicit Client(asio::ip::tcp::socket s) : sock(std::move(s)) {}

    asio::ip::tcp::socket sock;
    std::array<uint8_t, 8> header{};
    std::vector<uint8_t> body;
};

LoopbackEntity::LoopbackEntity() : LoopbackEntity(Options{}) {}

LoopbackEntity::LoopbackEntity(Options options)
    : options_(options)
    , udp_(io_)
    , acceptor_(io_)
{
}

LoopbackEntity::~LoopbackEntity()
{
    stop();
}

void LoopbackEntity::clearReceivedSids()
{
    std::lock_guard<std::mutex> lock(diagLogMutex_);
    diagLog_.clear();
}

std::vector<uint8_t> LoopbackEntity::receivedSids() const
{
    std::lock_guard<std::mutex> lock(diagLogMutex_);
    std::vector<uint8_t> out;
    out.reserve(diagLog_.size());
    for (const auto &rec : diagLog_)
    {
        out.push_back(rec.sid);
    }
    return out;
}

std::vector<uint64_t> LoopbackEntity::receivedSidTimesMs() const
{
    std::lock_guard<std::mutex> lock(diagLogMutex_);
    std::vector<uint64_t> out;
    out.reserve(diagLog_.size());
    for (const auto &rec : diagLog_)
    {
        out.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(rec.at - startedAt_)
                .count()));
    }
    return out;
}

bool LoopbackEntity::start()
{
    if (running_)
    {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(diagLogMutex_);
        diagLog_.clear();
        startedAt_ = std::chrono::steady_clock::now();
    }

    asio::error_code ec;
    const auto addr = asio::ip::make_address_v4("127.0.0.1");

    acceptor_.open(asio::ip::tcp::v4(), ec);
    if (ec)
    {
        return false;
    }
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true), ec);
    acceptor_.bind(asio::ip::tcp::endpoint(addr, 0), ec);
    if (ec)
    {
        return false;
    }
    port_ = acceptor_.local_endpoint(ec).port();
    if (ec || port_ == 0)
    {
        return false;
    }
    acceptor_.listen(asio::socket_base::max_listen_connections, ec);
    if (ec)
    {
        return false;
    }

    udp_.open(asio::ip::udp::v4(), ec);
    if (ec)
    {
        return false;
    }
    udp_.bind(asio::ip::udp::endpoint(addr, port_), ec);
    if (ec)
    {
        return false;
    }

    routingRequests_ = 0;
    arm_udp();
    arm_accept();

    running_ = true;
    thread_ = std::thread([this]() { io_.run(); });
    return true;
}

void LoopbackEntity::stop()
{
    if (!running_)
    {
        if (thread_.joinable())
        {
            thread_.join();
        }
        return;
    }
    running_ = false;
    io_.stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
    asio::error_code ec;
    udp_.close(ec);
    acceptor_.close(ec);
    clients_.clear();
    io_.restart();
}

void LoopbackEntity::arm_udp()
{
    udp_.async_receive_from(
        asio::buffer(udpBuf_),
        udpSender_,
        [this](const asio::error_code &ec, std::size_t n) {
            if (!ec)
            {
                on_datagram(n);
            }
            if (udp_.is_open())
            {
                arm_udp();
            }
        });
}

void LoopbackEntity::on_datagram(std::size_t n)
{
    if (!options_.replyVid || n < 8)
    {
        return;
    }
    if (!VersionOk(udpBuf_[0], udpBuf_[1]))
    {
        return;
    }
    const uint16_t type = ReadU16Be(&udpBuf_[2]);
    if (type != 0x0001 && type != 0x0002 && type != 0x0003)
    {
        return;
    }

    uint8_t payload[33];
    std::memset(payload, 0, sizeof(payload));
    const char *vin = options_.vin ? options_.vin : "";
    const std::size_t vinLen = std::strlen(vin);
    std::memcpy(payload, vin, vinLen > 17 ? 17 : vinLen);
    WriteU16Be(&payload[17], options_.logicalAddress);
    // EID / GID remain zero; further action 0x00; optional sync 0x00.
    const auto frame = Encode(0x0004, payload, 33);
    asio::error_code ignored;
    udp_.send_to(asio::buffer(frame), udpSender_, 0, ignored);
}

void LoopbackEntity::arm_accept()
{
    auto client = std::make_shared<Client>(asio::ip::tcp::socket(io_));
    acceptor_.async_accept(client->sock, [this, client](const asio::error_code &ec) {
        if (!ec)
        {
            clients_.push_back(client);
            auto self = client;
            auto *entity = this;
            auto read_header = std::make_shared<std::function<void()>>();
            *read_header = [entity, self, read_header]() {
                asio::async_read(
                    self->sock,
                    asio::buffer(self->header),
                    [entity, self, read_header](const asio::error_code &rec, std::size_t) {
                        if (rec)
                        {
                            return;
                        }
                        if (!VersionOk(self->header[0], self->header[1]))
                        {
                            return;
                        }
                        const uint32_t len = ReadU32Be(&self->header[4]);
                        // Control frames stay small; diagnostic 0x8001 may carry a
                        // full TransferData block up to the library payload cap.
                        const uint16_t ptype = ReadU16Be(&self->header[2]);
                        const uint32_t maxLen = ptype == 0x8001 ? 65535u : 4096u;
                        if (len > maxLen)
                        {
                            return;
                        }
                        self->body.assign(len, 0);
                        auto on_body = [entity, self, read_header](const asio::error_code &bec, std::size_t) {
                            if (bec)
                            {
                                return;
                            }
                            const uint16_t type = ReadU16Be(&self->header[2]);
                            if (type == 0x0005)
                            {
                                entity->handle_routing(
                                    self,
                                    self->body.empty() ? nullptr : self->body.data(),
                                    static_cast<uint32_t>(self->body.size()));
                            }
                            else if (type == 0x8001)
                            {
                                entity->handle_diagnostic(
                                    self,
                                    self->body.empty() ? nullptr : self->body.data(),
                                    static_cast<uint32_t>(self->body.size()));
                            }
                            (*read_header)();
                        };
                        if (len == 0)
                        {
                            on_body({}, 0);
                            return;
                        }
                        asio::async_read(self->sock, asio::buffer(self->body), on_body);
                    });
            };
            (*read_header)();
        }
        if (acceptor_.is_open())
        {
            arm_accept();
        }
    });
}

void LoopbackEntity::handle_routing(const std::shared_ptr<Client> &client,
                                    const uint8_t *payload,
                                    uint32_t payloadLen)
{
    if (!payload || payloadLen < 7)
    {
        return;
    }

    const bool first = routingRequests_ == 0;
    ++routingRequests_;

    if (first && options_.routingCloseFirst)
    {
        asio::error_code ignored;
        client->sock.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        client->sock.close(ignored);
        return;
    }
    if (!options_.replyRouting)
    {
        return;
    }

    const uint16_t tester = ReadU16Be(payload);
    uint8_t code = options_.routingCode;
    if (first && options_.routingDelayMs > 0 && options_.routingLateNack)
    {
        code = options_.routingLateCode;
    }
    uint8_t body[9] = {0};
    WriteU16Be(&body[0], tester);
    WriteU16Be(&body[2], options_.logicalAddress);
    body[4] = code;
    const auto frame = std::make_shared<std::vector<uint8_t>>(Encode(0x0006, body, 9));

    auto write_at = [client, frame](std::size_t offset, std::size_t count) {
        if (!client->sock.is_open() || offset >= frame->size() || count == 0)
        {
            return;
        }
        const std::size_t avail = frame->size() - offset;
        const std::size_t n = count < avail ? count : avail;
        asio::error_code ignored;
        asio::write(client->sock, asio::buffer(frame->data() + offset, n), ignored);
    };

    // Header immediately, payload later: the client can time out between
    // the two writes and observe a partial activation response.
    if (first && options_.routingPayloadDelayMs > 0)
    {
        asio::error_code ignored;
        client->sock.set_option(asio::ip::tcp::no_delay(true), ignored);
        write_at(0, 8);
        auto timer = std::make_shared<asio::steady_timer>(io_);
        timer->expires_after(std::chrono::milliseconds(options_.routingPayloadDelayMs));
        timer->async_wait([write_at, frame, timer](const asio::error_code &ec) {
            if (ec)
            {
                return;
            }
            write_at(8, frame->size());
        });
        return;
    }

    if (!first || options_.routingDelayMs == 0)
    {
        write_at(0, frame->size());
        return;
    }

    auto timer = std::make_shared<asio::steady_timer>(io_);
    timer->expires_after(std::chrono::milliseconds(options_.routingDelayMs));
    timer->async_wait([write_at, frame, timer](const asio::error_code &ec) {
        if (ec)
        {
            return;
        }
        write_at(0, frame->size());
    });
}

void LoopbackEntity::send_diagnostic_uds(const std::shared_ptr<Client> &client,
                                         const uint8_t *addrPrefix,
                                         const std::vector<uint8_t> &uds,
                                         uint32_t delayMs)
{
    std::vector<uint8_t> body;
    body.assign(addrPrefix, addrPrefix + 4);
    body.insert(body.end(), uds.begin(), uds.end());
    const auto frame = Encode(0x8001, body.data(), static_cast<uint32_t>(body.size()));
    if (delayMs == 0)
    {
        asio::error_code ignored;
        asio::write(client->sock, asio::buffer(frame), ignored);
        return;
    }
    auto timer = std::make_shared<asio::steady_timer>(io_);
    timer->expires_after(std::chrono::milliseconds(delayMs));
    timer->async_wait([client, frame, timer](const asio::error_code &ec) {
        if (ec || !client->sock.is_open())
        {
            return;
        }
        asio::error_code ignored;
        asio::write(client->sock, asio::buffer(frame), ignored);
    });
}

void LoopbackEntity::handle_diagnostic(const std::shared_ptr<Client> &client,
                                       const uint8_t *payload,
                                       uint32_t payloadLen)
{
    if (options_.diagnosticMode == DiagnosticMode::Silent || !payload || payloadLen < 4)
    {
        return;
    }

    const uint16_t tester = ReadU16Be(payload);
    uint8_t addrPrefix[4];
    WriteU16Be(&addrPrefix[0], options_.logicalAddress);
    WriteU16Be(&addrPrefix[2], tester);

    if (options_.diagnosticMode == DiagnosticMode::Nack)
    {
        uint8_t body[5];
        std::memcpy(body, addrPrefix, 4);
        body[4] = options_.diagnosticNackCode;
        const auto frame = Encode(0x8003, body, 5);
        asio::error_code ignored;
        asio::write(client->sock, asio::buffer(frame), ignored);
        return;
    }

    if (options_.sendDiagnosticAck)
    {
        uint8_t body[5];
        std::memcpy(body, addrPrefix, 4);
        body[4] = 0x00;
        const auto frame = Encode(0x8002, body, 5);
        asio::error_code ignored;
        asio::write(client->sock, asio::buffer(frame), ignored);
    }

    const uint8_t *uds = payload + 4;
    const uint32_t udsLen = payloadLen - 4;
    if (udsLen < 1)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(diagLogMutex_);
        DiagRecord rec;
        rec.sid = uds[0];
        rec.at = std::chrono::steady_clock::now();
        diagLog_.push_back(rec);
    }

    for (uint32_t i = 0; i < options_.udsPendingCount; ++i)
    {
        const std::vector<uint8_t> pending{0x7F, uds[0], 0x78};
        const uint32_t pendingAt =
            options_.udsPendingGapMs == 0 ? 0 : i * options_.udsPendingGapMs;
        send_diagnostic_uds(client, addrPrefix, pending, pendingAt);
    }
    if (options_.udsDropAfterPending)
    {
        return;
    }
    uint32_t finalDelay = 0;
    if (options_.udsPendingCount > 0)
    {
        finalDelay = options_.udsPendingCount * options_.udsPendingGapMs +
                     options_.udsPendingDelayMs;
    }
    else
    {
        finalDelay = options_.udsResponseDelayMs;
    }

    std::vector<uint8_t> udsResp;
    if (options_.udsNrc != 0)
    {
        udsResp = {0x7F, uds[0], options_.udsNrc};
    }
    else if (uds[0] == 0x22 && (options_.didData != nullptr || options_.rejectUnknownDid))
    {
        if (udsLen < 3)
        {
            udsResp = {0x7F, 0x22, 0x13};
        }
        else
        {
            const uint16_t did = ReadU16Be(uds + 1);
            if (options_.didData != nullptr && did == options_.supportedDid)
            {
                udsResp.push_back(0x62);
                udsResp.push_back(uds[1]);
                udsResp.push_back(uds[2]);
                const char *data = options_.didData;
                const std::size_t n = std::strlen(data);
                udsResp.insert(udsResp.end(), data, data + n);
            }
            else if (options_.rejectUnknownDid)
            {
                udsResp = {0x7F, 0x22, 0x31};
            }
        }
    }
    else if (uds[0] == 0x10 &&
             (options_.rejectUnknownSession || options_.sessionIncludeTiming))
    {
        if (udsLen < 2)
        {
            udsResp = {0x7F, 0x10, 0x13};
        }
        else
        {
            const uint8_t requested = uds[1];
            const uint8_t type = static_cast<uint8_t>(requested & 0x7F);
            const bool suppress = (requested & 0x80u) != 0;
            if (options_.rejectUnknownSession &&
                type != static_cast<uint8_t>(options_.supportedSession & 0x7F))
            {
                // NRC is always returned, even when suppressPositiveResponse is set.
                udsResp = {0x7F, 0x10, 0x12};
            }
            else if (!suppress)
            {
                udsResp = {0x50, requested};
                if (options_.sessionIncludeTiming)
                {
                    uint8_t timing[4];
                    WriteU16Be(timing, options_.sessionP2);
                    WriteU16Be(timing + 2, options_.sessionP2Star);
                    udsResp.insert(udsResp.end(), timing, timing + 4);
                }
            }
            // suppress + accepted type: intentional silence (no positive response).
        }
    }
    else if (uds[0] == 0x3E && options_.rejectUnknownTesterPresent)
    {
        if (udsLen < 2)
        {
            udsResp = {0x7F, 0x3E, 0x13};
        }
        else
        {
            const uint8_t requested = uds[1];
            const uint8_t type = static_cast<uint8_t>(requested & 0x7F);
            const bool suppress = (requested & 0x80u) != 0;
            if (type != static_cast<uint8_t>(options_.supportedTesterPresent & 0x7F))
            {
                // NRC is always returned, even when suppressPositiveResponse is set.
                udsResp = {0x7F, 0x3E, 0x12};
            }
            else if (!suppress)
            {
                udsResp = {0x7E, type};
            }
            // suppress + accepted type: intentional silence (no positive response).
        }
    }
    else if (uds[0] == 0x31 && options_.rejectUnknownRoutine)
    {
        if (udsLen < 4)
        {
            udsResp = {0x7F, 0x31, 0x13};
        }
        else
        {
            const uint8_t requested = uds[1];
            const bool suppress = (requested & 0x80u) != 0;
            const uint16_t rid = ReadU16Be(uds + 2);
            if (rid != options_.supportedRoutineId)
            {
                // NRC is always returned, even when suppressPositiveResponse is set.
                udsResp = {0x7F, 0x31, 0x31};
            }
            else if (!suppress)
            {
                udsResp = {0x71, static_cast<uint8_t>(requested & 0x7F), uds[2], uds[3]};
            }
            // suppress + accepted routine: intentional silence (no positive response).
        }
    }
    else if (uds[0] == 0x34)
    {
        if (options_.rejectRequestDownload)
        {
            udsResp = {0x7F, 0x34, 0x70}; // uploadDownloadNotAccepted (placeholder NRC)
        }
        else if (udsLen < 3)
        {
            udsResp = {0x7F, 0x34, 0x13};
        }
        else
        {
            // Positive 0x74 with configured maxNumberOfBlockLength.
            const uint8_t lfid = options_.downloadLengthFormatId;
            const uint8_t nbytes = static_cast<uint8_t>((lfid >> 4) & 0x0f);
            udsResp.push_back(0x74);
            udsResp.push_back(lfid);
            uint8_t be[4];
            WriteU32Be(be, options_.downloadMaxBlockLength);
            const uint8_t use = nbytes < 1 ? 1 : (nbytes > 4 ? 4 : nbytes);
            udsResp.insert(udsResp.end(), be + (4 - use), be + 4);
        }
    }
    else if (uds[0] == 0x36)
    {
        // Positive 0x76 + blockSequenceCounter only (do not echo block data).
        if (udsLen < 2)
        {
            udsResp = {0x7F, 0x36, 0x13};
        }
        else
        {
            udsResp = {0x76, uds[1]};
        }
    }
    else if (uds[0] == 0x37)
    {
        udsResp = {0x77};
    }
    else if (uds[0] == 0x11 && options_.rejectUnknownReset)
    {
        if (udsLen < 2)
        {
            udsResp = {0x7F, 0x11, 0x13};
        }
        else
        {
            const uint8_t requested = uds[1];
            const uint8_t type = static_cast<uint8_t>(requested & 0x7F);
            const bool suppress = (requested & 0x80u) != 0;
            if (type != static_cast<uint8_t>(options_.supportedResetType & 0x7F))
            {
                udsResp = {0x7F, 0x11, 0x12};
            }
            else if (!suppress)
            {
                udsResp = {0x51, type};
            }
        }
    }

    if (udsResp.empty())
    {
        // Default 0x10 / 0x3E / 0x11 / 0x31 echo also honors suppressPositiveResponse bit7.
        if ((uds[0] == 0x10 || uds[0] == 0x3E || uds[0] == 0x11 || uds[0] == 0x31) &&
            udsLen >= 2 && (uds[1] & 0x80u) != 0)
        {
            return;
        }
        udsResp.push_back(static_cast<uint8_t>(uds[0] + 0x40));
        if (udsLen > 1)
        {
            udsResp.insert(udsResp.end(), uds + 1, uds + udsLen);
        }
    }

    send_diagnostic_uds(client, addrPrefix, udsResp, finalDelay);
}
