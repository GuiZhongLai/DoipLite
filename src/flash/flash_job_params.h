#pragma once

#include "doiplite/flash.h"

#include <cstdint>
#include <string>
#include <vector>

namespace doiplite {
namespace flash {

/**
 * Owned copy of flash job parameters + payload bytes.
 * Pure data: no sockets, no UDS calls. Built once at FlashStart.
 */
struct FlashJobParams
{
    bool haveFlashSessionType = false;
    uint8_t flashSessionType = 0;

    bool haveSecurity = false;
    uint8_t securityLevel = 0;

    bool haveEraseRoutine = false;
    uint8_t eraseRoutineSubFunction = 0;
    uint16_t eraseRoutineId = 0;
    std::vector<uint8_t> eraseRoutineOption;

    uint8_t dataFormatId = 0;
    uint8_t addressAndLengthFormatId = 0;
    uint64_t memoryAddress = 0;
    uint64_t memorySize = 0;

    std::vector<uint8_t> payload;
    uint32_t maxBlockDataCap = 0;

    std::vector<uint8_t> transferExitParam;

    bool haveEcuReset = false;
    uint8_t resetType = 0;

    // Periodic 0x3E keep-alive: both interval > 0 and haveKeepAliveSubFunction
    // required; library invents neither default.
    uint32_t keepAliveIntervalMs = 0;
    bool haveKeepAliveSubFunction = false;
    uint8_t keepAliveSubFunction = 0;

    uint32_t timeoutMs = 0;
};

} // namespace flash
} // namespace doiplite
