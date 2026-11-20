# DoipLite

DoipLite 是一个轻量级 C++ 库，用于在工控机上通过以太网做 **DoIP**（ISO 13400）诊断传输，并在其上发送 **UDS**（ISO 14229）请求。核心库不依赖 Qt。

当前仓库是可编译的工程骨架（里程碑 M1 之前）。协议握手、读版本、刷写状态机尚未实现，对应 API 会返回 `DOIPLITE_ERR_NOT_IMPLEMENTED`。

## 分层

- `net`：UDP/TCP，不含诊断语义
- `doip`：DoIP 帧、车辆发现、路由激活、诊断消息托运
- `uds`：UDS 请求/应答与 NRC，不组装 DoIP 头；只通过 `DoipLiteSendUds` 走传输
- `flash`：刷写状态机，只调用 UDS 接口，不直接操作套接字

对外提供 C API（见 `include/doiplite/`），便于之后在其它工程（例如 Qt 作业界面）中调用。

## 路线图

1. **M1**：UDP 车辆发现、TCP 诊断连接、路由激活；命令行工具在工控机上跑通
2. **M2**：`DoipLiteSendUds` + `UdsReadDataByIdentifier` 读取一个版本 DID（DID 来自配置）
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
cmake -S . -B build -DDOIPLITE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## 配置与密钥

逻辑地址、版本 DID、现场 IP 等**不要**写进库代码，通过配置文件或 API 参数传入。`examples/config/example.conf` 只是本地回环示例，请换成你自己的值，且不要把真实现场配置提交到本仓库。

UDS 安全访问的种子—密钥算法不在本库实现，请在调用方注册 `DoipLiteSeedKeyFn`。详见 [SECURITY.md](SECURITY.md)。

## 命令行

`doiplite-cli` 目前是空壳，支持 `--help`。计划中的 M1 子命令：`discover`、`activate`（实现后才会真正访问网络）。

## 许可证

MIT。本库只提供协议实现框架，不包含 OEM 规范文档或密钥。
