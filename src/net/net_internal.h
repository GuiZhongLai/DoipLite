#pragma once

#include "doiplite/doip.h"

#include <asio.hpp>
#include <cstddef>
#include <cstdint>

void DoipLiteClearVehicle(DoipLiteVehicle *vehicle);

// 网络传输内部接口：只收发字节，不含 DoIP / UDS 语义。
namespace doiplite {
namespace net {

DoipLiteStatus MapError(const asio::error_code &ec, bool timedOut);

bool ParseIpv4(const char *ip, asio::ip::address_v4 *out);

class UdpChannel
{
public:
    UdpChannel();
    ~UdpChannel();
    UdpChannel(const UdpChannel &) = delete;
    UdpChannel &operator=(const UdpChannel &) = delete;

    DoipLiteStatus open();
    void close();
    bool is_open() const;

    DoipLiteStatus send_to(const char *ip,
                           uint16_t port,
                           const uint8_t *data,
                           std::size_t len);
    DoipLiteStatus receive_from(uint32_t timeoutMs,
                                uint8_t *buf,
                                std::size_t cap,
                                std::size_t *outLen,
                                char *srcIp,
                                std::size_t srcIpCap,
                                uint16_t *srcPort);

private:
    asio::io_context io_;
    asio::ip::udp::socket sock_;
};

class TcpChannel
{
public:
    TcpChannel();
    ~TcpChannel();
    TcpChannel(const TcpChannel &) = delete;
    TcpChannel &operator=(const TcpChannel &) = delete;

    bool is_open() const;
    DoipLiteStatus connect(const char *ip, uint16_t port, uint32_t timeoutMs);
    DoipLiteStatus send_all(const uint8_t *data, std::size_t len, uint32_t timeoutMs);
    DoipLiteStatus recv_exact(uint8_t *data, std::size_t len, uint32_t timeoutMs);
    void close();

private:
    asio::io_context io_;
    asio::ip::tcp::socket sock_;
};

} // namespace net
} // namespace doiplite
