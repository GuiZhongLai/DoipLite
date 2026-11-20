#include "doiplite/flash.h"

DoipLiteStatus DoipLiteSetSeedKeyCallback(
    DoipLiteSession *session,
    DoipLiteSeedKeyFn fn,
    void *user)
{
    (void)fn;
    (void)user;
    if (!session)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}

DoipLiteStatus DoipLiteFlashStart(
    DoipLiteSession *session,
    const DoipLiteFlashJob *job)
{
    if (!session || !job)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}

DoipLiteStatus DoipLiteFlashPoll(
    DoipLiteSession *session,
    DoipLiteFlashProgress *progress)
{
    if (!session || !progress)
    {
        return DOIPLITE_ERR_INVALID_ARG;
    }
    progress->phase = DOIPLITE_FLASH_IDLE;
    progress->percent = 0;
    progress->err = DOIPLITE_ERR_NOT_IMPLEMENTED;
    return DOIPLITE_ERR_NOT_IMPLEMENTED;
}
