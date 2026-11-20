#pragma once

#include "doiplite/doip.h"

#include <cstdint>

namespace doiplite {
namespace uds {

/**
 * UDS 0x31 RoutineControl — one focused unit.
 * Talks to the ECU only through DoipLiteSendUdsInternal; never builds DoIP headers.
 * Sub-function, routine ID, and option record are caller-supplied.
 * Supports suppressPosRspMsgIndicationBit via SendUdsInternal.
 */
class RoutineControl final
{
public:
    struct Args
    {
        uint8_t subFunction = 0;
        uint16_t routineId = 0;
        const uint8_t *optionRecord = nullptr;
        uint32_t optionLen = 0;
    };

    RoutineControl() = default;

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
