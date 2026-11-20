#ifndef DOIPLITE_UDS_H
#define DOIPLITE_UDS_H

#include "doiplite/doip.h"

DOIPLITE_EXTERN_C_BEGIN

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x22 ReadDataByIdentifier。
 * DID 只来自调用参数，库不提供默认 DID。
 * 成功时 out 写入 0x62 与 DID 之后的数据记录，*len 为该记录长度。
 * 收到 UDS 否定应答时返回 DOIPLITE_ERR_UDS_NRC，out 写入完整 0x7F 应答（含 NRC）。
 * 未激活会话由 SendUds 返回 DOIPLITE_ERR_NETWORK。
 */
DOIPLITE_API DoipLiteStatus UdsReadDataByIdentifier(
    DoipLiteSession *session,
    uint16_t did,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x10 DiagnosticSessionControl。
 * 会话类型/子功能只来自调用参数，库不提供默认会话（也不默认编程会话）。
 * 成功时 out 写入完整肯定应答（0x50 + 子功能，以及若存在的 P2/P2*），*len 为该应答长度。
 * 收到 UDS 否定应答时返回 DOIPLITE_ERR_UDS_NRC，out 写入完整 0x7F 应答（含 NRC）。
 * 未激活会话由 SendUds 返回 DOIPLITE_ERR_NETWORK。
 *
 * 子功能 bit7（suppressPosRspMsgIndicationBit）置位时：在 P2 窗口内等待可能的 NRC；
 * 若对端保持静默则视为成功（*len 为 0），且不因这次静默关闭 TCP。
 * 若先收到 0x78，则按 P2* 继续等待最终应答；其后静默视为超时，不是成功。
 * 若仍收到肯定应答，按成功处理；其它 NRC 路径不变。
 * 肯定应答若含 P2/P2*（0x50 后至少 4 字节），按 ISO 线格式写入会话时序：
 * P2 为毫秒，P2* 为 10ms 单位（库内转为毫秒）。
 */
DOIPLITE_API DoipLiteStatus UdsDiagnosticSessionControl(
    DoipLiteSession *session,
    uint8_t sessionType,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x3E TesterPresent（会话保活）。
 * 子功能只来自调用参数，库不提供默认子功能（也不默认 zeroSubFunction）。
 * 成功时 out 写入完整肯定应答（0x7E + 子功能），*len 为该应答长度。
 * 收到 UDS 否定应答时返回 DOIPLITE_ERR_UDS_NRC，out 写入完整 0x7F 应答（含 NRC）。
 * 未激活会话由 SendUds 返回 DOIPLITE_ERR_NETWORK。
 *
 * 子功能 bit7（suppressPosRspMsgIndicationBit）置位时：在 P2 窗口内等待可能的 NRC；
 * 若对端保持静默则视为成功（*len 为 0），且不因这次静默关闭 TCP。
 * 若先收到 0x78，则按 P2* 继续等待最终应答；其后静默视为超时，不是成功。
 * 若仍收到肯定应答，按成功处理；其它 NRC 路径不变。
 */
DOIPLITE_API DoipLiteStatus UdsTesterPresent(
    DoipLiteSession *session,
    uint8_t subFunction,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x31 RoutineControl。
 * 子功能、routine ID、可选 routineControlOptionRecord 均只来自调用参数。
 * 成功时 out 写入完整肯定应答（0x71 + 子功能 + routineId + 可选 status record）。
 * 收到 UDS 否定应答时返回 DOIPLITE_ERR_UDS_NRC，out 写入完整 0x7F 应答。
 */
DOIPLITE_API DoipLiteStatus UdsRoutineControl(
    DoipLiteSession *session,
    uint8_t subFunction,
    uint16_t routineId,
    const uint8_t *optionRecord,
    uint32_t optionLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x34 RequestDownload。
 * dataFormatId、ALFID、memoryAddress、memorySize 均只来自调用参数。
 * ALFID：高半字节为 memorySize 字节数，低半字节为 memoryAddress 字节数（1..8）。
 * 成功时解析 0x74，写出 lengthFormatIdentifier / maxNumberOfBlockLength 到
 * *outMaxBlockLength（可为 NULL）；out 写入完整肯定应答。
 * ISO：maxNumberOfBlockLength 含 SID 与 blockSequenceCounter（即 0x36 整帧上限）。
 */
DOIPLITE_API DoipLiteStatus UdsRequestDownload(
    DoipLiteSession *session,
    uint8_t dataFormatId,
    uint8_t addressAndLengthFormatId,
    uint64_t memoryAddress,
    uint64_t memorySize,
    uint32_t *outMaxBlockLength,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x36 TransferData。
 * blockSequenceCounter 与原始块数据只来自调用参数；不解析 OEM 包格式。
 * 成功时 out 写入完整肯定应答（0x76 + blockSequenceCounter + 可选记录）。
 */
DOIPLITE_API DoipLiteStatus UdsTransferData(
    DoipLiteSession *session,
    uint8_t blockSequenceCounter,
    const uint8_t *data,
    uint32_t dataLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x37 RequestTransferExit。
 * 可选 transferRequestParameterRecord 为调用方原始字节。
 * 成功时 out 写入完整肯定应答（0x77 + 可选记录）。
 */
DOIPLITE_API DoipLiteStatus UdsRequestTransferExit(
    DoipLiteSession *session,
    const uint8_t *parameterRecord,
    uint32_t parameterLen,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

/**
 * 通过 DoipLiteSendUds 发送 UDS 0x11 ECUReset。
 * resetType 只来自调用参数，库不提供现场默认复位类型。
 * 成功时 out 写入完整肯定应答（0x51 + resetType）。
 * 子功能 bit7（suppressPosRspMsgIndicationBit）语义同 DiagnosticSessionControl。
 */
DOIPLITE_API DoipLiteStatus UdsEcuReset(
    DoipLiteSession *session,
    uint8_t resetType,
    uint8_t *out,
    uint32_t cap,
    uint32_t *len,
    uint32_t timeoutMs);

DOIPLITE_EXTERN_C_END

#endif
