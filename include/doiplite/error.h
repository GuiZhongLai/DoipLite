#ifndef DOIPLITE_ERROR_H
#define DOIPLITE_ERROR_H

#include "doiplite/export.h"
#include <stdint.h>

DOIPLITE_EXTERN_C_BEGIN

typedef int32_t DoipLiteStatus;

enum {
    DOIPLITE_OK = 0,
    DOIPLITE_ERR_TIMEOUT = 1,
    DOIPLITE_ERR_NETWORK = 2,
    DOIPLITE_ERR_DOIP_NACK = 3,
    DOIPLITE_ERR_UDS_NRC = 4,
    DOIPLITE_ERR_INVALID_ARG = 5,
    DOIPLITE_ERR_NOT_IMPLEMENTED = 6
};

DOIPLITE_API const char *DoipLiteStatusString(DoipLiteStatus status);

DOIPLITE_EXTERN_C_END

#endif
