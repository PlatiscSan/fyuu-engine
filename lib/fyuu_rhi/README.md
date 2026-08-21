# FyuuRHI

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

`PhysicalDevice::BestPerformance()` provides the default adapter preference.
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

## 中文摘要

FyuuRHI 是 FyuuEngine 的底层渲染硬件接口，统一提供设备枚举、资源与管线创建、
命令图执行、同步及呈现。应用导入 `fyuu_rhi` 主模块，并链接 `FyuuRHI` 目标。

当前经过 Windows 构建和运行验证的后端为 Direct3D 12、Vulkan、OpenGL 和
WebGPU；Linux 路径仍需在对应平台验证。

**Metal 目前只有初始实现，尚未经过 Apple 工具链编译、Apple GPU 运行、真实
AppKit 窗口、事件循环、呈现、缩放和错误诊断验证，因此必须标记为未验证，不能
视为生产可用后端。**

标准使用流程：

1. 调用 `InitializeRHIContext()` 设置应用信息与日志接收器。
2. 通过 `EnumerateBackends()` 查询当前构建实际包含的后端。
3. 使用 `RequestInstance()` 创建实例并枚举物理设备。
4. 创建逻辑设备、资源、采样器、管线和调度器。
5. 由 Resource 创建 View，由 Pipeline 创建 `PipelineResourceGroup`。
6. 创建命令图，准确声明资源访问、节点依赖和逻辑队列能力。
7. 将绑定对象移动进 operation，设置呈现目标并调用 `start()`。
8. 在 receiver 的成功、失败或取消路径中取回绑定对象。

资源访问声明是正确性契约，必须与实际命令一致。`QueueType` 仅表达能力需求，
不保证独立物理队列。窗口和事件循环由应用管理，RHI 不接管窗口生命周期。

完整测试位于 `lib/fyuu_rhi/test/hello_triangle/main.cpp`。测试通过必须同时满足：
进程成功退出、没有 Error/Fatal 后端日志、实际完成 Draw 和 Present、resize 后仍能
继续呈现、完成回调正确归还绑定。Metal 在 Apple 硬件完成这些验证前始终保持
“未验证”状态。
