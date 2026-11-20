#pragma once

#include "doiplite/doip.h"

// Library-internal SendUds entry used by UDS helpers.
// When optionalResponse is true and no 0x78 was seen, a receive timeout is
// treated as intentional silence: returns DOIPLITE_OK with empty response and
// does not close TCP (suppressPositiveResponse).
// After any 0x78, silence is a normal timeout (P2* expiry), not success.
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
