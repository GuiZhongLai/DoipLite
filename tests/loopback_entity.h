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
        // Fake 0x22 ReadDID data for tests (not a plant / OEM DID).
        // When didData is set, a matching supportedDid returns 0x62 + DID + data.
        uint16_t supportedDid = 0x0001;
        const char *didData = nullptr;
        bool rejectUnknownDid = false; // unknown 0x22 DID -> NRC 0x31
        // Fake 0x10 DiagnosticSessionControl for tests (not a plant / OEM session).
        // When rejectUnknownSession is set, only supportedSession is accepted (NRC 0x12 otherwise).
        // When sessionIncludeTiming is set, a positive 0x50 includes P2 / P2* (on-wire units).
        uint8_t supportedSession = 0x01;
        bool rejectUnknownSession = false;
        bool sessionIncludeTiming = false;
        uint16_t sessionP2 = 0x0032;      // P2Server_max, milliseconds on the wire
        uint16_t sessionP2Star = 0x01F4;  // P2*Server_max, 10 ms units on the wire
        uint32_t udsPendingCount = 0;  // number of 0x78 pendings to send first
        bool udsDropAfterPending = false;
        // Delay before the final UDS response (and between pendings when gap is 0:
        // all pendings go out immediately, then final waits this long).
        uint32_t udsPendingDelayMs = 20;
        // Gap between successive 0x78 pendings (and before final after last pending).
        // When non-zero: pending i at i*gap, final at count*gap + udsPendingDelayMs.
        uint32_t udsPendingGapMs = 0;
        // Delay before a non-pending final response (ignored when pendings are used).
        uint32_t udsResponseDelayMs = 0;
        // First routing request only. Later requests (including a new TCP
        // connection) use the normal routingCode / logicalAddress reply.
        bool routingCloseFirst = false;      // close TCP after the request is read
        uint32_t routingDelayMs = 0;         // delay the whole 0x0006, then send it
        bool routingLateNack = false;        // delayed 0x0006 uses routingLateCode
        uint8_t routingLateCode = 0x06;      // ISO: unsupported activation type
        uint32_t routingPayloadDelayMs = 0;  // header now, payload after this delay
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
    void send_diagnostic_uds(const std::shared_ptr<Client> &client,
                             const uint8_t *addrPrefix,
                             const std::vector<uint8_t> &uds,
                             uint32_t delayMs);

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
    uint32_t routingRequests_ = 0;
};
