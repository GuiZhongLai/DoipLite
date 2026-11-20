#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace doiplite {
namespace proto {

// ISO 13400-2 通用协议版本。默认发送 0x02（2012/常见实现）；
// 接收时兼容 0x01、0x02、0x03 以及车辆识别请求用的 0xFF。
constexpr uint8_t kVersionDefault = 0x02;
constexpr uint8_t kVersion2010 = 0x01;
constexpr uint8_t kVersion2019 = 0x03;
constexpr uint8_t kVersionVidDefault = 0xFF;

constexpr std::size_t kHeaderSize = 8;
constexpr uint32_t kMaxControlPayload = 4096;

constexpr uint16_t kGenericNack = 0x0000;
constexpr uint16_t kVidRequest = 0x0001;
constexpr uint16_t kVidRequestEid = 0x0002;
constexpr uint16_t kVidRequestVin = 0x0003;
constexpr uint16_t kVidResponse = 0x0004;
constexpr uint16_t kRoutingRequest = 0x0005;
constexpr uint16_t kRoutingResponse = 0x0006;

constexpr uint8_t kRoutingSuccess = 0x10;

constexpr std::size_t kVinLen = 17;
constexpr std::size_t kVidResponseMinLen = 32; // VIN17 + addr2 + EID6 + GID6 + action1
constexpr std::size_t kRoutingRequestMinLen = 7;
constexpr std::size_t kRoutingResponseMinLen = 9;

inline uint8_t InverseVersion(uint8_t version)
{
    return static_cast<uint8_t>(~version);
}

inline uint16_t ReadU16Be(const uint8_t *p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t ReadU32Be(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void WriteU16Be(uint8_t *p, uint16_t value)
{
    p[0] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[1] = static_cast<uint8_t>(value & 0xff);
}

inline void WriteU32Be(uint8_t *p, uint32_t value)
{
    p[0] = static_cast<uint8_t>((value >> 24) & 0xff);
    p[1] = static_cast<uint8_t>((value >> 16) & 0xff);
    p[2] = static_cast<uint8_t>((value >> 8) & 0xff);
    p[3] = static_cast<uint8_t>(value & 0xff);
}

bool IsSupportedVersion(uint8_t version);
bool HeaderVersionValid(uint8_t version, uint8_t inverse);

std::vector<uint8_t> EncodeFrame(uint8_t version,
                                 uint16_t payloadType,
                                 const uint8_t *payload,
                                 uint32_t payloadLen);

bool DecodeHeader(const uint8_t *data,
                  std::size_t len,
                  uint8_t *version,
                  uint16_t *payloadType,
                  uint32_t *payloadLen);

bool ParseVidResponse(const uint8_t *payload,
                      uint32_t payloadLen,
                      uint8_t vin[17],
                      uint16_t *logicalAddress);

} // namespace proto
} // namespace doiplite
