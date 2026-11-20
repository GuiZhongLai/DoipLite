#include "doiplite/doiplite.h"
#include "loopback_entity.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#define DOIPLITE_POPEN _popen
#define DOIPLITE_PCLOSE _pclose
#else
#include <sys/wait.h>
#define DOIPLITE_POPEN popen
#define DOIPLITE_PCLOSE pclose
#endif

static void expect(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

static int cmd_status(int raw)
{
    if (raw < 0)
    {
        return raw;
    }
#ifdef _WIN32
    return raw;
#else
    if (WIFEXITED(raw))
    {
        return WEXITSTATUS(raw);
    }
    return raw;
#endif
}

static void expect_fresh_activation(DoipLiteSession *session, const char *prefix)
{
    char msg[192];
    std::snprintf(msg, sizeof(msg), "%s: same-session retry succeeds", prefix);
    expect(DoipLiteRoutingActivationEx(session, 0x0E00, 0x00, nullptr, 0, 1000) == DOIPLITE_OK,
           msg);
    std::snprintf(msg, sizeof(msg), "%s: retry reports fresh success code", prefix);
    expect(DoipLiteLastDoipCode(session) == 0x10, msg);
    std::snprintf(msg, sizeof(msg), "%s: retry uses fresh entity address", prefix);
    expect(DoipLiteEntityAddress(session) == 0x1234, msg);

    const uint8_t testerPresent[1] = {0x00};
    uint8_t resp[8];
    uint32_t len = 0;
    std::snprintf(msg, sizeof(msg), "%s: sendUds after retry is aligned", prefix);
    expect(DoipLiteSendUds(session, 0x3E, testerPresent, 1, resp, sizeof(resp), &len, 1000) ==
               DOIPLITE_OK,
           msg);
    std::snprintf(msg, sizeof(msg), "%s: uds response after retry", prefix);
    expect(len == 2 && resp[0] == 0x7E && resp[1] == 0x00, msg);
}

#ifdef DOIPLITE_CLI_PATH
static bool run_cli(const std::string &args, int *exitCode, std::string *output)
{
    const std::string cmd = std::string("\"") + DOIPLITE_CLI_PATH + "\" " + args + " 2>&1";
    FILE *fp = DOIPLITE_POPEN(cmd.c_str(), "r");
    if (!fp)
    {
        return false;
    }
    std::string text;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp) != nullptr)
    {
        text += buf;
    }
    const int raw = DOIPLITE_PCLOSE(fp);
    if (exitCode)
    {
        *exitCode = cmd_status(raw);
    }
    if (output)
    {
        *output = text;
    }
    return true;
}
#endif

int main()
{
    expect(DoipLiteStatusString(DOIPLITE_OK) != nullptr, "status string");
    expect(DoipLiteOpen(nullptr, 0) == nullptr, "open null ip");
    expect(DoipLiteOpen("", 0) == nullptr, "open empty ip");
    expect(DoipLiteOpen("not-an-ipv4", 13400) == nullptr, "open bad ip");
    DoipLiteClose(nullptr);

    uint32_t count = 99;
    expect(DoipLiteDiscover(100, nullptr, 0, nullptr) == DOIPLITE_ERR_INVALID_ARG,
           "discover null count");
    expect(DoipLiteDiscoverEx(100, "127.0.0.1", 13400, nullptr, 4, &count) ==
               DOIPLITE_ERR_INVALID_ARG,
           "discover cap without out");
    expect(DoipLiteDiscoverEx(100, "not-an-ipv4", 0, nullptr, 0, &count) ==
               DOIPLITE_ERR_INVALID_ARG,
           "discover bad dest");

    LoopbackEntity::Options timeoutOpts;
    timeoutOpts.replyVid = false;
    timeoutOpts.replyRouting = false;
    LoopbackEntity noReply(timeoutOpts);
    expect(noReply.start(), "start silent entity");
    count = 99;
    const DoipLiteStatus idle = DoipLiteDiscoverEx(
        200, "127.0.0.1", noReply.port(), nullptr, 0, &count);
    expect(idle == DOIPLITE_ERR_TIMEOUT, "discover timeout with no vehicles");
    expect(count == 0, "discover count is zero on timeout");
    noReply.stop();

    LoopbackEntity entity;
    expect(entity.start(), "start loopback entity");
    expect(entity.port() != 0, "ephemeral port assigned");

    DoipLiteVehicle vehicles[4];
    count = 0;
    const DoipLiteStatus found = DoipLiteDiscoverEx(
        1000, entity.ip(), entity.port(), vehicles, 4, &count);
    expect(found == DOIPLITE_OK, "discover finds loopback entity");
    expect(count == 1, "one vehicle");
    expect(std::strcmp(vehicles[0].ip, "127.0.0.1") == 0, "vehicle ip");
    expect(vehicles[0].tcpPort == entity.port(), "vehicle tcp port");
    expect(vehicles[0].logicalAddress == 0x1234, "vehicle logical address");
    expect(std::memcmp(vehicles[0].vin, "DOIPLITEEXAMPLE01", 17) == 0, "example vin");

    DoipLiteSession *session = DoipLiteOpen(entity.ip(), entity.port());
    expect(session != nullptr, "open loopback");
    expect(DoipLiteLastDoipCode(session) == 0, "no nack yet");
    expect(DoipLiteEntityAddress(session) == 0, "no entity yet");
    expect(DoipLiteRoutingActivation(session, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "routing activation success");
    expect(DoipLiteLastDoipCode(session) == 0x10, "success response code");
    expect(DoipLiteEntityAddress(session) == 0x1234, "entity address from response");

    expect(DoipLiteRoutingActivation(nullptr, 0, 0, nullptr, 0) == DOIPLITE_ERR_INVALID_ARG,
           "activate null session");
    expect(DoipLiteRoutingActivation(session, 0x0E00, 0, nullptr, 4) == DOIPLITE_ERR_INVALID_ARG,
           "activate oem without data");

    expect(DoipLiteSendUds(nullptr, 0x3E, nullptr, 0, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "sendUds null session");
    expect(DoipLiteSendUds(session, 0x3E, nullptr, 2, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "sendUds dataLen without data");
    uint8_t tinyReq[1] = {0x00};
    expect(DoipLiteSendUds(session, 0x3E, tinyReq, 1, nullptr, 8, nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "sendUds respCap without buffer");

    DoipLiteSession *notActivated = DoipLiteOpen(entity.ip(), entity.port());
    expect(notActivated != nullptr, "open without activate");
    expect(DoipLiteSendUds(notActivated, 0x3E, nullptr, 0, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NETWORK,
           "sendUds without routing activation is network");
    DoipLiteClose(notActivated);

    const uint8_t testerPresent[1] = {0x00};
    uint8_t udsResp[16];
    uint32_t udsLen = 0;
    expect(DoipLiteSendUds(session,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           1000) == DOIPLITE_OK,
           "sendUds positive response");
    expect(udsLen == 2, "positive uds length");
    expect(udsResp[0] == 0x7E && udsResp[1] == 0x00, "tester present positive response");

    expect(UdsReadDataByIdentifier(nullptr, 0x0001, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "read did null session");
    uint8_t didBuf[32];
    expect(UdsReadDataByIdentifier(session, 0x0001, nullptr, sizeof(didBuf), nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "read did cap without buffer");
    uint32_t didLen = 99;
    expect(UdsReadDataByIdentifier(session, 0x0001, didBuf, sizeof(didBuf), &didLen, 1000) ==
               DOIPLITE_OK,
           "read did echo on default entity");
    expect(didLen == 0, "default entity 0x22 echo has empty data record");

    DoipLiteFlashJob job{};
    job.packagePath = "example.pkg";
    expect(DoipLiteFlashStart(session, &job) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "flash start stub");
    DoipLiteFlashProgress progress{};
    expect(DoipLiteFlashPoll(session, &progress) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "flash poll stub");
    expect(progress.phase == DOIPLITE_FLASH_IDLE, "flash idle");
    DoipLiteClose(session);

    LoopbackEntity::Options nackOpts;
    nackOpts.routingCode = 0x06;
    LoopbackEntity nackEntity(nackOpts);
    expect(nackEntity.start(), "start nack entity");
    DoipLiteSession *nackSession = DoipLiteOpen(nackEntity.ip(), nackEntity.port());
    expect(nackSession != nullptr, "open nack entity");
    expect(DoipLiteRoutingActivationEx(nackSession, 0x0E00, 0x00, nullptr, 0, 1000) ==
               DOIPLITE_ERR_DOIP_NACK,
           "routing nack");
    expect(DoipLiteLastDoipCode(nackSession) == 0x06, "nack code surfaced");
    DoipLiteClose(nackSession);
    nackEntity.stop();

    LoopbackEntity::Options hangOpts;
    hangOpts.replyRouting = false;
    LoopbackEntity hangEntity(hangOpts);
    expect(hangEntity.start(), "start hang entity");
    DoipLiteSession *hangSession = DoipLiteOpen(hangEntity.ip(), hangEntity.port());
    expect(hangSession != nullptr, "open hang entity");
    expect(DoipLiteRoutingActivationEx(hangSession, 0x0E00, 0x00, nullptr, 0, 250) ==
               DOIPLITE_ERR_TIMEOUT,
           "routing timeout");
    expect(DoipLiteLastDoipCode(hangSession) == 0, "timeout does not invent a doip code");
    expect(DoipLiteEntityAddress(hangSession) == 0, "timeout does not set entity address");
    expect(DoipLiteSendUds(hangSession, 0x3E, nullptr, 0, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NETWORK,
           "sendUds after activation timeout is not activated");
    DoipLiteClose(hangSession);
    hangEntity.stop();

    // Late full 0x0006: a same-session retry must not accept that stale frame.
    // Timing: client gives up at 200ms, entity emits the NACK at 500ms, the
    // test waits until that write has happened, then retries.
    LoopbackEntity::Options lateOpts;
    lateOpts.routingDelayMs = 500;
    lateOpts.routingLateNack = true;
    lateOpts.routingLateCode = 0x06;
    LoopbackEntity lateEntity(lateOpts);
    expect(lateEntity.start(), "start late routing entity");
    DoipLiteSession *lateSession = DoipLiteOpen(lateEntity.ip(), lateEntity.port());
    expect(lateSession != nullptr, "open late routing entity");
    expect(DoipLiteRoutingActivationEx(lateSession, 0x0E00, 0x00, nullptr, 0, 200) ==
               DOIPLITE_ERR_TIMEOUT,
           "delayed routing response times out");
    expect(DoipLiteLastDoipCode(lateSession) == 0, "delayed response is not a doip code yet");
    expect(DoipLiteEntityAddress(lateSession) == 0, "delayed response sets no entity address");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    expect_fresh_activation(lateSession, "late routing response");
    DoipLiteClose(lateSession);
    lateEntity.stop();

    // Header now, payload after the client has already timed out.
    LoopbackEntity::Options partialOpts;
    partialOpts.routingPayloadDelayMs = 500;
    LoopbackEntity partialEntity(partialOpts);
    expect(partialEntity.start(), "start partial routing entity");
    DoipLiteSession *partialSession =
        DoipLiteOpen(partialEntity.ip(), partialEntity.port());
    expect(partialSession != nullptr, "open partial routing entity");
    expect(DoipLiteRoutingActivationEx(partialSession, 0x0E00, 0x00, nullptr, 0, 200) ==
               DOIPLITE_ERR_TIMEOUT,
           "partial routing response times out");
    expect(DoipLiteLastDoipCode(partialSession) == 0, "partial response has no doip code");
    expect(DoipLiteEntityAddress(partialSession) == 0, "partial response sets no entity");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    expect_fresh_activation(partialSession, "partial routing response");
    DoipLiteClose(partialSession);
    partialEntity.stop();

    // Peer drops the TCP connection after reading the activation request.
    LoopbackEntity::Options dropOpts;
    dropOpts.routingCloseFirst = true;
    LoopbackEntity dropEntity(dropOpts);
    expect(dropEntity.start(), "start disconnect routing entity");
    DoipLiteSession *dropSession = DoipLiteOpen(dropEntity.ip(), dropEntity.port());
    expect(dropSession != nullptr, "open disconnect routing entity");
    expect(DoipLiteRoutingActivationEx(dropSession, 0x0E00, 0x00, nullptr, 0, 1000) ==
               DOIPLITE_ERR_NETWORK,
           "routing disconnect is network");
    expect(DoipLiteLastDoipCode(dropSession) == 0, "disconnect does not invent a doip code");
    expect(DoipLiteEntityAddress(dropSession) == 0, "disconnect sets no entity address");
    expect(DoipLiteSendUds(dropSession, 0x3E, nullptr, 0, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NETWORK,
           "sendUds after activation disconnect is not activated");
    expect_fresh_activation(dropSession, "routing disconnect");
    DoipLiteClose(dropSession);
    dropEntity.stop();

    LoopbackEntity::Options ackOpts;
    ackOpts.sendDiagnosticAck = true;
    LoopbackEntity ackEntity(ackOpts);
    expect(ackEntity.start(), "start ack entity");
    DoipLiteSession *ackSession = DoipLiteOpen(ackEntity.ip(), ackEntity.port());
    expect(ackSession != nullptr, "open ack entity");
    expect(DoipLiteRoutingActivation(ackSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate ack entity");
    udsLen = 0;
    expect(DoipLiteSendUds(ackSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           1000) == DOIPLITE_OK,
           "sendUds after 0x8002 ACK");
    expect(udsLen == 2 && udsResp[0] == 0x7E && udsResp[1] == 0x00,
           "ack path still returns UDS response");
    DoipLiteClose(ackSession);
    ackEntity.stop();

    LoopbackEntity::Options diagNackOpts;
    diagNackOpts.diagnosticMode = LoopbackEntity::DiagnosticMode::Nack;
    diagNackOpts.diagnosticNackCode = 0x03;
    LoopbackEntity diagNackEntity(diagNackOpts);
    expect(diagNackEntity.start(), "start diagnostic nack entity");
    DoipLiteSession *diagNackSession =
        DoipLiteOpen(diagNackEntity.ip(), diagNackEntity.port());
    expect(diagNackSession != nullptr, "open diagnostic nack entity");
    expect(DoipLiteRoutingActivation(diagNackSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate diagnostic nack entity");
    expect(DoipLiteSendUds(diagNackSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           1000) == DOIPLITE_ERR_DOIP_NACK,
           "sendUds diagnostic nack");
    expect(DoipLiteLastDoipCode(diagNackSession) == 0x03, "diagnostic nack code surfaced");
    DoipLiteClose(diagNackSession);
    diagNackEntity.stop();

    LoopbackEntity::Options nrcOpts;
    nrcOpts.udsNrc = 0x11;
    LoopbackEntity nrcEntity(nrcOpts);
    expect(nrcEntity.start(), "start uds nrc entity");
    DoipLiteSession *nrcSession = DoipLiteOpen(nrcEntity.ip(), nrcEntity.port());
    expect(nrcSession != nullptr, "open nrc entity");
    expect(DoipLiteRoutingActivation(nrcSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate nrc entity");
    udsLen = 0;
    expect(DoipLiteSendUds(nrcSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           1000) == DOIPLITE_ERR_UDS_NRC,
           "sendUds maps 0x7F to UDS NRC");
    expect(udsLen == 3, "nrc response length");
    expect(udsResp[0] == 0x7F && udsResp[1] == 0x3E && udsResp[2] == 0x11,
           "uds negative response bytes");
    DoipLiteClose(nrcSession);
    nrcEntity.stop();

    LoopbackEntity::Options silentDiagOpts;
    silentDiagOpts.diagnosticMode = LoopbackEntity::DiagnosticMode::Silent;
    LoopbackEntity silentDiag(silentDiagOpts);
    expect(silentDiag.start(), "start silent diagnostic entity");
    DoipLiteSession *silentSession = DoipLiteOpen(silentDiag.ip(), silentDiag.port());
    expect(silentSession != nullptr, "open silent diagnostic entity");
    expect(DoipLiteRoutingActivation(silentSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate silent diagnostic entity");
    expect(DoipLiteSendUds(silentSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           250) == DOIPLITE_ERR_TIMEOUT,
           "sendUds timeout");
    expect(DoipLiteSendUds(silentSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           100) == DOIPLITE_ERR_NETWORK,
           "sendUds after timeout marks session unusable");
    DoipLiteClose(silentSession);
    silentDiag.stop();

    LoopbackEntity::Options didOpts;
    didOpts.supportedDid = 0x0001;
    didOpts.didData = "SIM-1.0";
    didOpts.rejectUnknownDid = true;
    LoopbackEntity didEntity(didOpts);
    expect(didEntity.start(), "start read-did entity");
    DoipLiteSession *didSession = DoipLiteOpen(didEntity.ip(), didEntity.port());
    expect(didSession != nullptr, "open read-did entity");
    expect(DoipLiteRoutingActivation(didSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate read-did entity");
    didLen = 0;
    std::memset(didBuf, 0, sizeof(didBuf));
    expect(UdsReadDataByIdentifier(didSession, 0x0001, didBuf, sizeof(didBuf), &didLen, 1000) ==
               DOIPLITE_OK,
           "read did success");
    expect(didLen == 7, "version data length");
    expect(std::memcmp(didBuf, "SIM-1.0", 7) == 0, "version string");

    uint8_t nrcBuf[16];
    uint32_t nrcLen = 0;
    expect(UdsReadDataByIdentifier(didSession, 0x0002, nrcBuf, sizeof(nrcBuf), &nrcLen, 1000) ==
               DOIPLITE_ERR_UDS_NRC,
           "unknown did is uds nrc");
    expect(nrcLen == 3, "unknown did nrc length");
    expect(nrcBuf[0] == 0x7F && nrcBuf[1] == 0x22 && nrcBuf[2] == 0x31,
           "unknown did nrc 0x31");
    DoipLiteClose(didSession);
    didEntity.stop();

    LoopbackEntity::Options pendingOpts;
    pendingOpts.udsPendingCount = 2;
    pendingOpts.udsPendingDelayMs = 30;
    LoopbackEntity pendingEntity(pendingOpts);
    expect(pendingEntity.start(), "start pending entity");
    DoipLiteSession *pendingSession =
        DoipLiteOpen(pendingEntity.ip(), pendingEntity.port());
    expect(pendingSession != nullptr, "open pending entity");
    expect(DoipLiteRoutingActivation(pendingSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate pending entity");
    udsLen = 0;
    expect(DoipLiteSendUds(pendingSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           1000) == DOIPLITE_OK,
           "sendUds waits through 0x78 pending");
    expect(udsLen == 2 && udsResp[0] == 0x7E && udsResp[1] == 0x00,
           "final response after 0x78");
    DoipLiteClose(pendingSession);
    pendingEntity.stop();

    LoopbackEntity::Options pendingDidOpts;
    pendingDidOpts.supportedDid = 0x0001;
    pendingDidOpts.didData = "SIM-1.0";
    pendingDidOpts.udsPendingCount = 1;
    pendingDidOpts.udsPendingDelayMs = 30;
    LoopbackEntity pendingDidEntity(pendingDidOpts);
    expect(pendingDidEntity.start(), "start pending read-did entity");
    DoipLiteSession *pendingDidSession =
        DoipLiteOpen(pendingDidEntity.ip(), pendingDidEntity.port());
    expect(pendingDidSession != nullptr, "open pending read-did entity");
    expect(DoipLiteRoutingActivation(pendingDidSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate pending read-did entity");
    didLen = 0;
    std::memset(didBuf, 0, sizeof(didBuf));
    expect(UdsReadDataByIdentifier(
               pendingDidSession, 0x0001, didBuf, sizeof(didBuf), &didLen, 1000) ==
               DOIPLITE_OK,
           "read did waits through 0x78 pending");
    expect(didLen == 7 && std::memcmp(didBuf, "SIM-1.0", 7) == 0,
           "version after 0x78 pending");
    DoipLiteClose(pendingDidSession);
    pendingDidEntity.stop();

    expect(UdsDiagnosticSessionControl(nullptr, 0x01, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "session control null session");
    uint8_t sessBuf[16];

    DoipLiteSession *notActivatedSession = DoipLiteOpen(entity.ip(), entity.port());
    expect(notActivatedSession != nullptr, "open for session without activate");
    expect(UdsDiagnosticSessionControl(
               notActivatedSession, 0x01, sessBuf, sizeof(sessBuf), nullptr, 100) ==
               DOIPLITE_ERR_NETWORK,
           "session control without routing activation is network");
    DoipLiteClose(notActivatedSession);

    LoopbackEntity::Options sessOpts;
    sessOpts.supportedSession = 0x01; // ISO defaultSession example, not a plant value
    sessOpts.rejectUnknownSession = true;
    sessOpts.sessionIncludeTiming = true;
    sessOpts.sessionP2 = 0x0032;
    sessOpts.sessionP2Star = 0x01F4;
    LoopbackEntity sessEntity(sessOpts);
    expect(sessEntity.start(), "start session-control entity");
    DoipLiteSession *sessSession = DoipLiteOpen(sessEntity.ip(), sessEntity.port());
    expect(sessSession != nullptr, "open session-control entity");
    expect(UdsDiagnosticSessionControl(sessSession, 0x01, nullptr, sizeof(sessBuf), nullptr, 100) ==
               DOIPLITE_ERR_INVALID_ARG,
           "session control cap without buffer");
    expect(DoipLiteRoutingActivation(sessSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate session-control entity");
    uint32_t sessLen = 0;
    std::memset(sessBuf, 0, sizeof(sessBuf));
    expect(UdsDiagnosticSessionControl(
               sessSession, 0x01, sessBuf, sizeof(sessBuf), &sessLen, 1000) == DOIPLITE_OK,
           "session control success");
    expect(sessLen == 6, "session positive response includes P2/P2*");
    expect(sessBuf[0] == 0x50 && sessBuf[1] == 0x01, "positive 0x50 + subfunction");
    expect(sessBuf[2] == 0x00 && sessBuf[3] == 0x32, "P2 50 ms");
    expect(sessBuf[4] == 0x01 && sessBuf[5] == 0xF4, "P2* 0x01F4");
    uint32_t gotP2 = 0;
    uint32_t gotP2Star = 0;
    DoipLiteGetP2Timing(sessSession, &gotP2, &gotP2Star);
    expect(gotP2 == 50 && gotP2Star == 5000, "0x50 timing applied to session");

    uint8_t sessNrc[16];
    uint32_t sessNrcLen = 0;
    expect(UdsDiagnosticSessionControl(
               sessSession, 0x03, sessNrc, sizeof(sessNrc), &sessNrcLen, 1000) ==
               DOIPLITE_ERR_UDS_NRC,
           "unsupported session type is uds nrc");
    expect(sessNrcLen == 3, "session nrc length");
    expect(sessNrc[0] == 0x7F && sessNrc[1] == 0x10 && sessNrc[2] == 0x12,
           "session nrc 0x12");
    DoipLiteClose(sessSession);
    sessEntity.stop();

    // Default loopback echoes 0x10 as 0x50 + subfunction, without timing bytes.
    DoipLiteSession *echoSession = DoipLiteOpen(entity.ip(), entity.port());
    expect(echoSession != nullptr, "open default entity for session echo");
    expect(DoipLiteRoutingActivation(echoSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate default entity for session echo");
    sessLen = 0;
    std::memset(sessBuf, 0, sizeof(sessBuf));
    expect(UdsDiagnosticSessionControl(
               echoSession, 0x01, sessBuf, sizeof(sessBuf), &sessLen, 1000) == DOIPLITE_OK,
           "session control echo on default entity");
    expect(sessLen == 2 && sessBuf[0] == 0x50 && sessBuf[1] == 0x01,
           "default entity 0x10 echo is 0x50 + type");
    DoipLiteClose(echoSession);

    // suppressPositiveResponse (bit7): intentional silence must succeed without
    // tearing down the TCP session; a later request on the same session works.
    LoopbackEntity::Options sprOpts;
    sprOpts.supportedSession = 0x01;
    sprOpts.rejectUnknownSession = true;
    LoopbackEntity sprEntity(sprOpts);
    expect(sprEntity.start(), "start suppress-positive entity");
    DoipLiteSession *sprSession = DoipLiteOpen(sprEntity.ip(), sprEntity.port());
    expect(sprSession != nullptr, "open suppress-positive entity");
    expect(DoipLiteRoutingActivation(sprSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate suppress-positive entity");
    sessLen = 99;
    std::memset(sessBuf, 0xAA, sizeof(sessBuf));
    expect(UdsDiagnosticSessionControl(
               sprSession, 0x81, sessBuf, sizeof(sessBuf), &sessLen, 400) == DOIPLITE_OK,
           "suppress-positive silence is success");
    expect(sessLen == 0, "suppress-positive success has empty response");
    sessLen = 0;
    std::memset(sessBuf, 0, sizeof(sessBuf));
    expect(UdsDiagnosticSessionControl(
               sprSession, 0x01, sessBuf, sizeof(sessBuf), &sessLen, 1000) == DOIPLITE_OK,
           "session remains usable after suppress-positive silence");
    expect(sessLen == 2 && sessBuf[0] == 0x50 && sessBuf[1] == 0x01,
           "post-suppress positive response");

    // suppress + unsupported type still surfaces NRC and keeps the session.
    sessNrcLen = 0;
    std::memset(sessNrc, 0, sizeof(sessNrc));
    expect(UdsDiagnosticSessionControl(
               sprSession, 0x83, sessNrc, sizeof(sessNrc), &sessNrcLen, 1000) ==
               DOIPLITE_ERR_UDS_NRC,
           "suppress-positive still returns nrc");
    expect(sessNrcLen == 3 && sessNrc[0] == 0x7F && sessNrc[1] == 0x10 && sessNrc[2] == 0x12,
           "suppress-positive nrc 0x12 bytes");
    sessLen = 0;
    std::memset(sessBuf, 0, sizeof(sessBuf));
    expect(UdsDiagnosticSessionControl(
               sprSession, 0x01, sessBuf, sizeof(sessBuf), &sessLen, 1000) == DOIPLITE_OK,
           "session remains usable after suppress-positive nrc");
    DoipLiteClose(sprSession);
    sprEntity.stop();

    // Default entity also stays silent for 0x81 and keeps the connection.
    DoipLiteSession *sprEcho = DoipLiteOpen(entity.ip(), entity.port());
    expect(sprEcho != nullptr, "open default entity for suppress echo");
    expect(DoipLiteRoutingActivation(sprEcho, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate default entity for suppress echo");
    sessLen = 99;
    expect(UdsDiagnosticSessionControl(
               sprEcho, 0x81, sessBuf, sizeof(sessBuf), &sessLen, 400) == DOIPLITE_OK,
           "default entity suppress-positive silence is success");
    expect(sessLen == 0, "default entity suppress-positive empty response");
    const uint8_t testerPresentAfterSpr[1] = {0x00};
    udsLen = 0;
    expect(DoipLiteSendUds(sprEcho,
                           0x3E,
                           testerPresentAfterSpr,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           1000) == DOIPLITE_OK,
           "sendUds after suppress-positive still aligned");
    expect(udsLen == 2 && udsResp[0] == 0x7E && udsResp[1] == 0x00,
           "uds response after suppress-positive");
    DoipLiteClose(sprEcho);

    // --- P2 / P2* timing ---
    uint32_t openP2 = 0;
    uint32_t openP2Star = 0;
    DoipLiteSession *timingProbe = DoipLiteOpen(entity.ip(), entity.port());
    expect(timingProbe != nullptr, "open timing probe");
    DoipLiteGetP2Timing(timingProbe, &openP2, &openP2Star);
    expect(openP2 == DOIPLITE_DEFAULT_P2_MS && openP2Star == DOIPLITE_DEFAULT_P2_STAR_MS,
           "open session has default P2/P2*");
    DoipLiteSetP2Timing(timingProbe, 0, 0);
    DoipLiteGetP2Timing(timingProbe, &openP2, &openP2Star);
    expect(openP2 == DOIPLITE_DEFAULT_P2_MS && openP2Star == DOIPLITE_DEFAULT_P2_STAR_MS,
           "SetP2Timing 0 falls back to defaults");
    DoipLiteClose(timingProbe);

    // First response within P2 succeeds (delayed but under the window).
    LoopbackEntity::Options p2OkOpts;
    p2OkOpts.udsResponseDelayMs = 30;
    LoopbackEntity p2OkEntity(p2OkOpts);
    expect(p2OkEntity.start(), "start p2-ok entity");
    DoipLiteSession *p2OkSession = DoipLiteOpen(p2OkEntity.ip(), p2OkEntity.port());
    expect(p2OkSession != nullptr, "open p2-ok entity");
    expect(DoipLiteRoutingActivation(p2OkSession, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "activate p2-ok entity");
    DoipLiteSetP2Timing(p2OkSession, 100, 500);
    udsLen = 0;
    expect(DoipLiteSendUds(p2OkSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           0) == DOIPLITE_OK,
           "first response within P2 succeeds");
    expect(udsLen == 2 && udsResp[0] == 0x7E && udsResp[1] == 0x00,
           "P2 success response bytes");
    DoipLiteClose(p2OkSession);
    p2OkEntity.stop();

    // No response within P2 times out (timeoutMs=0 → session P2).
    LoopbackEntity::Options p2SilentOpts;
    p2SilentOpts.diagnosticMode = LoopbackEntity::DiagnosticMode::Silent;
    LoopbackEntity p2SilentEntity(p2SilentOpts);
    expect(p2SilentEntity.start(), "start p2-silent entity");
    DoipLiteSession *p2SilentSession =
        DoipLiteOpen(p2SilentEntity.ip(), p2SilentEntity.port());
    expect(p2SilentSession != nullptr, "open p2-silent entity");
    expect(DoipLiteRoutingActivation(p2SilentSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate p2-silent entity");
    DoipLiteSetP2Timing(p2SilentSession, 80, 500);
    expect(DoipLiteSendUds(p2SilentSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           0) == DOIPLITE_ERR_TIMEOUT,
           "no response within P2 times out");
    DoipLiteClose(p2SilentSession);
    p2SilentEntity.stop();

    // 0x78 then final within P2* succeeds.
    LoopbackEntity::Options p2StarOkOpts;
    p2StarOkOpts.udsPendingCount = 1;
    p2StarOkOpts.udsPendingDelayMs = 40;
    LoopbackEntity p2StarOkEntity(p2StarOkOpts);
    expect(p2StarOkEntity.start(), "start p2*-ok entity");
    DoipLiteSession *p2StarOkSession =
        DoipLiteOpen(p2StarOkEntity.ip(), p2StarOkEntity.port());
    expect(p2StarOkSession != nullptr, "open p2*-ok entity");
    expect(DoipLiteRoutingActivation(p2StarOkSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate p2*-ok entity");
    DoipLiteSetP2Timing(p2StarOkSession, 50, 200);
    udsLen = 0;
    expect(DoipLiteSendUds(p2StarOkSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           0) == DOIPLITE_OK,
           "0x78 then final within P2* succeeds");
    expect(udsLen == 2 && udsResp[0] == 0x7E, "P2* success final response");
    DoipLiteClose(p2StarOkSession);
    p2StarOkEntity.stop();

    // 0x78 then silence beyond P2* times out (full P2* window, not leftover P2).
    LoopbackEntity::Options pendingTimeoutOpts;
    pendingTimeoutOpts.udsPendingCount = 1;
    pendingTimeoutOpts.udsDropAfterPending = true;
    LoopbackEntity pendingTimeoutEntity(pendingTimeoutOpts);
    expect(pendingTimeoutEntity.start(), "start pending-timeout entity");
    DoipLiteSession *pendingTimeoutSession =
        DoipLiteOpen(pendingTimeoutEntity.ip(), pendingTimeoutEntity.port());
    expect(pendingTimeoutSession != nullptr, "open pending-timeout entity");
    expect(DoipLiteRoutingActivation(pendingTimeoutSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate pending-timeout entity");
    DoipLiteSetP2Timing(pendingTimeoutSession, 50, 100);
    expect(DoipLiteSendUds(pendingTimeoutSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           0) == DOIPLITE_ERR_TIMEOUT,
           "0x78 then silence beyond P2* times out");
    DoipLiteClose(pendingTimeoutSession);
    pendingTimeoutEntity.stop();

    // Repeated 0x78: each resets P2*; gaps would exceed a single short leftover window.
    LoopbackEntity::Options multiPendingOpts;
    multiPendingOpts.udsPendingCount = 3;
    multiPendingOpts.udsPendingGapMs = 70;
    multiPendingOpts.udsPendingDelayMs = 0;
    LoopbackEntity multiPendingEntity(multiPendingOpts);
    expect(multiPendingEntity.start(), "start multi-pending entity");
    DoipLiteSession *multiPendingSession =
        DoipLiteOpen(multiPendingEntity.ip(), multiPendingEntity.port());
    expect(multiPendingSession != nullptr, "open multi-pending entity");
    expect(DoipLiteRoutingActivation(multiPendingSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate multi-pending entity");
    // First window short; after each 0x78 a full 120ms P2* applies.
    DoipLiteSetP2Timing(multiPendingSession, 40, 120);
    udsLen = 0;
    expect(DoipLiteSendUds(multiPendingSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           0) == DOIPLITE_OK,
           "repeated 0x78 waits with P2* reset");
    expect(udsLen == 2 && udsResp[0] == 0x7E, "multi-pending final response");
    DoipLiteClose(multiPendingSession);
    multiPendingEntity.stop();

    // suppressPositive + 0x78 then silence: timeout, not success.
    LoopbackEntity::Options sprPendingOpts;
    sprPendingOpts.supportedSession = 0x01;
    sprPendingOpts.rejectUnknownSession = true;
    sprPendingOpts.udsPendingCount = 1;
    sprPendingOpts.udsDropAfterPending = true;
    LoopbackEntity sprPendingEntity(sprPendingOpts);
    expect(sprPendingEntity.start(), "start suppress-pending entity");
    DoipLiteSession *sprPendingSession =
        DoipLiteOpen(sprPendingEntity.ip(), sprPendingEntity.port());
    expect(sprPendingSession != nullptr, "open suppress-pending entity");
    expect(DoipLiteRoutingActivation(sprPendingSession, 0x0E00, 0x00, nullptr, 0) ==
               DOIPLITE_OK,
           "activate suppress-pending entity");
    DoipLiteSetP2Timing(sprPendingSession, 50, 100);
    sessLen = 99;
    expect(UdsDiagnosticSessionControl(
               sprPendingSession, 0x81, sessBuf, sizeof(sessBuf), &sessLen, 0) ==
               DOIPLITE_ERR_TIMEOUT,
           "suppress after 0x78 silence is timeout not success");
    DoipLiteClose(sprPendingSession);
    sprPendingEntity.stop();

    // A closed TCP port should produce a network error, not hang.
    DoipLiteSession *refused = DoipLiteOpen("127.0.0.1", 1);
    expect(refused != nullptr, "open closed port still allocates");
    const DoipLiteStatus netSt =
        DoipLiteRoutingActivationEx(refused, 0x0E00, 0x00, nullptr, 0, 500);
    expect(netSt == DOIPLITE_ERR_NETWORK || netSt == DOIPLITE_ERR_TIMEOUT,
           "connect to closed port is network or timeout");
    DoipLiteClose(refused);

#ifdef DOIPLITE_CLI_PATH
    int exitCode = 99;
    std::string output;
    expect(run_cli("--help", &exitCode, &output), "run cli help");
    expect(exitCode == 0, "cli --help exits 0");
    expect(output.find("discover") != std::string::npos, "help mentions discover");

    char discArgs[256];
    std::snprintf(discArgs,
                  sizeof(discArgs),
                  "discover --broadcast 127.0.0.1 --port %u --timeout-ms 1000",
                  static_cast<unsigned>(entity.port()));
    expect(run_cli(discArgs, &exitCode, &output), "run cli discover");
    expect(exitCode == 0, "cli discover exits 0");
    expect(output.find("127.0.0.1") != std::string::npos, "cli discover prints ip");
    expect(output.find("0x1234") != std::string::npos, "cli discover prints logical address");

    char actArgs[256];
    std::snprintf(actArgs,
                  sizeof(actArgs),
                  "activate --ip 127.0.0.1 --port %u --tester-addr 0x0E00 --timeout-ms 1000",
                  static_cast<unsigned>(entity.port()));
    expect(run_cli(actArgs, &exitCode, &output), "run cli activate");
    expect(exitCode == 0, "cli activate exits 0");
    expect(output.find("0x1234") != std::string::npos, "cli activate prints entity address");

    char nackArgs[256];
    std::snprintf(nackArgs,
                  sizeof(nackArgs),
                  "activate --ip 127.0.0.1 --port %u --tester-addr 0x0E00 --timeout-ms 1000",
                  static_cast<unsigned>(nackEntity.port()));
    // nack entity was stopped; start a fresh one
    LoopbackEntity::Options nackOpts2;
    nackOpts2.routingCode = 0x00;
    LoopbackEntity nack2(nackOpts2);
    expect(nack2.start(), "start nack entity for cli");
    std::snprintf(nackArgs,
                  sizeof(nackArgs),
                  "activate --ip 127.0.0.1 --port %u --tester-addr 0x0E00 --timeout-ms 1000",
                  static_cast<unsigned>(nack2.port()));
    expect(run_cli(nackArgs, &exitCode, &output), "run cli activate nack");
    expect(exitCode != 0, "cli activate nack is non-zero");
    expect(output.find("doip_nack") != std::string::npos, "cli mentions doip_nack");
    expect(output.find("0x00") != std::string::npos || output.find("0x0") != std::string::npos,
           "cli surfaces nack code");
    nack2.stop();

    int missingDidCode = 99;
    std::string missingDidOut;
    expect(run_cli("read-version --ip 127.0.0.1 --port 13400", &missingDidCode, &missingDidOut),
           "run cli read-version without did");
    expect(missingDidCode != 0, "cli read-version without did is non-zero");
    expect(missingDidOut.find("did") != std::string::npos ||
               missingDidOut.find("version_did") != std::string::npos,
           "cli mentions missing did");

    LoopbackEntity::Options cliDidOpts;
    cliDidOpts.supportedDid = 0x0001;
    cliDidOpts.didData = "SIM-1.0";
    cliDidOpts.rejectUnknownDid = true;
    LoopbackEntity cliDid(cliDidOpts);
    expect(cliDid.start(), "start cli read-did entity");
    char verArgs[256];
    std::snprintf(verArgs,
                  sizeof(verArgs),
                  "read-version --ip 127.0.0.1 --port %u --tester-addr 0x0E00 "
                  "--did 0x0001 --timeout-ms 1000",
                  static_cast<unsigned>(cliDid.port()));
    expect(run_cli(verArgs, &exitCode, &output), "run cli read-version");
    expect(exitCode == 0, "cli read-version exits 0");
    expect(output.find("SIM-1.0") != std::string::npos, "cli prints version string");
    expect(output.find("62 00 01") != std::string::npos ||
               output.find("62 00 01 53 49 4D 2D 31 2E 30") != std::string::npos,
           "cli prints raw uds hex");

    char badDidArgs[256];
    std::snprintf(badDidArgs,
                  sizeof(badDidArgs),
                  "read-version --ip 127.0.0.1 --port %u --tester-addr 0x0E00 "
                  "--did 0x0002 --timeout-ms 1000",
                  static_cast<unsigned>(cliDid.port()));
    expect(run_cli(badDidArgs, &exitCode, &output), "run cli read-version unknown did");
    expect(exitCode != 0, "cli unknown did is non-zero");
    expect(output.find("uds_nrc") != std::string::npos, "cli mentions uds_nrc");
    expect(output.find("0x31") != std::string::npos, "cli surfaces nrc 0x31");
    cliDid.stop();

    int missingSessCode = 99;
    std::string missingSessOut;
    expect(run_cli("session --ip 127.0.0.1 --port 13400", &missingSessCode, &missingSessOut),
           "run cli session without type");
    expect(missingSessCode != 0, "cli session without type is non-zero");
    expect(missingSessOut.find("session") != std::string::npos ||
               missingSessOut.find("session_type") != std::string::npos,
           "cli mentions missing session type");

    // Oversized values must be rejected before narrowing. On LP64,
    // 0x100000002 truncated to uint32_t becomes programmingSession 0x02.
    int overflowSessCode = 0;
    std::string overflowSessOut;
    expect(run_cli("session --ip 127.0.0.1 --port 13400 --session 0x100000002",
                   &overflowSessCode,
                   &overflowSessOut),
           "run cli session oversized hex");
    expect(overflowSessCode == 2, "cli oversized session exits 2");
    expect(overflowSessOut.find("invalid") != std::string::npos,
           "cli oversized session mentions invalid");
    expect(overflowSessOut.find("network") == std::string::npos &&
               overflowSessOut.find("timeout") == std::string::npos,
           "cli oversized session does not reach the wire");

    overflowSessCode = 0;
    overflowSessOut.clear();
    expect(run_cli("session --ip 127.0.0.1 --port 13400 --session 4294967298",
                   &overflowSessCode,
                   &overflowSessOut),
           "run cli session oversized decimal");
    expect(overflowSessCode == 2, "cli oversized decimal session exits 2");
    expect(overflowSessOut.find("invalid") != std::string::npos,
           "cli oversized decimal session mentions invalid");

    overflowSessCode = 0;
    overflowSessOut.clear();
    expect(run_cli("session --ip 127.0.0.1 --port 13400 --session 0x102",
                   &overflowSessCode,
                   &overflowSessOut),
           "run cli session 0x102");
    expect(overflowSessCode == 2, "cli session 0x102 exits 2");
    expect(overflowSessOut.find("invalid") != std::string::npos,
           "cli session 0x102 mentions invalid");

    LoopbackEntity::Options cliSessOpts;
    cliSessOpts.supportedSession = 0x01;
    cliSessOpts.rejectUnknownSession = true;
    cliSessOpts.sessionIncludeTiming = true;
    cliSessOpts.sessionP2 = 0x0032;
    cliSessOpts.sessionP2Star = 0x01F4;
    LoopbackEntity cliSess(cliSessOpts);
    expect(cliSess.start(), "start cli session entity");
    char sessArgs[256];
    std::snprintf(sessArgs,
                  sizeof(sessArgs),
                  "session --ip 127.0.0.1 --port %u --tester-addr 0x0E00 "
                  "--session 0x01 --timeout-ms 1000",
                  static_cast<unsigned>(cliSess.port()));
    expect(run_cli(sessArgs, &exitCode, &output), "run cli session");
    expect(exitCode == 0, "cli session exits 0");
    expect(output.find("50 01") != std::string::npos, "cli prints raw 0x50 response");
    expect(output.find("session_type=0x01") != std::string::npos, "cli prints session type");
    expect(output.find("p2_ms=50") != std::string::npos, "cli prints P2");
    expect(output.find("p2_star=0x01F4") != std::string::npos, "cli prints P2*");

    char sessNrcArgs[256];
    std::snprintf(sessNrcArgs,
                  sizeof(sessNrcArgs),
                  "session --ip 127.0.0.1 --port %u --tester-addr 0x0E00 "
                  "--session 0x03 --timeout-ms 1000",
                  static_cast<unsigned>(cliSess.port()));
    expect(run_cli(sessNrcArgs, &exitCode, &output), "run cli session nrc");
    expect(exitCode != 0, "cli unsupported session is non-zero");
    expect(output.find("uds_nrc") != std::string::npos, "cli session mentions uds_nrc");
    expect(output.find("0x12") != std::string::npos, "cli surfaces nrc 0x12");
    cliSess.stop();

    LoopbackEntity::Options cliSilentOpts;
    cliSilentOpts.diagnosticMode = LoopbackEntity::DiagnosticMode::Silent;
    LoopbackEntity cliSilent(cliSilentOpts);
    expect(cliSilent.start(), "start cli silent entity");
    char toArgs[256];
    std::snprintf(toArgs,
                  sizeof(toArgs),
                  "read-version --ip 127.0.0.1 --port %u --tester-addr 0x0E00 "
                  "--did 0x0001 --timeout-ms 250",
                  static_cast<unsigned>(cliSilent.port()));
    expect(run_cli(toArgs, &exitCode, &output), "run cli read-version timeout");
    expect(exitCode != 0, "cli read-version timeout is non-zero");
    expect(output.find("timeout") != std::string::npos, "cli mentions timeout");
    cliSilent.stop();
#endif

    entity.stop();
    std::puts("ok");
    return 0;
}
