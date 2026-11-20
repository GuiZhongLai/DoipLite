#include "station_config.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#ifdef _WIN32
#define DOIPLITE_POPEN _popen
#define DOIPLITE_PCLOSE _pclose
#else
#include <sys/wait.h>
#define DOIPLITE_POPEN popen
#define DOIPLITE_PCLOSE pclose
#endif

namespace {

void expect(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int cmd_status(int raw)
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

std::string writeTempConfig(const std::string &body, const char *name)
{
    const std::string path = std::string("/tmp/") + name;
    std::ofstream out(path, std::ios::trunc);
    expect(static_cast<bool>(out), "write temp config");
    out << body;
    out.close();
    return path;
}

void fillVin(DoipLiteVehicle *v, const char *vin17)
{
    std::memset(v->vin, 0, sizeof(v->vin));
    if (!vin17)
    {
        return;
    }
    const std::size_t n = std::strlen(vin17);
    expect(n <= 17, "test vin length");
    std::memcpy(v->vin, vin17, n);
}

bool run_process(const std::string &cmd, int *exitCode, std::string *output)
{
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

void test_config_validation()
{
    doiplite_examples::StationConfig cfg;
    std::string err;

    const std::string missing = writeTempConfig(
        "tester_address = 0x0E00\n"
        "version_did = 0x0001\n",
        "doiplite_ex_missing_sel.conf");
    expect(doiplite_examples::LoadStationConfig(missing.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::Ok,
           "load missing selector file");
    expect(!doiplite_examples::ValidateReadVersionConfig(cfg, &err),
           "read-version rejects missing identity");
    expect(err.find("select_") != std::string::npos, "missing identity message");

    const std::string overflow = writeTempConfig(
        "select_logical_address = 0x10000\n"
        "tester_address = 0x0E00\n",
        "doiplite_ex_overflow.conf");
    expect(doiplite_examples::LoadStationConfig(overflow.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::ParseError,
           "reject u16 overflow");
    expect(err.find("select_logical_address") != std::string::npos, "overflow names key");

    const std::string actOk = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "activation_type = 0xFF\n"
        "version_did = 0x0001\n",
        "doiplite_ex_act_ff.conf");
    expect(doiplite_examples::LoadStationConfig(actOk.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::Ok,
           "activation_type 0xFF accepted");
    expect(cfg.activationType == 0xFF, "activation_type boundary 0xFF");

    const std::string actOverflow = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "activation_type = 0x100\n"
        "version_did = 0x0001\n",
        "doiplite_ex_act_100.conf");
    expect(doiplite_examples::LoadStationConfig(actOverflow.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::ParseError,
           "activation_type 0x100 rejected before network");
    expect(err.find("activation_type") != std::string::npos, "activation_type error names key");

    const std::string actDec = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "activation_type = 256\n"
        "version_did = 0x0001\n",
        "doiplite_ex_act_256.conf");
    expect(doiplite_examples::LoadStationConfig(actDec.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::ParseError,
           "activation_type 256 rejected");

    const std::string badKey = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "not_a_real_key = 1\n",
        "doiplite_ex_badkey.conf");
    expect(doiplite_examples::LoadStationConfig(badKey.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::ParseError,
           "reject unknown key");

    const std::string partialKeepAlive = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "download_alfid = 0x44\n"
        "download_memory_address = 0x1000\n"
        "package_path = /tmp/payload.bin\n"
        "flash_keep_alive_interval_ms = 1000\n",
        "doiplite_ex_ka_partial.conf");
    expect(doiplite_examples::LoadStationConfig(partialKeepAlive.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::Ok,
           "load partial keep-alive");
    expect(!doiplite_examples::ValidateFlashConfig(cfg, &err),
           "partial keep-alive invalid");
    expect(err.find("keep-alive") != std::string::npos, "partial keep-alive message");

    const std::string zeroInterval = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "download_alfid = 0x44\n"
        "download_memory_address = 0x1000\n"
        "package_path = /tmp/payload.bin\n"
        "flash_keep_alive_interval_ms = 0\n"
        "flash_keep_alive_sub_function = 0x00\n",
        "doiplite_ex_ka_zero.conf");
    expect(doiplite_examples::LoadStationConfig(zeroInterval.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::Ok,
           "load zero keep-alive interval");
    expect(!doiplite_examples::ValidateFlashConfig(cfg, &err),
           "zero keep-alive interval invalid");

    const std::string okFlash = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "select_vin = DOIPLITEEXAMPLE01\n"
        "tester_address = 0x0E00\n"
        "download_alfid = 0x44\n"
        "download_memory_address = 0x1000\n"
        "package_path = /tmp/payload.bin\n"
        "flash_keep_alive_interval_ms = 2000\n"
        "flash_keep_alive_sub_function = 0x80\n",
        "doiplite_ex_flash_ok.conf");
    expect(doiplite_examples::LoadStationConfig(okFlash.c_str(), &cfg, &err) ==
               doiplite_examples::ConfigLoadStatus::Ok,
           "load ok flash config");
    expect(doiplite_examples::ValidateFlashConfig(cfg, &err), "ok flash validates");

    DoipLiteFlashJob job{};
    doiplite_examples::FillFlashJob(cfg, &job);
    expect(job.keepAliveIntervalMs == 2000, "keep-alive interval mapped");
    expect(job.haveKeepAliveSubFunction == 1, "keep-alive sub present");
    expect(job.keepAliveSubFunction == 0x80, "keep-alive sub value");
    expect(job.haveSecurity == 0, "security off by default");
}

void test_flash_run_classification()
{
    using doiplite_examples::ClassifyFlashRunResult;
    using doiplite_examples::FlashRunResult;
    using doiplite_examples::IsSecurityNotImplementedFailure;

    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_DONE, DOIPLITE_OK, false) ==
               FlashRunResult::DoneOk,
           "DONE+OK is success");
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_DONE, DOIPLITE_OK, true) ==
               FlashRunResult::LimitExhausted,
           "exhausted flag wins over DONE (should not happen in practice)");
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_TRANSFER, DOIPLITE_OK, true) ==
               FlashRunResult::LimitExhausted,
           "transfer still running after limit is exhaustion failure");
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_FAILED, DOIPLITE_OK, false) ==
               FlashRunResult::Failed,
           "FAILED phase is failure even if last status OK");
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_TRANSFER, DOIPLITE_ERR_TIMEOUT, false) ==
               FlashRunResult::PollError,
           "poll error classified");
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_FAILED,
                                  DOIPLITE_ERR_NOT_IMPLEMENTED,
                                  false) == FlashRunResult::PollError,
           "NOT_IMPLEMENTED poll status is PollError class");

    // Success must never be reported for non-DONE terminal-looking states.
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_TRANSFER, DOIPLITE_OK, false) !=
               FlashRunResult::DoneOk,
           "non-DONE never DoneOk");
    expect(ClassifyFlashRunResult(DOIPLITE_FLASH_FAILED, DOIPLITE_OK, false) !=
               FlashRunResult::DoneOk,
           "FAILED never DoneOk");

    doiplite_examples::StationConfig secCfg;
    secCfg.haveSecurity = true;
    expect(IsSecurityNotImplementedFailure(secCfg, DOIPLITE_ERR_NOT_IMPLEMENTED),
           "security+NOT_IMPLEMENTED actionable");
    expect(!IsSecurityNotImplementedFailure(secCfg, DOIPLITE_ERR_TIMEOUT),
           "security without NOT_IMPLEMENTED is false");
    doiplite_examples::StationConfig noSec;
    expect(!IsSecurityNotImplementedFailure(noSec, DOIPLITE_ERR_NOT_IMPLEMENTED),
           "NOT_IMPLEMENTED without security flag is not security explanation");
}

void test_identity_matching()
{
    doiplite_examples::StationConfig cfg;
    cfg.haveSelectLogicalAddress = true;
    cfg.selectLogicalAddress = 0x1234;

    DoipLiteVehicle vehicles[3]{};
    std::snprintf(vehicles[0].ip, sizeof(vehicles[0].ip), "127.0.0.1");
    vehicles[0].tcpPort = 10001;
    vehicles[0].logicalAddress = 0x1111;
    fillVin(&vehicles[0], "AAAAAAAAAAAAAAA01");

    std::snprintf(vehicles[1].ip, sizeof(vehicles[1].ip), "127.0.0.1");
    vehicles[1].tcpPort = 10002;
    vehicles[1].logicalAddress = 0x1234;
    fillVin(&vehicles[1], "DOIPLITEEXAMPLE01");

    std::snprintf(vehicles[2].ip, sizeof(vehicles[2].ip), "127.0.0.1");
    vehicles[2].tcpPort = 10003;
    vehicles[2].logicalAddress = 0x1234;
    fillVin(&vehicles[2], "DOIPLITEEXAMPLE02");

    uint32_t index = 99;
    uint32_t matches = 0;
    std::string err;

    doiplite_examples::StationConfig none;
    expect(doiplite_examples::SelectUniqueVehicle(
               vehicles, 3, none, &index, &matches, &err) ==
               doiplite_examples::DeviceSelectStatus::NoSelectorConfigured,
           "no selector refuses first");

    expect(doiplite_examples::SelectUniqueVehicle(
               vehicles, 3, cfg, &index, &matches, &err) ==
               doiplite_examples::DeviceSelectStatus::MultipleMatches,
           "two logical-address matches fail");
    expect(matches == 2, "two matches counted");

    cfg.haveSelectVin = true;
    cfg.selectVin = "DOIPLITEEXAMPLE01";
    expect(doiplite_examples::SelectUniqueVehicle(
               vehicles, 3, cfg, &index, &matches, &err) ==
               doiplite_examples::DeviceSelectStatus::Ok,
           "AND selectors unique");
    expect(index == 1 && matches == 1, "unique index");

    cfg.selectVin = "NOSUCHVIN00000001";
    expect(doiplite_examples::SelectUniqueVehicle(
               vehicles, 3, cfg, &index, &matches, &err) ==
               doiplite_examples::DeviceSelectStatus::ZeroMatches,
           "zero matches");
    expect(matches == 0, "zero match count");
}

void test_flash_dry_run_gate()
{
#ifndef DOIPLITE_FLASH_EXAMPLE_PATH
    expect(false, "flash example path define missing");
#else
    const std::string conf = writeTempConfig(
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "download_alfid = 0x44\n"
        "download_memory_address = 0x1000\n"
        "package_path = /tmp/does-not-need-to-exist-for-dry-run.bin\n"
        "flash_session_type = 0x02\n"
        "flash_keep_alive_interval_ms = 1500\n"
        "flash_keep_alive_sub_function = 0x00\n",
        "doiplite_ex_dryrun.conf");

    int code = 99;
    std::string out;
    const std::string cmd = std::string("\"") + DOIPLITE_FLASH_EXAMPLE_PATH +
                            "\" --config \"" + conf + "\" 2>&1";
    expect(run_process(cmd, &code, &out), "spawn flash dry-run");
    expect(code == 0, "dry-run exit 0");
    expect(out.find("dry-run") != std::string::npos, "dry-run banner");
    expect(out.find("no network") != std::string::npos ||
               out.find("NO network") != std::string::npos ||
               out.find("no ECU") != std::string::npos,
           "dry-run states no network/ECU change");
    expect(out.find("package_path is not opened") != std::string::npos ||
               out.find("config fields validated only") != std::string::npos,
           "dry-run states package/payload not validated");
    expect(out.find("keep-alive") != std::string::npos, "dry-run shows keep-alive");
    expect(out.find("--execute") != std::string::npos, "dry-run mentions execute gate");
    // Must not look like a real discover/connect happened.
    expect(out.find("DoipLiteOpen") == std::string::npos, "no open in dry-run output");
    expect(out.find("discover:") == std::string::npos, "no discover listing in dry-run");

    // Missing --config should fail before any network.
    code = 0;
    out.clear();
    const std::string noConf =
        std::string("\"") + DOIPLITE_FLASH_EXAMPLE_PATH + "\" 2>&1";
    expect(run_process(noConf, &code, &out), "spawn without config");
    expect(code != 0, "missing config fails");

    // --execute without reachable device should fail after attempting discover,
    // proving the gate is what enables network — but we must not require a real
    // ECU. Use a short unicast to 127.0.0.1 on a closed port via config.
    const std::string execConf = writeTempConfig(
        "broadcast = 127.0.0.1\n"
        "port = 1\n"
        "discover_timeout_ms = 200\n"
        "select_logical_address = 0x1234\n"
        "tester_address = 0x0E00\n"
        "download_alfid = 0x44\n"
        "download_memory_address = 0x1000\n"
        "package_path = /tmp/does-not-need-to-exist.bin\n",
        "doiplite_ex_exec.conf");
    code = 0;
    out.clear();
    const std::string execCmd = std::string("\"") + DOIPLITE_FLASH_EXAMPLE_PATH +
                                "\" --config \"" + execConf + "\" --execute 2>&1";
    expect(run_process(execCmd, &code, &out), "spawn flash execute");
    expect(code != 0, "execute against empty network fails");
    expect(out.find("WARNING") != std::string::npos, "execute prints warning");
    expect(out.find("discover") != std::string::npos, "execute attempts discover");
#endif
}

void test_read_version_requires_config()
{
#ifndef DOIPLITE_READ_VERSION_EXAMPLE_PATH
    expect(false, "read-version example path define missing");
#else
    int code = 0;
    std::string out;
    const std::string cmd =
        std::string("\"") + DOIPLITE_READ_VERSION_EXAMPLE_PATH + "\" 2>&1";
    expect(run_process(cmd, &code, &out), "spawn read-version");
    expect(code != 0, "read-version requires config");

    const std::string conf = writeTempConfig(
        "tester_address = 0x0E00\n"
        "version_did = 0x0001\n",
        "doiplite_ex_rv_nosel.conf");
    code = 0;
    out.clear();
    const std::string cmd2 = std::string("\"") + DOIPLITE_READ_VERSION_EXAMPLE_PATH +
                             "\" --config \"" + conf + "\" 2>&1";
    expect(run_process(cmd2, &code, &out), "spawn read-version bad cfg");
    expect(code != 0, "read-version rejects missing selector before network");
    expect(out.find("select_") != std::string::npos ||
               out.find("identity") != std::string::npos ||
               out.find("invalid config") != std::string::npos,
           "read-version invalid config message");
#endif
}

} // namespace

int main()
{
    test_config_validation();
    test_flash_run_classification();
    test_identity_matching();
    test_flash_dry_run_gate();
    test_read_version_requires_config();
    std::puts("doiplite-test-examples: ok");
    return 0;
}
