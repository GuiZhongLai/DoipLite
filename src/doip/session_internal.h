#pragma once

#include "doiplite/doip.h"
#include "doiplite/flash.h"
#include "net/net_internal.h"

#include <memory>
#include <string>

namespace doiplite {
namespace flash {
class FlashOrchestrator;
} // namespace flash
} // namespace doiplite

// Opaque session body shared by doip transport and flash ownership.
// FlashOrchestrator is incomplete here; destructor is out-of-line in session.cpp.
struct DoipLiteSession
{
    std::string ip;
    uint16_t port = DOIPLITE_DEFAULT_TCP_PORT;
    uint8_t lastDoipCode = 0;
    uint16_t entityAddress = 0;
    uint16_t testerAddress = 0;
    bool routingActivated = false;
    uint32_t p2Ms = DOIPLITE_DEFAULT_P2_MS;
    uint32_t p2StarMs = DOIPLITE_DEFAULT_P2_STAR_MS;
    doiplite::net::TcpChannel tcp;

    // Stored for a future 0x27 path; flash orchestration does not call it yet.
    DoipLiteSeedKeyFn seedKeyFn = nullptr;
    void *seedKeyUser = nullptr;

    std::unique_ptr<doiplite::flash::FlashOrchestrator> flash;

    DoipLiteSession();
    ~DoipLiteSession();

    DoipLiteSession(const DoipLiteSession &) = delete;
    DoipLiteSession &operator=(const DoipLiteSession &) = delete;
};
