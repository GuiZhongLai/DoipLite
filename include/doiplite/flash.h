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

/**
 * 刷写作业参数：全部由调用方填充，库不硬编码现场/OEM 值。
 * 未提供的可选步骤会被跳过；下载/传输所需字段缺失时 Start 返回 INVALID_ARG。
 * 本 PR 不实现 0x27 安全访问：haveSecurity 即使置位也会在 SECURITY 阶段以
 * NOT_IMPLEMENTED 失败（或保持 0 以跳过该阶段）。seed-key 回调可注册但未接线。
 */
typedef struct DoipLiteFlashJob {
    /** 可选：刷写前会话切换（0x10）。haveFlashSessionType=0 则跳过。 */
    int haveFlashSessionType;
    uint8_t flashSessionType;

    /**
     * 可选：安全访问（0x27）。本 PR 未实现。
     * haveSecurity=0：跳过 SECURITY 阶段；非 0：该阶段返回 NOT_IMPLEMENTED。
     */
    int haveSecurity;
    uint8_t securityLevel;

    /** 可选：擦除/预下载例程（0x31）。haveEraseRoutine=0 则跳过。 */
    int haveEraseRoutine;
    uint8_t eraseRoutineSubFunction;
    uint16_t eraseRoutineId;
    const uint8_t *eraseRoutineOption;
    uint32_t eraseRoutineOptionLen;

    /** 下载（0x34）参数；与 payload 一起为传包所必需。 */
    uint8_t dataFormatId;
    uint8_t addressAndLengthFormatId;
    uint64_t memoryAddress;
    /**
     * 目标内存大小。为 0 时使用实际 payload 字节数（文件或 data 缓冲）。
     * 若非 0 且与 payload 长度不一致，Start 返回 INVALID_ARG。
     */
    uint64_t memorySize;

    /**
     * 传输载荷来源（二选一或组合）：
     * - packagePath：从文件读取原始字节（不是 OEM 包解析）；
     * - data / dataLen：内存缓冲。
     * 若两者都提供，优先使用 data 缓冲。
     */
    const char *packagePath;
    const uint8_t *data;
    uint32_t dataLen;

    /**
     * 调用方对单块数据长度的上限（不含 SID/计数器）。
     * 0 表示仅使用 0x74 返回的 maxNumberOfBlockLength-2。
     * 实际块数据长度 = min(ECU 允许数据长度, maxBlockDataCap)。
     */
    uint32_t maxBlockDataCap;

    /** 可选：0x37 参数记录（原始字节）。 */
    const uint8_t *transferExitParam;
    uint32_t transferExitParamLen;

    /** 可选：结束后 ECUReset（0x11）。haveEcuReset=0 则跳过。 */
    int haveEcuReset;
    uint8_t resetType;

    /** 每步首应答窗口；0 表示使用会话当前 P2。 */
    uint32_t timeoutMs;
} DoipLiteFlashJob;

typedef struct DoipLiteFlashProgress {
    DoipLiteFlashPhase phase;
    uint32_t percent;
    DoipLiteStatus err;
} DoipLiteFlashProgress;

/**
 * 安全访问种子到密钥的回调。算法与密钥不得放入本仓库，由调用方提供实现。
 * 本 PR 仅保存回调指针，刷写状态机尚未调用（0x27 未实现）。
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

/**
 * 启动刷写作业：校验并复制作业参数，加载 payload，进入状态机。
 * 不直接操作套接字；只通过 UDS 服务推进。
 */
DOIPLITE_API DoipLiteStatus DoipLiteFlashStart(
    DoipLiteSession *session,
    const DoipLiteFlashJob *job);

/**
 * 推进刷写状态机一步（会话/擦除/下载/单块传输/退出/复位）。
 * 重复调用直到 phase 为 DONE 或 FAILED。
 * TRANSFER 阶段每次 Poll 发送一块 0x36。
 */
DOIPLITE_API DoipLiteStatus DoipLiteFlashPoll(
    DoipLiteSession *session,
    DoipLiteFlashProgress *progress);

DOIPLITE_EXTERN_C_END

#endif
