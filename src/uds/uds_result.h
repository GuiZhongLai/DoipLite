#pragma once

#include "doiplite/error.h"

#include <cstring>
#include <cstdint>

namespace doiplite {
namespace uds {

// Shared result copy for UDS helpers: on NRC, copy the 0x7F payload out;
// on success paths callers validate then use CopyPositive.
inline void CopyNrcIfNeeded(
    DoipLiteStatus st,
    const uint8_t *resp,
    uint32_t respLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len)
{
    if (st != DOIPLITE_ERR_UDS_NRC)
    {
        return;
    }
    if (len)
    {
        *len = respLen;
    }
    const uint32_t ncopy = respLen < cap ? respLen : cap;
    if (ncopy > 0 && out)
    {
        std::memcpy(out, resp, ncopy);
    }
}

inline DoipLiteStatus CopyPositive(
    const uint8_t *resp,
    uint32_t respLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len)
{
    if (len)
    {
        *len = respLen;
    }
    if (respLen > cap)
    {
        if (cap > 0 && out)
        {
            std::memcpy(out, resp, cap);
        }
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (respLen > 0 && out)
    {
        std::memcpy(out, resp, respLen);
    }
    return DOIPLITE_OK;
}

} // namespace uds
} // namespace doiplite
