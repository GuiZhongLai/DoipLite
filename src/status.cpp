#include "doiplite/error.h"

const char *DoipLiteStatusString(DoipLiteStatus status)
{
    switch (status)
    {
    case DOIPLITE_OK:
        return "ok";
    case DOIPLITE_ERR_TIMEOUT:
        return "timeout";
    case DOIPLITE_ERR_NETWORK:
        return "network";
    case DOIPLITE_ERR_DOIP_NACK:
        return "doip_nack";
    case DOIPLITE_ERR_UDS_NRC:
        return "uds_nrc";
    case DOIPLITE_ERR_INVALID_ARG:
        return "invalid_arg";
    case DOIPLITE_ERR_NOT_IMPLEMENTED:
        return "not_implemented";
    default:
        return "unknown";
    }
}
