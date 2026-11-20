#include "doiplite/doiplite.h"
#include "loopback_entity.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    DoipLiteClose(hangSession);
    hangEntity.stop();

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
    expect(DoipLiteSendUds(pendingTimeoutSession,
                           0x3E,
                           testerPresent,
                           1,
                           udsResp,
                           sizeof(udsResp),
                           &udsLen,
                           250) == DOIPLITE_ERR_TIMEOUT,
           "0x78 then silence times out");
    DoipLiteClose(pendingTimeoutSession);
    pendingTimeoutEntity.stop();

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
