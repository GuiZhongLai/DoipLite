#pragma once

#include "doiplite/doip.h"

// Library-internal SendUds entry used by UDS helpers.
// When optionalResponse is true, a receive timeout returns DOIPLITE_ERR_TIMEOUT
// without closing TCP (intentional silence after suppressPositiveResponse).
// Partial-frame / send failures and DoIP NACKs keep the same behavior as the
// public DoipLiteSendUds path.
DoipLiteStatus DoipLiteSendUdsInternal(
    DoipLiteSession *session,
    uint8_t sid,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *resp,
    uint32_t respCap,
    uint32_t *respLen,
    uint32_t timeoutMs,
    bool optionalResponse);
