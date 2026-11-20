#pragma once

#include "doiplite/doip.h"

#include <cstdint>

namespace doiplite {
namespace uds {

/**
 * UDS 0x34 RequestDownload — one focused unit.
 * Encodes memoryAddress / memorySize per caller ALFID; parses 0x74 for
 * lengthFormatIdentifier and maxNumberOfBlockLength. No DoIP headers.
 */
class RequestDownload final
{
public:
    struct Args
    {
        uint8_t dataFormatId = 0;
        uint8_t addressAndLengthFormatId = 0;
        uint64_t memoryAddress = 0;
        uint64_t memorySize = 0;
    };

    struct Result
    {
        uint8_t lengthFormatId = 0;
        uint32_t maxNumberOfBlockLength = 0;
    };

    RequestDownload() = default;

    static DoipLiteStatus execute(
        DoipLiteSession *session,
        const Args &args,
        Result *result,
        uint8_t *out,
        uint32_t cap,
        uint32_t *len,
        uint32_t timeoutMs);
};

} // namespace uds
} // namespace doiplite
