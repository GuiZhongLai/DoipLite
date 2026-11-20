#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>

// In-process ISO 13400-2 loopback entity for tests. Binds UDP+TCP on the
// same ephemeral port on 127.0.0.1. VIN and addresses are synthetic
// examples, not field or customer values.
class LoopbackEntity
{
public:
    enum class DiagnosticMode
    {
        Response, // 0x8001 UDS response (optional 0x8002 ACK first)
        Nack,     // 0x8003 Diagnostic Message NACK
        Silent    // no diagnostic reply (caller should time out)
    };

    struct Options
    {
        uint16_t logicalAddress = 0x1234;
        // 17-character example VIN (not a real vehicle).
        const char *vin = "DOIPLITEEXAMPLE01";
        uint8_t routingCode = 0x10;
        bool replyVid = true;
        bool replyRouting = true;
        DiagnosticMode diagnosticMode = DiagnosticMode::Response;
        bool sendDiagnosticAck = false;
        uint8_t diagnosticNackCode = 0x02; // ISO: unknown target address
        uint8_t udsNrc = 0;                // non-zero: reply 0x7F + SID + NRC
    };

    LoopbackEntity();
    explicit LoopbackEntity(Options options);
    ~LoopbackEntity();

    LoopbackEntity(const LoopbackEntity &) = delete;
    LoopbackEntity &operator=(const LoopbackEntity &) = delete;

    bool start();
    void stop();

    uint16_t port() const { return port_; }
    const char *ip() const { return "127.0.0.1"; }

private:
    struct Client;

    void arm_udp();
    void arm_accept();
    void on_datagram(std::size_t n);
    void handle_routing(const std::shared_ptr<Client> &client,
                        const uint8_t *payload,
                        uint32_t payloadLen);
    void handle_diagnostic(const std::shared_ptr<Client> &client,
                           const uint8_t *payload,
                           uint32_t payloadLen);

    Options options_;
    asio::io_context io_;
    asio::ip::udp::socket udp_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ip::udp::endpoint udpSender_;
    std::array<uint8_t, 512> udpBuf_{};
    std::vector<std::shared_ptr<Client>> clients_;
    std::thread thread_;
    uint16_t port_ = 0;
    bool running_ = false;
};
