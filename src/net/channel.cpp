#include "net_internal.h"

#include <chrono>
#include <cstring>
#include <utility>

namespace doiplite {
namespace net {

DoipLiteStatus MapError(const asio::error_code &ec, bool timedOut)
{
    if (timedOut || ec == asio::error::timed_out ||
        ec == asio::error::operation_aborted)
    {
        return DOIPLITE_ERR_TIMEOUT;
    }
    if (!ec)
    {
        return DOIPLITE_OK;
    }
    if (ec == asio::error::invalid_argument)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    return DOIPLITE_ERR_NETWORK;
}

bool ParseIpv4(const char *ip, asio::ip::address_v4 *out)
{
    if (!ip || ip[0] == '\0' || !out)
    {
        return false;
    }
    asio::error_code ec;
    *out = asio::ip::make_address_v4(ip, ec);
    return !ec;
}

namespace {

template <typename CancelFn, typename StartFn>
DoipLiteStatus RunWithTimeout(asio::io_context &io,
                              uint32_t timeoutMs,
                              CancelFn &&cancel,
                              StartFn &&start)
{
    asio::error_code result;
    bool completed = false;
    start([&](const asio::error_code &ec, std::size_t) {
        result = ec;
        completed = true;
    });

    io.restart();
    if (timeoutMs == 0)
    {
        io.poll();
    }
    else
    {
        io.run_for(std::chrono::milliseconds(timeoutMs));
    }

    if (!completed)
    {
        asio::error_code ignored;
        cancel(ignored);
        io.run();
        return DOIPLITE_ERR_TIMEOUT;
    }
    return MapError(result, false);
}

} // namespace

UdpChannel::UdpChannel() : sock_(io_) {}

UdpChannel::~UdpChannel()
{
    close();
}

bool UdpChannel::is_open() const
{
    return sock_.is_open();
}

void UdpChannel::close()
{
    asio::error_code ec;
    sock_.close(ec);
}

DoipLiteStatus UdpChannel::open()
{
    close();
    asio::error_code ec;
    sock_.open(asio::ip::udp::v4(), ec);
    if (ec)
    {
        return DOIPLITE_ERR_NETWORK;
    }
    sock_.set_option(asio::socket_base::broadcast(true), ec);
    if (ec)
    {
        close();
        return DOIPLITE_ERR_NETWORK;
    }
    sock_.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), 0), ec);
    if (ec)
    {
        close();
        return DOIPLITE_ERR_NETWORK;
    }
    return DOIPLITE_OK;
}

DoipLiteStatus UdpChannel::send_to(const char *ip,
                                   uint16_t port,
                                   const uint8_t *data,
                                   std::size_t len)
{
    if (!sock_.is_open())
    {
        return DOIPLITE_ERR_NETWORK;
    }
    if (len > 0 && !data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    asio::ip::address_v4 addr;
    if (!ParseIpv4(ip, &addr) || port == 0)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    asio::error_code ec;
    sock_.send_to(asio::const_buffer(data, len),
                  asio::ip::udp::endpoint(addr, port),
                  0,
                  ec);
    if (ec)
    {
        return DOIPLITE_ERR_NETWORK;
    }
    return DOIPLITE_OK;
}

DoipLiteStatus UdpChannel::receive_from(uint32_t timeoutMs,
                                        uint8_t *buf,
                                        std::size_t cap,
                                        std::size_t *outLen,
                                        char *srcIp,
                                        std::size_t srcIpCap,
                                        uint16_t *srcPort)
{
    if (!outLen)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    *outLen = 0;
    if (cap > 0 && !buf)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (!sock_.is_open())
    {
        return DOIPLITE_ERR_NETWORK;
    }

    asio::ip::udp::endpoint sender;
    std::size_t n = 0;
    asio::error_code recEc;
    bool completed = false;
    sock_.async_receive_from(
        asio::buffer(buf, cap),
        sender,
        [&](const asio::error_code &ec, std::size_t bytes) {
            recEc = ec;
            n = bytes;
            completed = true;
        });

    io_.restart();
    if (timeoutMs == 0)
    {
        io_.poll();
    }
    else
    {
        io_.run_for(std::chrono::milliseconds(timeoutMs));
    }

    if (!completed)
    {
        asio::error_code ignored;
        sock_.cancel(ignored);
        io_.run();
        return DOIPLITE_ERR_TIMEOUT;
    }
    if (recEc)
    {
        return MapError(recEc, false);
    }

    *outLen = n;
    if (srcPort)
    {
        *srcPort = sender.port();
    }
    if (srcIp && srcIpCap > 0)
    {
        const std::string text = sender.address().to_string();
        std::strncpy(srcIp, text.c_str(), srcIpCap - 1);
        srcIp[srcIpCap - 1] = '\0';
    }
    return DOIPLITE_OK;
}

TcpChannel::TcpChannel() : sock_(io_) {}

TcpChannel::~TcpChannel()
{
    close();
}

bool TcpChannel::is_open() const
{
    return sock_.is_open();
}

void TcpChannel::close()
{
    asio::error_code ec;
    if (sock_.is_open())
    {
        sock_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    }
    sock_.close(ec);
}

DoipLiteStatus TcpChannel::connect(const char *ip, uint16_t port, uint32_t timeoutMs)
{
    close();
    asio::ip::address_v4 addr;
    if (!ParseIpv4(ip, &addr) || port == 0)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    asio::error_code openEc;
    sock_.open(asio::ip::tcp::v4(), openEc);
    if (openEc)
    {
        return DOIPLITE_ERR_NETWORK;
    }

    const DoipLiteStatus st = RunWithTimeout(
        io_,
        timeoutMs,
        [&](asio::error_code &ec) { sock_.cancel(ec); },
        [&](auto &&handler) {
            sock_.async_connect(asio::ip::tcp::endpoint(addr, port),
                                [h = std::forward<decltype(handler)>(handler)](
                                    const asio::error_code &ec) { h(ec, 0); });
        });
    if (st != DOIPLITE_OK)
    {
        close();
        return st;
    }
    return DOIPLITE_OK;
}

DoipLiteStatus TcpChannel::send_all(const uint8_t *data, std::size_t len, uint32_t timeoutMs)
{
    if (len == 0)
    {
        return DOIPLITE_OK;
    }
    if (!data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (!sock_.is_open())
    {
        return DOIPLITE_ERR_NETWORK;
    }
    return RunWithTimeout(
        io_,
        timeoutMs,
        [&](asio::error_code &ec) { sock_.cancel(ec); },
        [&](auto &&handler) {
            asio::async_write(sock_, asio::buffer(data, len), std::forward<decltype(handler)>(handler));
        });
}

DoipLiteStatus TcpChannel::recv_exact(uint8_t *data, std::size_t len, uint32_t timeoutMs)
{
    if (len == 0)
    {
        return DOIPLITE_OK;
    }
    if (!data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (!sock_.is_open())
    {
        return DOIPLITE_ERR_NETWORK;
    }
    return RunWithTimeout(
        io_,
        timeoutMs,
        [&](asio::error_code &ec) { sock_.cancel(ec); },
        [&](auto &&handler) {
            asio::async_read(sock_, asio::buffer(data, len), std::forward<decltype(handler)>(handler));
        });
}

} // namespace net
} // namespace doiplite
