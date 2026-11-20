#include "request_download.h"

#include "doiplite/uds.h"
#include "uds_result.h"

#include <vector>

namespace doiplite {
namespace uds {
namespace {

bool EncodeBeField(uint64_t value, uint8_t nbytes, std::vector<uint8_t> *out)
{
    if (!out || nbytes < 1 || nbytes > 8)
    {
        return false;
    }
    // Reject values that do not fit in the ALFID-declared width.
    if (nbytes < 8)
    {
        const uint64_t max = (uint64_t{1} << (nbytes * 8)) - 1u;
        if (value > max)
        {
            return false;
        }
    }
    for (uint8_t i = 0; i < nbytes; ++i)
    {
        const uint8_t shift = static_cast<uint8_t>((nbytes - 1u - i) * 8u);
        out->push_back(static_cast<uint8_t>((value >> shift) & 0xff));
    }
    return true;
}

bool DecodeBeField(const uint8_t *p, uint8_t nbytes, uint64_t *value)
{
    if (!p || !value || nbytes < 1 || nbytes > 8)
    {
        return false;
    }
    uint64_t v = 0;
    for (uint8_t i = 0; i < nbytes; ++i)
    {
        v = (v << 8) | p[i];
    }
    *value = v;
    return true;
}

} // namespace

DoipLiteStatus RequestDownload::execute(
    DoipLiteSession *session,
    const Args &args,
    Result *result,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    if (len)
    {
        *len = 0;
    }
    if (result)
    {
        result->lengthFormatId = 0;
        result->maxNumberOfBlockLength = 0;
    }
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (cap > 0 && !out)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    // ALFID: high nibble = sizeOfMemorySize, low nibble = sizeOfMemoryAddress.
    const uint8_t addrBytes = static_cast<uint8_t>(args.addressAndLengthFormatId & 0x0f);
    const uint8_t sizeBytes =
        static_cast<uint8_t>((args.addressAndLengthFormatId >> 4) & 0x0f);
    if (addrBytes < 1 || addrBytes > 8 || sizeBytes < 1 || sizeBytes > 8)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    std::vector<uint8_t> req;
    req.reserve(2u + addrBytes + sizeBytes);
    req.push_back(args.dataFormatId);
    req.push_back(args.addressAndLengthFormatId);
    if (!EncodeBeField(args.memoryAddress, addrBytes, &req) ||
        !EncodeBeField(args.memorySize, sizeBytes, &req))
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }

    uint32_t respCap = cap < 64u ? 64u : cap;
    std::vector<uint8_t> resp(respCap);
    uint32_t respLen = 0;
    const DoipLiteStatus st = DoipLiteSendUds(
        session,
        0x34,
        req.data(),
        static_cast<uint32_t>(req.size()),
        resp.data(),
        respCap,
        &respLen,
        timeoutMs);

    if (st != DOIPLITE_OK)
    {
        CopyNrcIfNeeded(st, resp.data(), respLen, out, cap, len);
        return st;
    }

    // Positive: 0x74 | lengthFormatIdentifier | maxNumberOfBlockLength
    if (respLen < 2 || resp[0] != 0x74)
    {
        return DOIPLITE_ERR_NETWORK;
    }
    const uint8_t lfid = resp[1];
    const uint8_t maxLenBytes = static_cast<uint8_t>((lfid >> 4) & 0x0f);
    if (maxLenBytes < 1 || maxLenBytes > 4 ||
        respLen < static_cast<uint32_t>(2u + maxLenBytes))
    {
        return DOIPLITE_ERR_NETWORK;
    }
    uint64_t maxBlock = 0;
    if (!DecodeBeField(resp.data() + 2, maxLenBytes, &maxBlock) || maxBlock == 0 ||
        maxBlock > 0xffffffffu)
    {
        return DOIPLITE_ERR_NETWORK;
    }
    if (result)
    {
        result->lengthFormatId = lfid;
        result->maxNumberOfBlockLength = static_cast<uint32_t>(maxBlock);
    }

    return CopyPositive(resp.data(), respLen, out, cap, len);
}

} // namespace uds
} // namespace doiplite

DoipLiteStatus UdsRequestDownload(
    DoipLiteSession *session,
    uint8_t dataFormatId,
    uint8_t addressAndLengthFormatId,
    uint64_t memoryAddress,
    uint64_t memorySize,
    uint32_t *outMaxBlockLength,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs)
{
    doiplite::uds::RequestDownload::Args args;
    args.dataFormatId = dataFormatId;
    args.addressAndLengthFormatId = addressAndLengthFormatId;
    args.memoryAddress = memoryAddress;
    args.memorySize = memorySize;
    doiplite::uds::RequestDownload::Result result;
    const DoipLiteStatus st = doiplite::uds::RequestDownload::execute(
        session, args, &result, out, cap, len, timeoutMs);
    if (st == DOIPLITE_OK && outMaxBlockLength)
    {
        *outMaxBlockLength = result.maxNumberOfBlockLength;
    }
    else if (outMaxBlockLength)
    {
        *outMaxBlockLength = 0;
    }
    return st;
}
