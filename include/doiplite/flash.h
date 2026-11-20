#ifndef DOIPLITE_FLASH_H
#define DOIPLITE_FLASH_H

#include "doiplite/doip.h"

DOIPLITE_EXTERN_C_BEGIN

typedef enum DoipLiteFlashPhase {
    DOIPLITE_FLASH_IDLE = 0,
    DOIPLITE_FLASH_SESSION,
    DOIPLITE_FLASH_SECURITY,
    DOIPLITE_FLASH_ERASE,
    DOIPLITE_FLASH_REQUEST_DOWNLOAD,
    DOIPLITE_FLASH_TRANSFER,
    DOIPLITE_FLASH_TRANSFER_EXIT,
    DOIPLITE_FLASH_RESET,
    DOIPLITE_FLASH_DONE,
    DOIPLITE_FLASH_FAILED
} DoipLiteFlashPhase;

/** 刷写作业参数全部由调用方填充；本结构只是占位，后续按规范扩展。 */
typedef struct DoipLiteFlashJob {
    const char *packagePath;
} DoipLiteFlashJob;

typedef struct DoipLiteFlashProgress {
    DoipLiteFlashPhase phase;
    uint32_t percent;
    DoipLiteStatus err;
} DoipLiteFlashProgress;

/**
 * 安全访问种子到密钥的回调。算法与密钥不得放入本仓库，由调用方提供实现。
 */
typedef DoipLiteStatus (*DoipLiteSeedKeyFn)(
    uint8_t level,
    const uint8_t *seed,
    uint32_t seedLen,
    uint8_t *key,
    uint32_t keyCap,
    uint32_t *keyLen,
    void *user);

DOIPLITE_API DoipLiteStatus DoipLiteSetSeedKeyCallback(
    DoipLiteSession *session,
    DoipLiteSeedKeyFn fn,
    void *user);

DOIPLITE_API DoipLiteStatus DoipLiteFlashStart(
    DoipLiteSession *session,
    const DoipLiteFlashJob *job);

DOIPLITE_API DoipLiteStatus DoipLiteFlashPoll(
    DoipLiteSession *session,
    DoipLiteFlashProgress *progress);

DOIPLITE_EXTERN_C_END

#endif
