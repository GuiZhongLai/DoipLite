#include "loopback_entity.h"

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

bool LoopbackEntity::start()
{
    if (running_)
    {
        return true;
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
                        if (len > 4096)
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
    if (!options_.replyRouting || !payload || payloadLen < 7)
    {
        return;
    }
    const uint16_t tester = ReadU16Be(payload);
    uint8_t body[9] = {0};
    WriteU16Be(&body[0], tester);
    WriteU16Be(&body[2], options_.logicalAddress);
    body[4] = options_.routingCode;
    const auto frame = Encode(0x0006, body, 9);
    asio::error_code ignored;
    asio::write(client->sock, asio::buffer(frame), ignored);
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

    std::vector<uint8_t> resp;
    resp.assign(addrPrefix, addrPrefix + 4);
    if (options_.udsNrc != 0)
    {
        resp.push_back(0x7F);
        resp.push_back(uds[0]);
        resp.push_back(options_.udsNrc);
    }
    else
    {
        resp.push_back(static_cast<uint8_t>(uds[0] + 0x40));
        if (udsLen > 1)
        {
            resp.insert(resp.end(), uds + 1, uds + udsLen);
        }
    }

    const auto frame = Encode(0x8001, resp.data(), static_cast<uint32_t>(resp.size()));
    asio::error_code ignored;
    asio::write(client->sock, asio::buffer(frame), ignored);
}
