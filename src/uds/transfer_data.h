#pragma once

#include "doiplite/doip.h"

#include <cstdint>

namespace doiplite {
namespace uds {

/**
 * UDS 0x36 TransferData — one focused unit.
 * Sends blockSequenceCounter + raw block bytes from the caller.
 * Does not invent or parse OEM package formats.
 */
class TransferData final
{
public:
    struct Args
    {
        uint8_t blockSequenceCounter = 0;
        const uint8_t *data = nullptr;
        uint32_t dataLen = 0;
    };

    TransferData() = default;

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
