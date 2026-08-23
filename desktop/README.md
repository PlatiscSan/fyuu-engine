# FyuuDesktop 使用指南 / FyuuDesktop User Guide

FyuuDesktop 是 FyuuEngine 的 SDL3 桌面平台适配器。它实现 `fyuu_engine::Platform`，负责桌面视频子系统、主窗口和事件泵，但不拥有应用、不实现 Runtime 状态机，也不负责 RHI、渲染或 Studio UI。

FyuuDesktop is the SDL3 desktop platform adapter for FyuuEngine. It implements `fyuu_engine::Platform` and owns the desktop video subsystem, main window, and event pump. It does not own the application, implement the Runtime state machine, or provide RHI, rendering, or Studio UI behavior.

- [中文](#中文)
- [English](#english)

---

## 中文

### 1. 设计边界

`FyuuDesktop` 是独立于 `FyuuEngine` 的 C++23 Modules 库：

- `FyuuEngine` 提供平台无关的 `Platform`、`Runtime` 和生命周期状态机。
- `FyuuDesktop` 使用 SDL3 实现 `Platform`。
- `FyuuStudio` 使用 `FyuuDesktop` 启动桌面窗口。
- `FyuuRHI` 不由 Desktop 初始化，也不会被 Desktop 隐式链接。

调用链如下：

```text
fyuu_engine::Runtime::Tick
  -> C ABI Runtime 状态机
  -> 平台 callback
  -> 生成的 thunk
  -> fyuu_desktop::Platform::PumpEvents
  -> SDL_PollEvent
```

这种拆分使无窗口工具、服务器和测试只链接 `FyuuEngine`，不必依赖 SDL3。

### 2. 构建与导入

顶层 CMake 选项控制 Desktop 目标：

```cmake
option(FYUU_BUILD_DESKTOP "Build the SDL3 desktop platform adapter" ON)
```

链接库并导入模块：

```cmake
target_link_libraries(MyTarget PRIVATE FyuuDesktop)
```

```cpp
import fyuu_desktop;
import fyuu_engine;
```

`FyuuDesktop` 公开传递 `FyuuEngine` 链接依赖，私有链接 `SDL3::SDL3`。公共模块接口不暴露 `SDL_Window*`，SDL 类型只存在于内部实现分区。

`Platform::GetPresentationTarget` 返回一个 `PresentationTarget` variant。它包含当前平台对应的 Win32、X11、Wayland 或 Cocoa 目标，供上层转换为 RHI presentation handle；该接口不暴露 SDL 窗口对象。

### 3. 窗口描述符

`Descriptor` 是全公开数据结构：

```cpp
fyuu_desktop::Descriptor const descriptor{
    .title = "Fyuu Studio",
    .width = 1600,
    .height = 900,
    .resizable = true,
    .high_pixel_density = true
};
```

字段含义：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `title` | `std::string` | 主窗口标题，由 Descriptor 自己拥有 |
| `width` | `std::uint32_t` | 初始逻辑宽度 |
| `height` | `std::uint32_t` | 初始逻辑高度 |
| `resizable` | `bool` | 是否添加 SDL 可调整尺寸标志 |
| `high_pixel_density` | `bool` | 是否请求高像素密度窗口 |

宽度和高度必须大于零，并且不能超过 `int` 的最大值。只有调用 `SDL_CreateWindow` 时才会在检查后转换成 `int`。

### 4. 快捷启动

普通桌面应用优先使用 `fyuu_desktop::Run`：

```cpp
class DesktopApplication final : public fyuu_engine::Application {
public:
    void Tick(fyuu_engine::Runtime& runtime) noexcept override {
        // 更新应用。
    }
};

int main() {
    fyuu_desktop::Descriptor const descriptor{
        .title = "Desktop Application",
        .width = 1280,
        .height = 720
    };
    DesktopApplication application;
    fyuu_engine::ConsoleLogSink log_sink;
    fyuu_engine::Logger logger{ log_sink };
    fyuu_desktop::EventSink event_sink;
    try {
        fyuu_desktop::Run(descriptor, logger, event_sink, application);
        return 0;
    }
    catch (fyuu_engine::Error const& error) {
        return 1;
    }
}
```

`Run` 内部按以下顺序工作：

1. 创建 `fyuu_desktop::Platform`。
2. 使用 Platform 和 Application 构造并初始化 `fyuu_engine::Runtime`。
3. 调用 `Runtime::Run`。
4. 依次析构 Runtime 和 Platform。

`Application` 由调用方拥有，必须在 `Run` 返回前保持存活且不能移动。

### 5. 手动启动

需要自己控制主循环时，直接创建 Platform：

```cpp
DesktopApplication application;
fyuu_engine::ConsoleLogSink log_sink;
fyuu_engine::Logger logger{ log_sink };
fyuu_desktop::EventSink event_sink;

try {
    fyuu_desktop::Platform platform{ descriptor, event_sink };
    fyuu_engine::Runtime runtime{ platform, logger, application };
    while (runtime.GetState() != fyuu_engine::RuntimeState::Stopped) {
        runtime.Tick();
    }
}
catch (fyuu_engine::Error const& error) {
    runtime.RequestStop();
    return 1;
}
```

声明顺序必须保证 `Runtime` 先析构、`Platform` 后析构。更完整的 Runtime 状态说明见 `runtime/README.md`。

### 6. 初始化和清理

`Platform` 构造函数按以下顺序执行：

1. 检查是否已经初始化。
2. 检查标题和窗口尺寸。
3. 取得进程内共享的 SDL Video 引用；首个 Platform 调用 `SDL_InitSubSystem(SDL_INIT_VIDEO)`。
4. 根据 Descriptor 组合 SDL 窗口标志。
5. 调用 `SDL_CreateWindow`。

窗口创建失败时会立即归还当前实例取得的 SDL Video 引用。只有最后一个 Platform 归还引用时才调用 `SDL_QuitSubSystem(SDL_INIT_VIDEO)`。

`Platform` 析构函数销毁窗口并归还本对象持有的 SDL Video 引用。内部使用原子引用计数判断首个取得和最后一个释放；Platform 仍应遵循 SDL 要求，在桌面主线程创建和销毁。正常路径必须先析构 Runtime，再析构 Platform。

### 7. 事件和关闭请求

`Platform::PumpEvents` 会读取当前所有待处理 SDL 事件。以下事件会请求关闭：

- `SDL_EVENT_QUIT`
- 属于主窗口的 `SDL_EVENT_WINDOW_CLOSE_REQUESTED`

Desktop 只把请求写入 `close_requested`，不直接销毁窗口。Runtime 随后调用 `Application::CloseRequested`：

```text
SDL close event
  -> Platform::PumpEvents
  -> Runtime 发现 close_requested
  -> Application::CloseRequested
  -> true: StopRequested
  -> false: 继续 Running
```

这样 Studio 可以在关闭窗口前显示未保存文档确认。

Platform 会把窗口尺寸、像素尺寸、焦点、鼠标、键盘和文本输入转换为 `fyuu_desktop::Event`，并在事件泵期间同步调用 `EventSink::ProcessEvent`。鼠标按键和键盘按键分别使用 `MouseButton` 与 `Key`，修饰键使用独立布尔状态，因此 SDL 常量不会越过 Desktop 边界。默认 EventSink 忽略事件，StudioFrontend 则覆盖它用于具体 UI 后端。

### 8. 所有权和线程约束

`Platform` 不可复制、不可移动。它在构造函数中获得并在析构函数中释放：

- Descriptor 的副本。
- SDL 主窗口。
- 一个进程内共享的 SDL Video 引用。

它不拥有 `fyuu_engine::Runtime` 或 `fyuu_engine::Application`。

当前实现假定初始化、事件泵和关闭在同一个桌面主线程执行。不要从其他线程调用 `PumpEvents` 或销毁 Platform。

### 9. 错误语义

- 标题为空、尺寸为零或超过 SDL `int` 范围：抛出 `Error{ InvalidArgument }`。
- SDL Video 初始化失败：抛出 `Error{ PlatformError }`。
- SDL 创建窗口失败：抛出 `Error{ PlatformError }`。
- 未初始化时调用事件泵：抛出 `Error{ InvalidState }`。

Platform 构造和事件泵可以抛异常，析构函数为 `noexcept`。生成 thunk 会在 `noexcept` C ABI 回调内暂存异常，外层 `Runtime::Tick` 随即重新抛出，因此异常不会跨越 C ABI。

### 10. 当前不负责的功能

当前 FyuuDesktop 只实现最小启动闭环，暂不提供：

- 尚未列入 `Key` 的完整键盘按键集合。
- 最小化和 DPI 比例状态通知。
- 多窗口管理。
- 文件选择、剪贴板、拖放和系统对话框。
- Android、Web 或其他非桌面入口。

这些能力应在保持 Runtime 平台无关的前提下逐步加入 Desktop，不应重新塞回 `FyuuEngine`。

---

## English

### 1. Scope

FyuuDesktop is a separate C++23 Modules target that adapts SDL3 to `fyuu_engine::Platform`. FyuuEngine remains platform-independent, and applications that do not need a window do not need to link SDL3.

FyuuDesktop owns SDL video initialization, one main window, and the event pump. It does not own the application, implement the Runtime state machine, initialize FyuuRHI, or provide Studio UI behavior.

### 2. Build and import

Enable the target, link it, and import both modules:

```cmake
set(FYUU_BUILD_DESKTOP ON)
target_link_libraries(MyTarget PRIVATE FyuuDesktop)
```

```cpp
import fyuu_desktop;
import fyuu_engine;
```

SDL3 is a private implementation dependency. No SDL type is exposed by the public module.

`Platform::GetPresentationTarget` returns a `PresentationTarget` variant containing the active Win32, X11, Wayland, or Cocoa target. Upper layers may adapt that value to an RHI presentation handle without exposing the SDL window object.

### 3. Descriptor

```cpp
fyuu_desktop::Descriptor const descriptor{
    .title = "Desktop Application",
    .width = 1280,
    .height = 720,
    .resizable = true,
    .high_pixel_density = true
};
```

The title is an owned `std::string`. Width and height are `std::uint32_t` values and are range-checked before conversion at the SDL boundary.

### 4. Convenience entry

Use `fyuu_desktop::Run` for the standard blocking desktop lifecycle:

```cpp
DesktopApplication application;
fyuu_engine::ConsoleLogSink log_sink;
fyuu_engine::Logger logger{ log_sink };
fyuu_desktop::EventSink event_sink;
try {
    fyuu_desktop::Run(descriptor, logger, event_sink, application);
    return 0;
}
catch (fyuu_engine::Error const& error) {
    return 1;
}
```

The helper performs:

```text
Platform constructor
  -> Runtime constructor
  -> Runtime::Run
  -> Runtime destruction
  -> Platform destruction
```

The caller owns Application and must keep it alive and unmoved until `Run` returns.

### 5. Manual lifecycle

Construct the objects explicitly when the host controls the outer loop:

```cpp
DesktopApplication application;
fyuu_engine::ConsoleLogSink log_sink;
fyuu_engine::Logger logger{ log_sink };
fyuu_desktop::EventSink event_sink;

try {
    fyuu_desktop::Platform platform{ descriptor, event_sink };
    fyuu_engine::Runtime runtime{ platform, logger, application };
    while (runtime.GetState() != fyuu_engine::RuntimeState::Stopped) {
        runtime.Tick();
    }
}
catch (fyuu_engine::Error const& error) {
    return 1;
}
```

Declare Platform and Application before Runtime so reverse destruction preserves both borrowed lifetimes.

### 6. Events and shutdown

`PumpEvents` treats `SDL_EVENT_QUIT` and a main-window `SDL_EVENT_WINDOW_CLOSE_REQUESTED` as close requests. It does not destroy the window. Runtime asks `Application::CloseRequested`, allowing the application to reject closure.

Platform normalizes window size, pixel size, focus, mouse, keyboard, and text input into `fyuu_desktop::Event`, then synchronously calls `EventSink::ProcessEvent` during the event pump. Mouse and keyboard values use the backend-independent `MouseButton` and `Key` enums, with explicit modifier states, so no SDL constant crosses the public module boundary. The default sink ignores events; StudioFrontend overrides it for a concrete UI backend.

The first Platform initializes SDL Video and each Platform owns one internally reference-counted claim. A destructor destroys its window and releases only that claim; SDL Video is shut down after the final claim is released.

### 7. Ownership and errors

Platform is non-copyable and non-movable. It owns its ABI handle, Descriptor copy, SDL window, and one shared SDL Video reference. Runtime borrows Platform and Application.

Invalid descriptors throw `Error{ InvalidArgument }`, invalid lifecycle calls throw `Error{ InvalidState }`, and SDL failures throw `Error{ PlatformError }`. Generated thunks catch these exceptions at the ABI boundary, and the outer C++ Runtime call rethrows them; no exception crosses a C callback.

### 8. Current limitations

The initial adapter still lacks the complete keyboard key set, minimized/DPI scale notifications, multiple windows, file dialogs, clipboard integration, drag and drop, and non-desktop platform entry. These features belong in FyuuDesktop rather than the platform-independent Runtime core.
