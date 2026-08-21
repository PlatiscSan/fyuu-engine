# FyuuRHI

FyuuRHI 是基于 C++23 Modules 的独立渲染硬件接口。本页先提供中文说明，随后提供 English guide。

FyuuRHI is a standalone rendering hardware interface built with C++23 Modules. The Chinese guide comes first, followed by the English guide.

## 中文

### 支持范围

FyuuRHI 提供统一的实例、设备、资源、管线、命令图和呈现接口。后端按平台编译：

| 平台 | 后端 |
| --- | --- |
| Windows | DirectX 12、Vulkan、OpenGL、WebGPU |
| Linux | Vulkan、OpenGL、WebGPU |
| Apple | Metal、WebGPU |

应用只导入主模块：

```cpp
import fyuu_rhi;
```

CMake 目标链接 `FyuuRHI`：

```cmake
target_link_libraries(MyApplication PRIVATE FyuuRHI)
```

### 初始化与选择后端

RHI 上下文必须在请求实例前初始化。日志由调用方提供的 `log::Sink` 同步接收：

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);
```

`EnumerateBackends()` 返回当前平台编译进来的后端。`RequestInstance()` 通过回调返回相应实例：

```cpp
for (auto backend : fyuu_rhi::EnumerateBackends()) {
    // 展示给用户，或按配置选择。
}

fyuu_rhi::RequestInstance(
    fyuu_rhi::Backend::Vulkan,
    [](fyuu_rhi::Instance instance) {
        auto physical_devices = instance.EnumeratePhysicalDevices();
        if (physical_devices.empty()) {
            throw std::runtime_error("No physical device is available");
        }

        auto logical_device = physical_devices.front().CreateLogicalDevice();
        auto scheduler = logical_device.CreateScheduler();
    }
);
```

可通过 `PhysicalDevice::GetInfo()` 检查名称、类型、厂商 ID、设备 ID 和专用显存。`BestPerformance()` 可用于推荐排序；创建逻辑设备的成员函数目前需要非 const 物理设备对象。

OpenGL 上下文具有线程亲和性。需要让其他调用线程访问共享 GL 对象时，调用：

```cpp
instance.ShareContextOnThisThread();
```

其他后端会忽略该操作。

### 对象所有权

`PhysicalDevice`、`LogicalDevice`、`Resource`、`View`、`Sampler`、`Pipeline`、`PipelineResourceGroup` 和 `CompletionToken` 使用不透明实现并按唯一所有权移动。`CommandScheduler` 可复制，它共享调度上下文。

命令提交时，绑定对象移动进 operation。GPU 完成、失败或取消后，receiver 收到 `CommandGraphResources`，调用方再按原绑定索引取回对象。提交期间不得继续访问已移动的对象。

### 创建资源和视图

`ResourceFlags` 同时声明用途、内存属性、纹理维度、视图类型、采样数和格式。后端会拒绝矛盾或缺失的组合。

```cpp
fyuu_rhi::ResourceFlags color_flags;
color_flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
color_flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
color_flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
color_flags.Set(fyuu_rhi::ResourceFlagBits::TextureViewAspectAll);
color_flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
color_flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
color_flags.Set(fyuu_rhi::ResourceFlagBits::Sample1);
color_flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);

auto texture = logical_device.CreateTexture(
    width,
    height,
    1u,
    1u,
    color_flags
);

auto view = texture.CreateTextureView(
    0u,
    1u,
    0u,
    1u,
    color_flags
);
```

View 由 Resource 创建，因此不会通过公共接口向下转型，也不会把其他后端或其他设备的资源误传给 View 工厂。缓冲区 View 同样使用 `Resource::CreateBufferView()`。

缓冲区上传目标必须声明 `CopyDST`；`WriteBuffer` 不能写入仅声明 `VertexBuffer` 等读取用途的资源。

### 创建管线和资源组

Shader 使用 `pipeline::SlangPipelineProgramDescriptor` 提供源码、入口点、宏和编译选项。图形管线还要声明顶点输入、图元、光栅化、深度模板、混合、采样数和颜色附件格式。

```cpp
auto pipeline = logical_device.CreateGraphicsPipeline(
    {
        .program = {
            .modules = modules,
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

`PipelineResourceGroup` 由所属 Pipeline 创建：

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

绑定值在创建期间借用资源；后端生成的资源组保留其原生描述符所需的所有权。

### 命令图

`CommandGraphBuilder` 先注册绑定槽，再创建节点。节点声明逻辑队列、依赖、资源访问和命令。访问声明用于生成批次、屏障和跨队列同步，必须与命令的真实使用一致。

```cpp
using namespace fyuu_rhi::execution;

auto builder = scheduler.schedule();
auto target_index = builder.RegisterResource();
auto target_view_index = builder.RegisterView();
auto pipeline_index = builder.RegisterPipeline();

auto render = builder.CreateNode(QueueType::Graphics);
render
    .Access(
        {
            target_index,
            AccessMode::Write,
            ResourceUsage::ColorAttachment,
            {}
        }
    )
    .Record(BindPipeline{ pipeline_index })
    .Record(
        BeginRendering{
            .area = { 0, 0, width, height },
            .colors = {
                {
                    .resource = target_index,
                    .view = target_view_index,
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

`QueueType` 表示能力需求，不承诺独占或独立的物理队列。D3D12 和 Vulkan 后端根据整张图选择队列；Vulkan 同时处理 queue-family ownership transfer、timeline semaphore 和 binary semaphore/fence 回退。

### Present 与窗口循环

Present 使用独立节点，并依赖完成渲染的节点：

```cpp
auto present = builder.CreateNode(QueueType::Present, render);
present
    .Access(
        {
            target_index,
            AccessMode::Read,
            ResourceUsage::PresentationSource,
            {}
        }
    )
    .Record(
        Present{
            .source = target_index,
            .target = 0u,
            .buffer_count = 3u,
            .vertical_sync = true
        }
    );
```

连接 operation 后通过 `SetPresentationTarget()` 按索引提供原生窗口目标。应用必须持续运行平台事件循环并逐帧提交；只提交一帧后阻塞消息循环不是有效的呈现循环。

后端会在窗口尺寸、buffer 数量或 present mode 改变，以及 WSI 返回 out-of-date/suboptimal 时重建交换链。Vulkan 的 acquire out-of-date 路径会在当前准备阶段安全重建并重试，不会遗留永远无法 signal 的 present fence。

### Receiver 与完成路径

连接图、移动绑定并启动：

```cpp
auto operation = std::move(builder).connect(Receiver{ state });
operation.BindResource(target_index, std::move(texture));
operation.BindView(target_view_index, std::move(view));
operation.BindPipeline(pipeline_index, std::move(pipeline));
operation.SetPresentationTarget(native_window);
operation.start();
```

Receiver 需要提供：

- `get_env()`：提供可选 stop token。
- `RecoverBindings(CommandGraphResources&&)`：错误或取消完成前收回绑定。
- `set_value(CommandGraphResources&&) &&`：成功完成。
- `set_error(std::exception_ptr) &&`：失败完成。
- `set_stopped() &&`：取消完成。

完成回调通常来自内部 completion service 线程，因此共享状态必须同步。通过以下接口按绑定索引取回对象：

```cpp
auto texture = resources.TakeResource(target_index);
auto view = resources.TakeView(target_view_index);
auto pipeline = resources.TakePipeline(pipeline_index);
```

每个槽位只能取回一次。

### 错误与验证

- phase 1 的参数和内存错误同步抛出并进入 error 通道。
- phase 2 的录制异常封存在 CompletionToken 中。
- stop 在提交前的准备与录制阶段生效；提交开始后不再检查。
- 后端未捕获验证错误会写入 RHI log sink。测试不能只检查进程退出码，还必须把 Error/Fatal 日志视为失败。
- GPU 对象可能在析构时等待最后一次使用完成，不应在延迟敏感线程随意销毁在途对象。

### 可运行示例

完整的跨后端窗口、上传、绘制、Present、receiver 和 resize 测试位于：

```text
lib/fyuu_rhi/test/hello_triangle/main.cpp
```

交互运行：

```text
HelloTriangle <d3d12|vulkan|opengl|webgpu|metal>
```

窗口会逐帧渲染直到关闭。CTest 使用 `--test`，提交两帧并在中间 resize，以验证交换链重建后自动退出。

## English

### Supported backends

FyuuRHI exposes one public API for instances, devices, resources, pipelines, command graphs, and presentation.

| Platform | Backends |
| --- | --- |
| Windows | DirectX 12, Vulkan, OpenGL, WebGPU |
| Linux | Vulkan, OpenGL, WebGPU |
| Apple | Metal, WebGPU |

Import and link the primary module:

```cpp
import fyuu_rhi;
```

```cmake
target_link_libraries(MyApplication PRIVATE FyuuRHI)
```

### Initialization

Initialize the process-wide RHI context before requesting an instance:

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);

fyuu_rhi::RequestInstance(
    fyuu_rhi::Backend::Vulkan,
    [](fyuu_rhi::Instance instance) {
        auto physical_devices = instance.EnumeratePhysicalDevices();
        auto logical_device = physical_devices.front().CreateLogicalDevice();
        auto scheduler = logical_device.CreateScheduler();
    }
);
```

Use `EnumerateBackends()` to discover the backends compiled for the current platform. Inspect `PhysicalDevice::GetInfo()` when applying a custom adapter policy. OpenGL callers may use `Instance::ShareContextOnThisThread()` to bind a shared context on another thread.

### Ownership

GPU objects use opaque implementations and unique ownership. Resources, views, samplers, pipelines, resource groups, and devices are moved into command operations. `CommandScheduler` is copyable and shares its scheduling context.

An operation owns every binding while GPU work is in flight. Its receiver gets all objects back as `CommandGraphResources` on success, failure, or cancellation.

### Resources

`ResourceFlags` declares usage, memory placement, texture dimension, view type, sample count, and format. Views are created by their owning resource:

```cpp
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

A buffer written by `WriteBuffer` must include `ResourceFlagBits::CopyDST`; declaring only `VertexBuffer` or another read usage is insufficient.

### Pipelines and resource groups

Shaders are supplied through `pipeline::SlangPipelineProgramDescriptor`. Graphics descriptors also define vertex input, primitive, rasterization, depth/stencil, multisampling, blending, and attachment formats.

Pipeline resource groups are created by their pipeline, which prevents public downcasts and cross-backend factory misuse:

```cpp
auto group = pipeline.CreatePipelineResourceGroup(0u, bindings);
```

### Command graphs

Register graph-local binding slots, create nodes, declare resource access, and record commands:

```cpp
using namespace fyuu_rhi::execution;

auto builder = scheduler.schedule();
auto target_index = builder.RegisterResource();
auto view_index = builder.RegisterView();
auto pipeline_index = builder.RegisterPipeline();

auto render = builder.CreateNode(QueueType::Graphics);
render
    .Access(
        {
            target_index,
            AccessMode::Write,
            ResourceUsage::ColorAttachment,
            {}
        }
    )
    .Record(BindPipeline{ pipeline_index })
    .Record(BeginRendering{ /* attachments */ })
    .Record(Draw{ 3u, 1u, 0u, 0u })
    .Record(EndRendering{});
```

Access declarations drive batching, barriers, and cross-queue synchronization. `QueueType` expresses a capability requirement, not a promise of a dedicated physical queue.

### Presentation loop

Presentation is a separate dependent node:

```cpp
auto present = builder.CreateNode(QueueType::Present, render);
present
    .Access(
        {
            target_index,
            AccessMode::Read,
            ResourceUsage::PresentationSource,
            {}
        }
    )
    .Record(
        Present{
            .source = target_index,
            .target = 0u,
            .buffer_count = 3u,
            .vertical_sync = true
        }
    );
```

Bind the native target with `SetPresentationTarget()`. A real application must pump platform events and submit frames continuously. Backends recreate swapchains when the extent, buffer count, or present mode changes, or when WSI reports out-of-date/suboptimal.

### Completion receiver

The receiver contract consists of `get_env()`, `RecoverBindings()`, `set_value()`, `set_error()`, and `set_stopped()`. Completion normally occurs on an internal service thread, so receiver state must be synchronized.

Recover move-only objects by their original binding index:

```cpp
auto texture = resources.TakeResource(target_index);
auto view = resources.TakeView(view_index);
auto pipeline = resources.TakePipeline(pipeline_index);
```

Each slot may be taken only once.

### Errors and testing

Preparation errors are reported before submission, recording failures are retained by the completion token, and stop requests are observed before phase 3 submission. Uncaptured backend validation errors are written to the configured RHI log sink and must be treated as test failures.

The complete FyuuRHI backend test is `lib/fyuu_rhi/test/hello_triangle/main.cpp`. Run it interactively as:

```text
HelloTriangle <d3d12|vulkan|opengl|webgpu|metal>
```

It renders and presents continuously until the window closes. CTest adds `--test`, renders two frames with a resize between them, validates swapchain recreation, and exits automatically.
