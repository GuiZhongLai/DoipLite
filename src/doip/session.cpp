#include "doiplite/doip.h"

#include "net/net_internal.h"

#include <cstdlib>
#include <cstring>

struct DoipLiteSession {
    char ip[64];
    uint16_t port;
};

DoipLiteStatus DoipLiteDiscover(
    uint32_t timeoutMs,
    DoipLiteVehicle *out,
    uint32_t cap,
    uint32_t *count)
{
    (void)timeoutMs;
    if ((cap > 0 && !out) || !count)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    *count = 0;
    for (uint32_t i = 0; i < cap; ++i)
    {
        DoipLiteClearVehicle(&out[i]);
    }
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}

DoipLiteSession *DoipLiteOpen(const char *ip, uint16_t port)
{
    if (!ip || ip[0] == '\0')
    {
        return nullptr;
    }
    DoipLiteSession *session =
        static_cast<DoipLiteSession *>(std::calloc(1, sizeof(DoipLiteSession)));
    if (!session)
    {
        return nullptr;
    }
    std::strncpy(session->ip, ip, sizeof(session->ip) - 1);
    session->port = port == 0 ? DOIPLITE_DEFAULT_TCP_PORT : port;
    return session;
}

DoipLiteStatus DoipLiteRoutingActivation(
    DoipLiteSession *session,
    uint16_t testerAddr,
    uint16_t activationType,
    const uint8_t *oemData,
    uint32_t oemLen)
{
    (void)testerAddr;
    (void)activationType;
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (oemLen > 0 && !oemData)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}

DoipLiteStatus DoipLiteSendUds(
    DoipLiteSession *session,
    uint8_t sid,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *resp,
    uint32_t respCap,
    uint32_t *respLen,
    uint32_t timeoutMs)
{
    (void)sid;
    (void)timeoutMs;
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (dataLen > 0 && !data)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (respCap > 0 && !resp)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    if (respLen)
    {
        *respLen = 0;
    }
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}

void DoipLiteClose(DoipLiteSession *session)
{
    std::free(session);
}
