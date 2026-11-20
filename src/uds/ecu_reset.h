#pragma once

#include "doiplite/doip.h"

#include <cstdint>

namespace doiplite {
namespace uds {

/**
 * UDS 0x11 ECUReset — one focused unit.
 * Reset type is caller-supplied (no plant default).
 * Supports suppressPosRspMsgIndicationBit via SendUdsInternal.
 */
class EcuReset final
{
public:
    struct Args
    {
        uint8_t resetType = 0;
    };

    EcuReset() = default;

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
