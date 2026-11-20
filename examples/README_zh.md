# DoipLite 公共 C API 示例说明

本目录提供两个可直接编译运行的示例程序，仅调用库的公共 C API（C++17 源文件，不依赖内部头文件）。配置解析属于示例层，不进入核心库。

## 示例一览

| 目标 | 程序 | 作用 |
|------|------|------|
| `doiplite-example-read-version` | `read_version/main.cpp` | UDP 发现 → 按配置身份唯一选设备 → TCP 连接 → 路由激活 → `UdsReadDataByIdentifier` 读版本 → 关闭会话 |
| `doiplite-example-flash-job` | `flash_job/main.cpp` | 构造 `DoipLiteFlashJob`；**默认干跑**只校验并打印计划；加 `--execute` 才发现/连接/刷写 |

共享辅助：`common/station_config.*`（配置加载、身份筛选、刷写参数填充）。

配置模板：`config/station_template.conf`（仅注释占位，无现场数据）。CLI 用的 `config/example.conf` 保持不变。

## 身份选择约定

`DoipLiteDiscover` / `DoipLiteDiscoverEx` 返回的 `DoipLiteVehicle` 含：`ip`、`tcpPort`、`logicalAddress`、`vin[18]`。

示例支持的配置选择器（至少设置一个；多个同时设置时为 **AND**）：

- `select_logical_address` / `entity_logical_address` → 匹配 `logicalAddress`
- `select_vin` / `entity_vin` → 匹配 `vin`（1..17 字符；较短 VIN 要求车辆剩余字节为 0）

规则：

1. **未配置任何选择器**：拒绝运行，不会静默选第一台。
2. **0 个匹配** 或 **多于 1 个匹配**：明确失败。
3. 连接使用匹配结果的 **`ip` + `tcpPort`**（发现得到的端点），配置中不手填连接 IP。

## 配置键与 C API 对应

### 发现 / 会话

| 配置键 | C API |
|--------|-------|
| `broadcast` / `discover_dest_ip` | `DoipLiteDiscoverEx(..., destIp, ...)`；空则库内有限广播 |
| `port` / `udp_port` | `DoipLiteDiscoverEx` 的 `udpPort`；0 表示默认 13400 |
| `discover_timeout_ms` | 发现超时；未设则用 `timeout_ms` |
| `tester_address` | `DoipLiteRoutingActivationEx` 的 `testerAddr` |
| `activation_type` | 激活类型；未设则 `0x00` |
| `timeout_ms` | 激活 / UDS / Flash 超时 |
| `p2_ms` / `p2_star_ms` | `DoipLiteSetP2Timing`（可选） |

### 读版本

| 配置键 | C API |
|--------|-------|
| `version_did` | `UdsReadDataByIdentifier(..., did, ...)` |

### 刷写作业 → `DoipLiteFlashJob`

| 配置键 | 字段 |
|--------|------|
| `flash_session_type` | `haveFlashSessionType` + `flashSessionType` |
| `flash_require_security` + `flash_security_level` | `haveSecurity` + `securityLevel`（0x27 **未实现** → `NOT_IMPLEMENTED`） |
| `flash_erase_routine_*` | 擦除例程 `0x31` |
| `download_data_format_id` / `download_alfid` / `download_memory_address` / `download_memory_size` | `0x34` 参数 |
| `package_path` | `packagePath` |
| `flash_max_block_data_cap` | `maxBlockDataCap` |
| `transfer_exit_param_hex` | `transferExitParam` |
| `flash_reset_type` | `haveEcuReset` + `resetType` |
| `flash_keep_alive_interval_ms` | `keepAliveIntervalMs`（须 >0） |
| `flash_keep_alive_sub_function` | `haveKeepAliveSubFunction` + `keepAliveSubFunction` |

保活：须**同时**提供间隔与子功能；库不提供默认值。仅在步骤/`0x36` 块间隙发送，不在等待应答（含 `0x78`）期间插入。

## 刷写干跑与执行门闩

```bash
# 默认：只校验配置字段并打印计划（无网络、不改 ECU）
# 干跑不打开 package_path，也不核对载荷长度与 download_memory_size；
# 这些要到 --execute 后的 FlashStart 才会检查。勿把干跑通过理解为包已校验。
./doiplite-example-flash-job --config /path/to/station.conf

# 显式执行：才会 discover / connect / FlashStart / FlashPoll
# 仅 phase==DONE 视为成功；轮询次数耗尽为明确失败
# 警告：可能擦除、写入、复位 ECU
./doiplite-example-flash-job --config /path/to/station.conf --execute
```

若 `flash_require_security=1`，核心在安全阶段以 `DOIPLITE_ERR_NOT_IMPLEMENTED` 失败（phase 会变为 `FAILED`）；示例根据作业配置打印该契约说明，不实现 `0x27`。

`activation_type` 按线路 8 位校验（`0..0xFF`）；`0x100` 等超出值在解析时拒绝，不会截断后联网。

## 构建与运行

依赖与主库相同：CMake 3.16+、C++17、standalone Asio。可用 `-DASIO_ROOT=` 指向含 `include/asio.hpp` 的本地 Asio 树（离线）。

```bash
# Ninja（若已安装 ninja）
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=g++ \
  -DDOIPLITE_BUILD_TESTS=ON -DDOIPLITE_BUILD_EXAMPLES=ON
# 离线 Asio 示例：
#   -DASIO_ROOT=/path/to/asio
cmake --build build
ctest --test-dir build --output-on-failure

# 读版本（需真实/回环实体与填好的配置）
./build/doiplite-example-read-version --config examples/config/station_template.conf

# 刷写干跑（推荐先跑）
./build/doiplite-example-flash-job --config /path/to/your_station.conf
```

未装 Ninja 时可用默认生成器（如 Unix Makefiles），省略 `-G Ninja` 即可。`doiplite-cli` 与原有测试目标行为不变。

## 安全提示

- 不要把现场地址、密钥、真 VIN、载荷路径提交进仓库。
- 刷写 `--execute` 仅用于明确目标设备。
- 核心库仍无图形界面依赖；本示例亦不引入额外大型框架。
