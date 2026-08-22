# FyuuRHI

[English](#english) | [简体中文](#简体中文)

## English

FyuuRHI is FyuuEngine's low-level rendering hardware interface. It provides a
backend-neutral API for device discovery, resource and pipeline creation,
command-graph execution, synchronization, and presentation.

FyuuRHI uses C++23 modules. Applications consume the public module with:

```cpp
import fyuu_rhi;
```

> The API is under active development. A backend being compiled into the library
> does not mean that it has been validated on the current platform.

## Backend status

| Backend | Platform | Build status | Runtime status | Notes |
| --- | --- | --- | --- | --- |
| Direct3D 12 | Windows | Verified | Verified | Native graphics, compute, copy, and presentation paths |
| Vulkan | Windows, Linux | Verified on Windows | Verified on Windows | Linux still requires platform validation |
| OpenGL | Windows, Linux | Verified on Windows | Verified on Windows | WGL, GLX, and EGL platform paths are present |
| WebGPU | Windows, Linux, Apple | Verified on Windows | Verified on Windows | Uses Dawn; its native backend is platform dependent |
| Metal | macOS/iOS | **Unverified** | **Unverified** | Initial implementation only; no Apple build or runtime validation has been completed |

Metal is experimental. It must not be treated as production-ready until it
passes an Apple toolchain build and real-device rendering, presentation, resize,
diagnostic, and shutdown tests.

## Architecture

FyuuRHI separates rendering into these object layers:

1. `Instance` discovers physical devices for one backend.
2. `PhysicalDevice` describes an adapter and creates a `LogicalDevice`.
3. `LogicalDevice` creates resources, samplers, pipelines, and schedulers.
4. `Resource` creates its buffer and texture views.
5. `Pipeline` creates resource groups compatible with its reflected interface.
6. `CommandScheduler` compiles and executes command graphs.
7. `CompletionToken` tracks submitted GPU work.

Public objects contain opaque backend data. Applications do not access native
objects or downcast between backend implementations.

## Build integration

Link the CMake target and enable C++23 module support in the consumer:

```cmake
target_link_libraries(MyApplication PRIVATE FyuuRHI)
```

Backends are selected according to the target platform and build dependencies.
Never assume that a backend is available; query the current build:

```cpp
for (auto backend : fyuu_rhi::EnumerateBackends()) {
    // Apply the application's backend-selection policy.
}
```

## Initialization and device selection

Initialize the process-wide RHI context before requesting an instance. The log
sink must remain valid while RHI operations can emit diagnostics.

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);
```

Create an instance and select a physical device:

```cpp
fyuu_rhi::RequestInstance(
    fyuu_rhi::Backend::Vulkan,
    [](fyuu_rhi::Instance instance) {
        auto adapters = instance.EnumeratePhysicalDevices();
        if (adapters.empty()) {
            throw std::runtime_error("No compatible GPU was found");
        }

        auto logical_device = adapters.front().CreateLogicalDevice();
        auto scheduler = logical_device.CreateScheduler();
    }
);
```

`PhysicalDevice::GetInfo()` reports the adapter name, type, optional vendor and
device identifiers, and optional dedicated-memory information. Missing optional
properties are unavailable data, not zero-valued properties.

`fyuu_rhi::BestPerformance()` provides the default adapter preference.
Applications may replace it with their own selection policy.

OpenGL callers can bind the instance's shared context on another thread with:

```cpp
instance.ShareContextOnThisThread();
```

The operation is empty for backends that do not require GL context affinity.

## Ownership and lifetime

GPU objects use move-oriented ownership. `PhysicalDevice`, `LogicalDevice`,
`Resource`, `View`, `Sampler`, `Pipeline`, `PipelineResourceGroup`, and
`CompletionToken` are owning objects. `CommandScheduler` is copyable and shares
its scheduling context.

Starting a graph operation transfers its bound objects into the operation. They
remain alive until success, failure, or cancellation, after which the receiver
gets them back in `CommandGraphResources`.

- Do not access an object after moving it into an operation.
- Do not destroy a device while objects created from it are still alive.
- Keep every resource required by recorded work bound for the operation.
- Each recovered binding slot may be taken only once.

## Resources and views

`ResourceFlags` describes usage, memory placement, format, dimensionality,
sample count, and view intent. Supply a complete, internally consistent set;
backends reject invalid or unsupported combinations.

```cpp
fyuu_rhi::ResourceFlags flags;
flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
flags.Set(fyuu_rhi::ResourceFlagBits::TextureViewAspectAll);
flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
flags.Set(fyuu_rhi::ResourceFlagBits::Sample1);
flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);

auto texture = logical_device.CreateTexture(
    width,
    height,
    1u,
    1u,
    flags
);

auto view = texture.CreateTextureView(
    0u,
    1u,
    0u,
    1u,
    flags
);
```

Views are created by their source resource, keeping backend and device
validation inside the implementation. Buffer views use
`Resource::CreateBufferView()`.

Declared usages are correctness requirements. A buffer updated by `WriteBuffer`
must include `CopyDST`; `VertexBuffer` alone does not authorize an upload.

## Shaders, pipelines, and resource groups

Shaders are supplied through `pipeline::SlangPipelineProgramDescriptor`.
FyuuRHI compiles the declared Slang modules and reflects their resource
interface. Graphics pipelines additionally declare vertex input, topology,
rasterization, multisampling, depth/stencil state, blending, and attachment
formats.

```cpp
auto pipeline = logical_device.CreateGraphicsPipeline(
    {
        .program = {
            .modules = shader_modules,
            .entry_points = entry_points
        },
        .vertex = {
            .buffers = vertex_buffers,
            .attributes = vertex_attributes
        },
        .color_targets = color_targets
    }
);
```

A resource group belongs to the pipeline that created it. Its space, slots,
array elements, and binding kinds must match the reflected interface:

```cpp
std::array bindings{
    fyuu_rhi::pipeline::ResourceBinding{
        .slot = 0u,
        .array_element = 0u,
        .value = fyuu_rhi::pipeline::BindingValue::FromCombined(
            texture_view,
            sampler
        )
    }
};

auto group = pipeline.CreatePipelineResourceGroup(0u, bindings);
```

Objects created by different backends or logical devices must not be mixed.

## Command graphs

A command graph contains binding slots, dependency-ordered nodes, resource
access declarations, logical queue requirements, and commands.

```cpp
using namespace fyuu_rhi::execution;

auto builder = scheduler.schedule();
auto target_resource = builder.RegisterResource();
auto target_view = builder.RegisterView();
auto graphics_pipeline = builder.RegisterPipeline();

auto render = builder.CreateNode(QueueType::Graphics);
render
    .Access(
        {
            target_resource,
            AccessMode::Write,
            ResourceUsage::ColorAttachment,
            {}
        }
    )
    .Record(BindPipeline{ graphics_pipeline })
    .Record(
        BeginRendering{
            .area = { 0, 0, width, height },
            .colors = {
                {
                    .resource = target_resource,
                    .view = target_view,
                    .clear = { 0.02f, 0.03f, 0.04f, 1.0f }
                }
            }
        }
    )
    .Record(Viewport{ 0.0f, 0.0f, float(width), float(height) })
    .Record(Scissor{ 0, 0, width, height })
    .Record(Draw{ 3u, 1u, 0u, 0u })
    .Record(EndRendering{});
```

Access declarations are part of the correctness contract, not optimization
hints. They must match actual command use. Backends use them for batching,
resource transitions, memory barriers, queue synchronization, and ownership
transfers.

`QueueType` specifies the capability a node requires. It does not promise a
dedicated physical queue or concurrent execution.

## Presentation

Presentation is graph work and normally depends on the rendering node:

```cpp
auto present = builder.CreateNode(QueueType::Present, render);
present
    .Access(
        {
            target_resource,
            AccessMode::Read,
            ResourceUsage::PresentationSource,
            {}
        }
    )
    .Record(
        Present{
            .source = target_resource,
            .target = 0u,
            .buffer_count = 3u,
            .vertical_sync = true
        }
    );
```

Bind native targets with `SetPresentationTarget()`. The application owns the
window and event loop, processes platform events, handles extent changes, and
submits frames until the window closes. Presentation recovery is backend
specific; applications must tolerate temporary presentation failures.

Metal presentation is unverified. The existing Apple Hello Triangle path is not
yet a validated AppKit window and event-loop implementation.

## Connecting and completing work

```cpp
auto operation = std::move(builder).connect(Receiver{ state });
operation.BindResource(target_resource, std::move(texture));
operation.BindView(target_view, std::move(view));
operation.BindPipeline(graphics_pipeline, std::move(pipeline));
operation.SetPresentationTarget(native_window);
operation.start();
```

The receiver contract is:

- `get_env()` supplies the environment and optional stop token.
- `set_value(CommandGraphResources&&) &&` reports success.
- `set_error(std::exception_ptr) &&` reports failure.
- `set_stopped() &&` reports cancellation.
- `RecoverBindings(CommandGraphResources&&)` recovers bindings before an error
  or stopped completion is delivered.

Completion may run on an internal service thread. Synchronize receiver and
application state shared with that thread.

```cpp
auto texture = resources.TakeResource(target_resource);
auto view = resources.TakeView(target_view);
auto pipeline = resources.TakePipeline(graphics_pipeline);
```

## Execution and error model

Execution has three conceptual stages:

1. Validate bindings, prepare backend state, and reserve required objects.
2. Record native command buffers or command lists.
3. Submit work and publish completion state.

Errors are delivered through the operation's error channel. Recording and GPU
execution failures may be retained by a `CompletionToken`. Stop requests are
meaningful before native submission; submitted GPU work generally cannot be
cancelled.

Backend diagnostics are written to the configured `log::Sink`. Tests and
applications must treat Error and Fatal diagnostics as failures even if the
process exits with code zero.

## Threading rules

- A graph operation owns its bindings until completion.
- Applications synchronize concurrent access to their own state.
- Native queue and allocator synchronization is backend-managed.
- Completion callbacks may execute on a worker thread.
- Window APIs and OpenGL contexts retain platform thread-affinity rules.

## Integration test

The cross-backend Hello Triangle test belongs to FyuuRHI:

```text
lib/fyuu_rhi/test/hello_triangle/main.cpp
```

Interactive invocation:

```text
HelloTriangle <d3d12|vulkan|opengl|webgpu|metal>
```

Verified Windows backends exercise device creation, upload, pipeline and
resource-group creation, command recording, drawing, presentation, completion,
and resize handling. Automated mode uses `--test` and exits after a bounded
frame sequence.

The Metal argument is experimental. It is not considered passing until the test
builds and runs on Apple hardware with a real window and event loop, visible
presentation, resize handling, diagnostics, and clean shutdown.

---

## 简体中文

FyuuRHI 是 FyuuEngine 的底层渲染硬件接口，统一提供设备发现、资源与管线创建、
命令图执行、同步和呈现能力。

FyuuRHI 使用 C++23 Modules。应用通过以下方式导入公开模块：

```cpp
import fyuu_rhi;
```

> 当前 API 仍在持续开发。某个后端被编译进库中，不代表它已经在当前平台完成
> 验证。

### 后端状态

| 后端 | 平台 | 构建状态 | 运行状态 | 说明 |
| --- | --- | --- | --- | --- |
| Direct3D 12 | Windows | 已验证 | 已验证 | 原生图形、计算、复制和呈现路径 |
| Vulkan | Windows、Linux | Windows 已验证 | Windows 已验证 | Linux 仍需对应平台验证 |
| OpenGL | Windows、Linux | Windows 已验证 | Windows 已验证 | 已提供 WGL、GLX 和 EGL 平台路径 |
| WebGPU | Windows、Linux、Apple | Windows 已验证 | Windows 已验证 | 使用 Dawn，底层原生后端由平台决定 |
| Metal | macOS、iOS | **未验证** | **未验证** | 仅有初始实现，尚未完成 Apple 构建和运行验证 |

Metal 当前属于实验后端。在通过 Apple 工具链构建以及真实设备上的绘制、呈现、
resize、错误诊断和正常退出测试之前，不得将其视为生产可用后端。

### 架构

FyuuRHI 将渲染功能划分为以下对象层级：

1. `Instance` 为指定后端发现物理设备。
2. `PhysicalDevice` 描述适配器并创建 `LogicalDevice`。
3. `LogicalDevice` 创建资源、采样器、管线和调度器。
4. `Resource` 创建自身的缓冲区 View 或纹理 View。
5. `Pipeline` 创建与反射接口兼容的资源组。
6. `CommandScheduler` 编译并执行命令图。
7. `CompletionToken` 跟踪已经提交的 GPU 工作。

公开对象使用不透明的后端数据。应用不直接访问原生 API 对象，也不在后端类型
之间进行向下转型。

### 构建集成

应用需要链接 `FyuuRHI` CMake 目标，并启用 C++23 模块支持：

```cmake
target_link_libraries(MyApplication PRIVATE FyuuRHI)
```

实际可用后端取决于目标平台和构建依赖。不要假定某个后端一定存在，应查询当前
构建包含的后端：

```cpp
for (auto backend : fyuu_rhi::EnumerateBackends()) {
    // 按应用自身策略选择后端。
}
```

### 初始化与设备选择

请求实例前必须初始化进程级 RHI 上下文。只要 RHI 仍可能输出诊断，传入的日志
Sink 就必须保持有效。

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);
```

创建实例并选择物理设备：

```cpp
fyuu_rhi::RequestInstance(
    fyuu_rhi::Backend::Vulkan,
    [](fyuu_rhi::Instance instance) {
        auto adapters = instance.EnumeratePhysicalDevices();
        if (adapters.empty()) {
            throw std::runtime_error("No compatible GPU was found");
        }

        auto logical_device = adapters.front().CreateLogicalDevice();
        auto scheduler = logical_device.CreateScheduler();
    }
);
```

`PhysicalDevice::GetInfo()` 返回适配器名称、类型、可选的厂商和设备编号，以及可选
的专用显存信息。部分图形 API 无法提供全部字段；缺失的可选字段表示数据不可用，
而不是数值为零。

`fyuu_rhi::BestPerformance()` 提供库内默认的适配器优先级。应用也可以实现
自己的选择策略。

OpenGL 调用方可以在其他线程绑定实例的共享上下文：

```cpp
instance.ShareContextOnThisThread();
```

不需要 GL 上下文线程亲和性的后端会将该操作视为空操作。

### 所有权与生命周期

GPU 对象采用偏向移动的所有权模型。`PhysicalDevice`、`LogicalDevice`、`Resource`、
`View`、`Sampler`、`Pipeline`、`PipelineResourceGroup` 和 `CompletionToken` 都是
持有所有权的对象。`CommandScheduler` 可以复制，并共享调度上下文。

命令图 operation 启动时，所有绑定对象的所有权都会移动到 operation 内部。这些
对象会一直存活到成功、失败或取消，随后 receiver 通过 `CommandGraphResources`
取回它们。

- 对象移动进 operation 后不得继续访问。
- 设备创建的对象仍然存活时，不得销毁设备。
- 命令执行所需的每个资源都必须在 operation 中保持绑定。
- 每个归还的绑定槽位只能取出一次。

### 资源与 View

`ResourceFlags` 描述用途、内存位置、格式、维度、采样数和 View 意图。调用方必须
提供完整且不矛盾的组合；后端会拒绝无效或不支持的组合。

```cpp
fyuu_rhi::ResourceFlags flags;
flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
flags.Set(fyuu_rhi::ResourceFlagBits::TextureViewAspectAll);
flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
flags.Set(fyuu_rhi::ResourceFlagBits::Sample1);
flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);

auto texture = logical_device.CreateTexture(
    width,
    height,
    1u,
    1u,
    flags
);

auto view = texture.CreateTextureView(
    0u,
    1u,
    0u,
    1u,
    flags
);
```

View 由来源 Resource 创建，使后端和设备归属验证保留在实现内部。缓冲区 View 使用
`Resource::CreateBufferView()` 创建。

资源用途声明属于正确性约束。例如，由 `WriteBuffer` 更新的缓冲区必须包含
`CopyDST`；只有 `VertexBuffer` 并不允许上传操作。

### Shader、Pipeline 与资源组

Shader 通过 `pipeline::SlangPipelineProgramDescriptor` 提供。FyuuRHI 编译声明的
Slang 模块并反射资源接口。图形 Pipeline 还需要声明顶点输入、拓扑、光栅化、
多重采样、深度模板、混合和附件格式。

```cpp
auto pipeline = logical_device.CreateGraphicsPipeline(
    {
        .program = {
            .modules = shader_modules,
            .entry_points = entry_points
        },
        .vertex = {
            .buffers = vertex_buffers,
            .attributes = vertex_attributes
        },
        .color_targets = color_targets
    }
);
```

资源组属于创建它的 Pipeline。space、slot、数组元素和绑定类型必须与 Shader 反射
接口一致：

```cpp
std::array bindings{
    fyuu_rhi::pipeline::ResourceBinding{
        .slot = 0u,
        .array_element = 0u,
        .value = fyuu_rhi::pipeline::BindingValue::FromCombined(
            texture_view,
            sampler
        )
    }
};

auto group = pipeline.CreatePipelineResourceGroup(0u, bindings);
```

不得混用由不同后端或不同逻辑设备创建的对象。

### 命令图

命令图由绑定槽位、具有依赖顺序的节点、资源访问声明、逻辑队列需求和命令组成。

```cpp
using namespace fyuu_rhi::execution;

auto builder = scheduler.schedule();
auto target_resource = builder.RegisterResource();
auto target_view = builder.RegisterView();
auto graphics_pipeline = builder.RegisterPipeline();

auto render = builder.CreateNode(QueueType::Graphics);
render
    .Access(
        {
            target_resource,
            AccessMode::Write,
            ResourceUsage::ColorAttachment,
            {}
        }
    )
    .Record(BindPipeline{ graphics_pipeline })
    .Record(
        BeginRendering{
            .area = { 0, 0, width, height },
            .colors = {
                {
                    .resource = target_resource,
                    .view = target_view,
                    .clear = { 0.02f, 0.03f, 0.04f, 1.0f }
                }
            }
        }
    )
    .Record(Viewport{ 0.0f, 0.0f, float(width), float(height) })
    .Record(Scissor{ 0, 0, width, height })
    .Record(Draw{ 3u, 1u, 0u, 0u })
    .Record(EndRendering{});
```

资源访问声明是正确性契约，不是优化提示，必须与命令的真实使用一致。后端根据
访问声明生成批次、资源状态转换、内存屏障、队列同步和所有权转移。

`QueueType` 表示节点需要的能力，不承诺独立物理队列或并发执行。

### 呈现

Present 是命令图工作，通常依赖产生呈现源的渲染节点：

```cpp
auto present = builder.CreateNode(QueueType::Present, render);
present
    .Access(
        {
            target_resource,
            AccessMode::Read,
            ResourceUsage::PresentationSource,
            {}
        }
    )
    .Record(
        Present{
            .source = target_resource,
            .target = 0u,
            .buffer_count = 3u,
            .vertical_sync = true
        }
    );
```

使用 `SetPresentationTarget()` 绑定平台原生呈现目标。应用负责窗口和事件循环，
必须处理平台事件、响应尺寸变化，并持续提交帧直到窗口关闭。呈现恢复行为取决于
具体后端，应用需要容忍临时呈现失败。

Metal 呈现路径尚未验证。现有 Apple Hello Triangle 路径还不构成经过验证的
AppKit 窗口和事件循环实现。

### 连接、启动与完成

```cpp
auto operation = std::move(builder).connect(Receiver{ state });
operation.BindResource(target_resource, std::move(texture));
operation.BindView(target_view, std::move(view));
operation.BindPipeline(graphics_pipeline, std::move(pipeline));
operation.SetPresentationTarget(native_window);
operation.start();
```

Receiver 契约包括：

- `get_env()` 提供 operation 环境和可选 stop token。
- `set_value(CommandGraphResources&&) &&` 报告成功。
- `set_error(std::exception_ptr) &&` 报告失败。
- `set_stopped() &&` 报告取消。
- `RecoverBindings(CommandGraphResources&&)` 在 error 或 stopped 完成前回收绑定。

完成回调可能在内部服务线程上执行。与该线程共享的 receiver 和应用状态必须进行
同步。

```cpp
auto texture = resources.TakeResource(target_resource);
auto view = resources.TakeView(target_view);
auto pipeline = resources.TakePipeline(graphics_pipeline);
```

### 执行与错误模型

命令执行在概念上分为三个阶段：

1. 验证绑定、准备后端状态并预约所需对象。
2. 录制原生命令缓冲或命令列表。
3. 提交工作并发布完成状态。

错误通过 operation 的 error 通道交付。录制和 GPU 执行错误可以保存在
`CompletionToken` 中。stop 请求在原生提交前有效；已经提交到 GPU 的工作通常
无法取消。

后端诊断写入配置的 `log::Sink`。即使进程退出码为零，测试和应用也必须将 Error
与 Fatal 日志视为失败。

### 线程规则

- 命令图 operation 在完成前拥有全部绑定对象。
- 应用负责同步自身状态的并发访问。
- 原生队列和分配器的同步由后端管理。
- 完成回调可能在工作线程执行。
- 窗口 API 和 OpenGL 上下文仍遵守各平台的线程亲和性约束。

### 集成测试

跨后端 Hello Triangle 测试属于 FyuuRHI：

```text
lib/fyuu_rhi/test/hello_triangle/main.cpp
```

交互运行：

```text
HelloTriangle <d3d12|vulkan|opengl|webgpu|metal>
```

已验证的 Windows 后端测试设备创建、上传、Pipeline 与资源组创建、命令录制、
绘制、呈现、完成处理和 resize。自动模式使用 `--test`，在有限帧序列后退出。

Metal 参数属于实验入口。在 Apple 硬件上完成真实窗口与事件循环、可见呈现、
resize、诊断和正常退出验证前，不得将它计为测试通过。
