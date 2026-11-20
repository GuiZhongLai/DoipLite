#pragma once

#include "doiplite/doip.h"

#include <cstdint>

namespace doiplite {
namespace uds {

/**
 * UDS 0x37 RequestTransferExit — one focused unit.
 * Optional transferRequestParameterRecord is raw caller bytes.
 */
class RequestTransferExit final
{
public:
    struct Args
    {
        const uint8_t *parameterRecord = nullptr;
        uint32_t parameterLen = 0;
    };

    RequestTransferExit() = default;

    static DoipLiteStatus execute(
        DoipLiteSession *session,
        const Args &args,
        uint8_t *out,
        uint32_t cap,
        uint32_t *len,
        uint32_t timeoutMs);
};

} // namespace uds
} // namespace doiplite
