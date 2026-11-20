#include "protocol.h"

#include <cstring>

namespace doiplite {
namespace proto {

bool IsSupportedVersion(uint8_t version)
{
    return version == kVersion2010 || version == kVersionDefault ||
           version == kVersion2019 || version == kVersionVidDefault;
}

bool HeaderVersionValid(uint8_t version, uint8_t inverse)
{
    return IsSupportedVersion(version) && inverse == InverseVersion(version);
}

std::vector<uint8_t> EncodeFrame(uint8_t version,
                                 uint16_t payloadType,
                                 const uint8_t *payload,
                                 uint32_t payloadLen)
{
    std::vector<uint8_t> frame(kHeaderSize + payloadLen, 0);
    frame[0] = version;
    frame[1] = InverseVersion(version);
    WriteU16Be(&frame[2], payloadType);
    WriteU32Be(&frame[4], payloadLen);
    if (payloadLen > 0 && payload)
    {
        std::memcpy(frame.data() + kHeaderSize, payload, payloadLen);
    }
    return frame;
}

bool DecodeHeader(const uint8_t *data,
                  std::size_t len,
                  uint8_t *version,
                  uint16_t *payloadType,
                  uint32_t *payloadLen)
{
    if (!data || len < kHeaderSize || !version || !payloadType || !payloadLen)
    {
        return false;
    }
    const uint8_t ver = data[0];
    const uint8_t inv = data[1];
    if (!HeaderVersionValid(ver, inv))
    {
        return false;
    }
    *version = ver;
    *payloadType = ReadU16Be(&data[2]);
    *payloadLen = ReadU32Be(&data[4]);
    return true;
}

bool ParseVidResponse(const uint8_t *payload,
                      uint32_t payloadLen,
                      uint8_t vin[17],
                      uint16_t *logicalAddress)
{
    if (!payload || !vin || !logicalAddress || payloadLen < kVidResponseMinLen)
    {
        return false;
    }
    std::memcpy(vin, payload, kVinLen);
    *logicalAddress = ReadU16Be(&payload[kVinLen]);
    return true;
}

} // namespace proto
} // namespace doiplite
