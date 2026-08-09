# FyuuRuntime 使用指南 / FyuuRuntime User Guide

FyuuRuntime 是 FyuuEngine 的平台无关生命周期核心。它通过同一套状态机连接平台事件、应用更新与关闭流程，并同时提供 C ABI 与 C++23 Modules 入口。`FyuuDesktop` 是独立的 SDL3 桌面适配器，不属于 Runtime 核心。

FyuuRuntime is the platform-independent lifecycle core of FyuuEngine. One state machine connects platform events, application updates, and shutdown while exposing both a C ABI and a C++23 Modules entry. `FyuuDesktop` is a separate SDL3 adapter and is not part of the Runtime core.

- [中文](#中文)
- [English](#english)

---

## 中文

### 1. 设计概览

Runtime 将平台实现和应用实现分开：

- `Platform` 在自己的构造和析构中管理平台资源，并实现事件泵。
- `Application` 在自己的构造和析构中管理应用资源，并实现逐帧更新和关闭确认。
- `Platform` 同时定义平台事件泵并拥有 C ABI 平台对象；具体适配器负责原生资源。
- `Runtime` 拥有 C ABI Runtime 对象，但只借用 `Platform` 和 `Application`。

一次完整启动严格遵循以下顺序：

1. 创建具体的 `Platform`，其基类构造函数创建 ABI Platform，具体构造函数取得原生资源。
2. 创建具体的 `Application`。
3. 使用 `Platform` 和 `Application` 构造 `Runtime`，构造函数创建并初始化 ABI Runtime。
4. 调用 `Runtime::Run`，或手动循环调用 `Tick`。
5. 先销毁 `Runtime`，再销毁 `Application` 和 `Platform`。

对象的借用关系决定了声明顺序。推荐按以下顺序声明局部对象：

```text
Platform
Application
Runtime
```

局部对象按相反顺序析构，因此实际销毁顺序是：

```text
Runtime -> Application -> Platform
```

### 2. 构建与导入

平台无关代码链接 `FyuuEngine` 并导入主模块：

```cmake
target_link_libraries(MyTarget PRIVATE FyuuEngine)
```

```cpp
import fyuu_engine;
```

桌面应用链接 `FyuuDesktop`。该目标会公开传递 `FyuuEngine` 依赖：

```cmake
target_link_libraries(MyTarget PRIVATE FyuuDesktop)
```

```cpp
import fyuu_desktop;
import fyuu_engine;
```

项目要求 C++23 和支持 Modules 依赖扫描的 CMake 工具链。Clang 构建使用 GNU-style `clang++` 驱动；当前 `clang-cl` 配置不能被 CMake 用于模块依赖扫描。

### 3. 最简单的 Desktop 启动

`fyuu_desktop::Run` 会创建具体的 Desktop `Platform` 和 `Runtime`，运行应用，并按相反顺序清理。

```cpp
import fyuu_desktop;
import fyuu_engine;

class StudioApplication final : public fyuu_engine::Application {
public:
    void Tick(fyuu_engine::Runtime& runtime) override {
        // 更新一帧。
    }

    bool CloseRequested(fyuu_engine::Runtime& runtime) override {
        // 返回 false 可以拒绝本次窗口关闭请求。
        return true;
    }
};

int main() {
    fyuu_desktop::Descriptor const descriptor{
        .title = "Fyuu Studio",
        .width = 1600,
        .height = 900,
        .resizable = true,
        .high_pixel_density = true
    };
    StudioApplication application;
    try {
        fyuu_desktop::Run(descriptor, application);
        return 0;
    }
    catch (fyuu_engine::Error const& error) {
        return 1;
    }
}
```

Desktop 调用链如下：

```text
fyuu_desktop::Run
  -> Platform 构造函数
  -> Runtime 构造函数（创建并初始化 ABI Runtime）
  -> Runtime::Run
     -> Runtime::Tick
        -> Platform::PumpEvents
        -> Application::CloseRequested（收到关闭请求时）
        -> Application::Tick
  -> Runtime、Platform 析构函数
```

### 4. 手动创建和运行

需要控制初始化与主循环时，显式创建每一层：

```cpp
fyuu_desktop::Descriptor const descriptor{
    .title = "Runtime Example",
    .width = 1280,
    .height = 720
};

StudioApplication application;

try {
    fyuu_desktop::Platform platform{ descriptor };
    fyuu_engine::Runtime runtime{ platform, application };
    while (runtime.GetState() != fyuu_engine::RuntimeState::Stopped) {
        runtime.Tick();
    }
}
catch (fyuu_engine::Error const& error) {
    runtime.RequestStop();
    return 1;
}
```

构造函数完成全部创建和初始化工作。对象构造成功后可以直接调用 `Run` 或 `Tick`，C++ API 不再暴露 `Create`、`Initialize`、`Shutdown` 或 `Destroy`。

### 5. Runtime 状态

Runtime 状态按以下方向转换：

```text
Created -> Running -> StopRequested -> Stopped
```

- `Created`：C ABI 创建完成后的内部过渡状态；C++ Runtime 构造函数不会在此状态返回。
- `Running`：Runtime 构造完成，可以执行 `Tick`。
- `StopRequested`：已经请求停止，下一次状态处理会执行应用关闭。
- `Stopped`：应用关闭完成，不能再执行普通更新。

`Runtime::RequestStop` 只把 `Running` 转换为 `StopRequested`。Runtime 析构函数会完成尚未执行的 C ABI 停止流程。应用自身资源由 Application 的构造和析构管理。

### 6. 每帧调用链

每次 `Runtime::Tick` 执行以下步骤：

1. 如果状态已经是 `StopRequested`，执行应用关闭并进入 `Stopped`。
2. 调用 `Platform::PumpEvents`。
3. 如果平台报告关闭请求，调用 `Application::CloseRequested`。
4. 如果应用接受关闭，把状态改为 `StopRequested`。
5. 状态仍为 `Running` 时调用 `Application::Tick`。
6. 本帧产生停止请求时进入 `Stopped`。

模块入口与 C ABI 共用以下调用链：

```text
C++ Platform/Runtime
  -> C ABI 函数
  -> fyuu_engine:entry 状态机
  -> C ABI callback
  -> 生成的 fyuu_engine:api_glue thunk
  -> Platform/Application
```

### 7. 所有权与生命周期

`Platform` 和 `Runtime` 都是不可复制、不可移动的拥有对象。这一点保证生成的 callback thunk 中保存的对象地址稳定。

- `Platform` 拥有自己的 ABI 句柄；具体适配器同时拥有原生平台资源。
- `Runtime` 不拥有 `Platform`。
- `Runtime` 不拥有 `Application`。
- Descriptor 会被 C ABI 对象复制，但其中的 `user_data` 仍是非拥有指针。
- 借用对象在对应拥有对象销毁前不能移动或析构。

Desktop 的 `Descriptor::title` 使用 `std::string`，由 Descriptor 自己拥有。窗口尺寸使用 `std::uint32_t`，只在 SDL 边界检查后转换成 `int`。

### 8. 关闭和错误语义

- C++ 构造函数、`Tick` 和 `Run` 成功时不返回值，失败时抛出 `fyuu_engine::Error`。
- `Error::Code()` 返回稳定的 `Result`；`what()` 返回错误消息。
- 平台初始化或事件泵失败时抛出 `Error{ PlatformError }`。
- 参数为空、窗口尺寸非法或对象关系非法时抛出 `Error{ InvalidArgument }`。
- 重复创建、重复初始化或在错误状态调用接口时抛出 `Error{ InvalidState }`。
- 分配 opaque ABI 对象失败时抛出 `Error{ OutOfMemory }`。
- `Application::CloseRequested` 默认返回 `true`；覆盖后可以实现未保存文档确认。
- `Application::Tick`、`CloseRequested` 和 `Platform::PumpEvents` 可以抛异常；生成 thunk 会在 ABI 边界捕获并由外层 C++ 调用重新抛出。

`Result` 是 `Fyuu_Result` 的类型安全错误码映射，只通过 `Error::Code()` 查询，不作为 C++ 操作的成功/失败返回值。数值由 `conf/runtime_api.yaml` 统一生成。不要手工修改生成的 ABI 头或生成分区。

### 9. C ABI 启动顺序

非 C++ 调用方使用顶层 `include` 中的 ABI 头。顺序与 C++ 入口一致：

1. 填写 `Fyuu_PlatformDescriptor`。
2. 调用 `Fyuu_PlatformCreate`，得到 `Fyuu_Platform*`。
3. 填写 `Fyuu_RuntimeDescriptor`。
4. 调用 `Fyuu_RuntimeCreate`，得到 `Fyuu_Runtime*`。
5. 调用 `Fyuu_RuntimeInitialize`。
6. 循环调用 `Fyuu_RuntimeTick`，直到 `Fyuu_RuntimeGetState` 返回 `FYUU_RUNTIME_STATE_STOPPED`。
7. 调用 `Fyuu_RuntimeDestroy`。
8. 调用 `Fyuu_PlatformDestroy`。

```c
Fyuu_Platform* platform = NULL;
Fyuu_Runtime* runtime = NULL;

Fyuu_PlatformDescriptor platform_descriptor = {
    .struct_size = sizeof(Fyuu_PlatformDescriptor),
    .ABI_version = FYUU_ABI_VERSION,
    .user_data = platform_state,
    .initialize = PlatformInitialize,
    .pump_events = PlatformPumpEvents,
    .shutdown = PlatformShutdown
};

Fyuu_RuntimeDescriptor runtime_descriptor = {
    .struct_size = sizeof(Fyuu_RuntimeDescriptor),
    .ABI_version = FYUU_ABI_VERSION,
    .user_data = application_state,
    .initialize = ApplicationInitialize,
    .tick = ApplicationTick,
    .close_requested = ApplicationCloseRequested,
    .shutdown = ApplicationShutdown
};

if (Fyuu_PlatformCreate(&platform_descriptor, &platform) != FYUU_RESULT_SUCCESS) {
    return 1;
}
if (Fyuu_RuntimeCreate(platform, &runtime_descriptor, &runtime) != FYUU_RESULT_SUCCESS) {
    Fyuu_PlatformDestroy(platform);
    return 1;
}
```

回调和 `user_data` 指向的状态必须存活到对应对象销毁。所有 ABI 回调都带 `NOEXCEPT`，C++ 定义必须使用 `noexcept(true)`。

### 10. 日志入口

日志系统不使用全局单例。调用方先实现 `fyuu_engine::LogSink`，再用它构造 `fyuu_engine::Logger`：

```cpp
class ConsoleSink final : public fyuu_engine::LogSink {
public:
    void Write(fyuu_engine::LogRecord const& record) override {
        // 同步消费 record；其中的字符串视图只在本次调用期间有效。
    }
};

ConsoleSink sink;
fyuu_engine::Logger logger{ sink };
logger.Write(fyuu_engine::LogLevel::Info, "Runtime", "Runtime started");
```

调用链为 `Logger::Write -> Fyuu_LoggerWrite -> C ABI sink callback -> 生成 thunk -> LogSink::Write`。Logger 拥有 opaque ABI 对象，但只借用 Sink，因此 Sink 必须后析构。Sink 抛出的异常由 `noexcept` thunk 捕获，并在同一次 C++ `Write` 返回 ABI 后重新抛出。

C 和其他语言通过 `fyuu_log.h` 创建 `Fyuu_Logger` 并提供 `Fyuu_LogSinkCallback`。`Fyuu_LogRecord` 的分类和消息均为借用的指针加长度，只保证在回调期间有效；需要保留时必须由 Sink 复制。

### 11. 生成和验证

ABI 与机械桥接代码由 YAML 生成：

```text
conf/runtime_api.yaml
  -> include/fyuu_*.h
  -> runtime/module/generated/api_types.cppm
  -> runtime/module/internal/generated/api_glue.cpp
```

```powershell
python script/generate_runtime_api.py
python script/generate_runtime_api.py --check
```

修改 Runtime 后应同时完成 MSVC 与 Clang 的配置、编译和链接验证。Clang 使用 `C:\Program Files\LLVM\bin\clang++.exe` 与 `clang-scan-deps.exe`。

---

## English

### 1. Architecture and startup order

FyuuRuntime separates platform behavior from application behavior. Platform and Application manage their resources in constructors and destructors; their Runtime-facing operations are event pumping, ticking, and close confirmation. Platform owns the opaque C platform object; Runtime owns the opaque C runtime object.

Create objects in this order:

1. Concrete `Platform`; its base constructor creates the ABI Platform and its concrete constructor acquires native resources.
2. Concrete `Application`.
3. Construct `Runtime` from Platform and Application; its constructor creates and initializes the ABI Runtime.
4. Call `Runtime::Run()`, or repeatedly call `Tick()`.
5. Destroy Runtime before Application and Platform.

Recommended local declaration order:

```text
Platform -> Application -> Runtime
```

Reverse destruction then produces the required lifetime:

```text
Runtime -> Application -> Platform
```

### 2. Build and import

Link the platform-independent core and import its primary module:

```cmake
target_link_libraries(MyTarget PRIVATE FyuuEngine)
```

```cpp
import fyuu_engine;
```

Desktop applications link and import the separate SDL3 adapter:

```cmake
target_link_libraries(MyTarget PRIVATE FyuuDesktop)
```

```cpp
import fyuu_desktop;
import fyuu_engine;
```

### 3. Desktop entry

`fyuu_desktop::Run` constructs and destroys the complete platform/runtime chain:

```cpp
fyuu_desktop::Descriptor const descriptor{
    .title = "Fyuu Studio",
    .width = 1600,
    .height = 900
};

StudioApplication application;
try {
    fyuu_desktop::Run(descriptor, application);
    return 0;
}
catch (fyuu_engine::Error const& error) {
    return 1;
}
```

The call chain is:

```text
fyuu_desktop::Run
  -> Platform constructor
  -> Runtime constructor
  -> Runtime::Run
     -> Platform::PumpEvents
     -> Application::CloseRequested
     -> Application::Tick
  -> Runtime and Platform destructors
```

### 4. Manual loop

Use the explicit lifecycle when the host owns the outer loop:

```cpp
StudioApplication application;

try {
    fyuu_desktop::Platform platform{ descriptor };
    fyuu_engine::Runtime runtime{ platform, application };
    while (runtime.GetState() != fyuu_engine::RuntimeState::Stopped) {
        runtime.Tick();
    }
}
catch (fyuu_engine::Error const& error) {
    return 1;
}
```

Construction performs all creation and initialization. The C++ API does not expose `Create`, `Initialize`, `Shutdown`, or `Destroy` operations.

### 5. State machine and tick order

The shared state machine is:

```text
Created -> Running -> StopRequested -> Stopped
```

Each tick pumps platform events, negotiates close requests with the application, runs the application tick while still Running, and completes shutdown when stopping was requested.

Both entry surfaces use the same chain:

```text
C++ Platform/Runtime
  -> C ABI function
  -> fyuu_engine:entry state machine
  -> C ABI callback
  -> generated fyuu_engine:api_glue thunk
  -> Platform/Application
```

### 6. Ownership

`Platform` and `Runtime` are non-copyable and non-movable so callback addresses remain stable. They borrow rather than own their collaborators:

- Platform owns its ABI handle; a concrete adapter also owns its native resources.
- Runtime borrows Platform.
- Runtime borrows Application.
- ABI descriptors are copied, but `user_data` remains non-owning.

Every borrowed object must remain alive and unmoved until its owner is destroyed.

### 7. C ABI order

C and other language bindings follow the same order:

1. Fill `Fyuu_PlatformDescriptor` and call `Fyuu_PlatformCreate`.
2. Fill `Fyuu_RuntimeDescriptor` and call `Fyuu_RuntimeCreate`.
3. Call `Fyuu_RuntimeInitialize`.
4. Call `Fyuu_RuntimeTick` until Stopped.
5. Call `Fyuu_RuntimeDestroy`.
6. Call `Fyuu_PlatformDestroy`.

Callbacks and their `user_data` must outlive the corresponding opaque object. No exception may cross an ABI callback.

### 8. Errors and generation

C++ constructors, `Tick`, and `Run` return no success value. They throw `fyuu_engine::Error` on failure; `Error::Code()` maps the stable `Fyuu_Result` value and `what()` provides the message. The C ABI continues to expose explicit creation, initialization, shutdown, and `Fyuu_Result` values for language bindings.

The ABI and mechanical bridge are generated from `conf/runtime_api.yaml`:

```powershell
python script/generate_runtime_api.py
python script/generate_runtime_api.py --check
```

Do not edit generated headers or generated module partitions directly. Validate Runtime changes with both MSVC and `C:\Program Files\LLVM\bin\clang++.exe`.

### Logging entry

Logging is instance-based rather than global. Implement `fyuu_engine::LogSink`, construct `fyuu_engine::Logger` from it, and keep the sink alive until the logger is destroyed. `Logger::Write` synchronously follows `C++ Logger -> C ABI -> generated thunk -> LogSink`. Category and message views are borrowed only for that call. A native sink exception is caught by the `noexcept` ABI thunk and rethrown by the same C++ `Write` call.

C and other language bindings use `fyuu_log.h`, create an opaque `Fyuu_Logger`, and provide a `Fyuu_LogSinkCallback`. They must copy record data inside the callback if it needs to outlive the call.
