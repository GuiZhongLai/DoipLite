# DoipLite

DoipLite 是一个轻量级 C++ 库，用于在工控机上通过以太网做 **DoIP**（ISO 13400-2 通用部分）诊断传输，并在其上发送 **UDS**（ISO 14229）请求。核心库不依赖 Qt。

当前已实现：**M1**（UDP 车辆发现、TCP 诊断连接、路由激活，以及 `doiplite-cli` 的 `discover` / `activate`），DoIP 诊断消息托运（`DoipLiteSendUds`，payload type 0x8001 / 0x8002 ACK / 0x8003 NACK），以及 **读版本**（`UdsReadDataByIdentifier` / CLI `read-version`，UDS 0x22；DID 来自调用参数或配置，库不提供默认值）。刷写仍返回 `DOIPLITE_ERR_NOT_IMPLEMENTED`。

本库只实现通用 ISO 13400-2 行为，不包含厂商/OEM 私有扩展。逻辑地址、激活类型、协议版本等均由调用参数或配置传入，库代码不写入现场地址或密钥。

## 分层

- `net`：UDP/TCP，不含诊断语义
- `doip`：DoIP 帧、车辆发现、路由激活、诊断消息托运
- `uds`：UDS 请求/应答与 NRC，不组装 DoIP 头；只通过 `DoipLiteSendUds` 走传输
- `flash`：刷写状态机，只调用 UDS 接口，不直接操作套接字

对外提供 C API（见 `include/doiplite/`），便于之后在其它工程（例如 Qt 作业界面）中调用。

## 路线图

1. **M1**：UDP 车辆发现、TCP 诊断连接、路由激活；命令行工具在工控机上跑通（本仓库当前范围）
2. **M2**：`DoipLiteSendUds` 诊断消息托运（已实现）+ `UdsReadDataByIdentifier` 读取一个版本 DID（已实现；DID 来自配置或参数，库不提供默认值）
3. **M3**：传包/升级状态机（会话、擦除、下载、传输、退出、复位）；安全访问只走回调
4. **M4**：Qt 作业界面（**不在本仓库**，只链接本库）
5. **M5**：与服务器做 HTTPS/REST 版本比对（非第一优先）

## 构建

需要 CMake 3.16+、C++17 编译器。网络层使用 **standalone Asio**（仅 Asio，不是整套 Boost），由 CMake FetchContent 拉取；也可用 `-DASIO_ROOT=` 指向本地 Asio 源码树（需含 `include/asio.hpp`），便于离线工控机环境。

### Windows（MSVC）

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

MSVC 2019 同样支持（换对应生成器即可）。主目标是 Windows 10/11 x64。

### Linux（编译与模拟测试）

```bash
cmake -S . -B build -G Ninja -DDOIPLITE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

测试使用进程内回环 DoIP 模拟器（`127.0.0.1` 上的临时端口），不会访问非回环网络，也不包含密钥或现场数据。构建测试时还会生成辅助程序 `doiplite-loopback-sim`，可手动与 CLI 联调：

```bash
./build/doiplite-loopback-sim
# 该进程会打印 listening 127.0.0.1 <port>
./build/doiplite-cli discover --broadcast 127.0.0.1 --port <port> --timeout-ms 1000
./build/doiplite-cli activate --ip 127.0.0.1 --port <port> --tester-addr 0x0E00
./build/doiplite-cli read-version --ip 127.0.0.1 --port <port> --tester-addr 0x0E00 --did 0x0001
```

回环模拟器会对示例 DID `0x0001` 返回可打印的假版本字符串 `SIM-1.0`（仅供本地联调，不是现场 DID）。

## 配置与密钥

逻辑地址、版本 DID、现场 IP 等**不要**写进库代码，通过配置文件或 API 参数传入。`examples/config/example.conf` 只是本地回环示例，请换成你自己的值，且不要把真实现场配置提交到本仓库。

UDS 安全访问的种子—密钥算法不在本库实现，请在调用方注册 `DoipLiteSeedKeyFn`。详见 [SECURITY.md](SECURITY.md)。

## 命令行

`doiplite-cli` 支持：

- `--help`：打印用法，退出码 0
- `discover`：发送车辆识别请求并收集应答，打印每个实体的 IP 与逻辑地址；至少发现一台时退出 0，超时或网络错误则以非 0 退出并给出原因
- `activate`：对指定 IPv4/端口建立 TCP 连接并做路由激活；成功退出 0，DoIP NACK / 超时 / 网络错误则以非 0 退出（NACK 会附带响应码）
- `read-version`：对指定 IPv4/端口做路由激活后，发送 UDS 0x22 读取版本 DID。DID 必须由 `--did` 或配置项 `version_did` 提供，库不提供默认 DID。成功时打印原始 UDS 应答与数据记录的十六进制；若数据为可打印文本，同时打印版本字符串，退出 0。超时、DoIP NACK、UDS NRC 或未提供 DID 时以非 0 退出并给出原因

常用参数（也可写入 `--config` 文件，命令行优先）：`--ip`、`--port`、`--broadcast`、`--tester-addr`、`--activation-type`、`--timeout-ms`、`--did`（配置键 `version_did`）。

默认发现目标为有限广播 `255.255.255.255:13400`，也支持单播（例如 `127.0.0.1`）。未指定测试仪逻辑地址时，`activate` 使用 **0x0E00** 作为示例默认值（ISO 13400 外部测试设备地址段 `0x0E00–0x0FFF` 的起始值，**不是**现场地址）；请按你自己的测试仪地址覆盖。激活类型默认 `0x00`（ISO 默认激活）。

库在发现时从临时端口发出请求并收集应答，不强制绑定 13400，因此不会主动监听车辆主动公告（Vehicle Announcement）；向已配置地址发送识别请求即可。

扩展 API：`DoipLiteDiscoverEx`（可指定目的 IPv4 与 UDP 端口）、`DoipLiteRoutingActivationEx`（可指定超时）、`DoipLiteLastDoipCode` / `DoipLiteEntityAddress`。`DoipLiteSendUds` 在已激活会话上发送 0x8001 并等待 UDS 应答；0x8003 / 通用 NACK 的代码可通过 `DoipLiteLastDoipCode` 读取。未连接或尚未路由激活时返回 `DOIPLITE_ERR_NETWORK`；`timeoutMs` 为 0 时使用默认 2000ms。收到 UDS NRC `0x78`（response pending）时继续等待最终应答，直到本次超时。`UdsReadDataByIdentifier` 只通过 `DoipLiteSendUds` 发送 0x22，不自行组装 DoIP 头；成功时写出 DID 之后的数据记录。发送使用协议版本 `0x02` / `0xFD`，接收兼容 `0x01`、`0x02`、`0x03` 与识别请求常见的 `0xFF`。

## 许可证

MIT。本库只提供通用协议实现，不包含 OEM 规范文档或密钥。
