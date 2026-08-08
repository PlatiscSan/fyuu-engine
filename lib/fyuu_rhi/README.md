# FyuuRHI 使用指南 / FyuuRHI User Guide

FyuuRHI 是一个基于 C++23 Modules 的渲染硬件接口。它将设备、资源、管线和执行图作为跨后端的公共接口，并由 D3D12、Vulkan、OpenGL、WebGPU（以及 Apple 平台上的 Metal）实现具体行为。

FyuuRHI is a C++23 Modules rendering hardware interface. It exposes backend-independent devices, resources, pipelines, and execution graphs, with concrete implementations for D3D12, Vulkan, OpenGL, WebGPU, and Metal on Apple platforms.

- [中文](#中文)
- [English](#english)

---

## 中文

### 1. 设计概览

一次典型的 FyuuRHI 工作流如下：

1. 初始化指定后端的 `Instance`。
2. 枚举并选择物理设备。
3. 创建逻辑设备、资源、视图、采样器和管线。
4. 从逻辑设备创建 `CommandScheduler`。
5. 使用 `CommandGraphBuilder` 注册绑定槽并描述节点、资源访问、依赖和命令。
6. 编译并连接命令图，移动绑定实际对象，然后启动异步执行。
7. receiver 在完成、错误或取消时收回所有绑定对象。

命令图声明的是资源访问关系，而不是手工编写后端屏障。执行计划会根据访问关系生成批次、依赖和屏障；后端负责选择实际队列、录制命令并提交。

### 2. 构建与导入

项目要求 C++23，并使用 CMake 的 C++ Modules 支持。将 `FyuuRHI` 链接到目标：

```cmake
target_link_libraries(MyTarget PRIVATE FyuuRHI)
```

在 C++ 文件中导入主模块：

```cpp
import fyuu_rhi;
```

主模块导出公共资源、管线和执行 API，并提供以下常用后端类型别名：

| 平台 | 类型 |
| --- | --- |
| Windows | `D3D12Instance`、`VulkanInstance`、`OpenGLInstance`、`WebGPUInstance` |
| Linux/Android | `VulkanInstance`、`OpenGLInstance`、`WebGPUInstance` |
| Apple | `MetalInstance`、`WebGPUInstance` |

后端相关对象不能混用。例如，Vulkan 逻辑设备创建的资源必须绑定到 Vulkan scheduler 的命令图。

### 3. 初始化设备

以下示例使用 Vulkan；替换类型别名即可选择其他后端。

```cpp
using namespace fyuu_rhi;

VulkanInstance::Initialize(
    "Example",
    Version{ 0, 1, 0, 0 },
    "FyuuEngine",
    Version{ 0, 1, 0, 0 }
);

auto instance = VulkanInstance::Get();
if (!instance) {
    throw std::runtime_error("Vulkan instance is not initialized");
}

auto physical_devices = instance->EnumeratePhysicalDevices();
auto physical_device = BestPerformance(physical_devices);
auto info = physical_device.GetInfo();
auto logical_device = physical_device.CreateLogicalDevice();
auto scheduler = logical_device.CreateScheduler();
```

`Instance::Initialize` 对每个后端类型只初始化一次。`BestPerformance` 优先选择独立 GPU，然后按设备类型和专用显存排序。也可以自行遍历 `GetInfo()` 的结果选择设备。

部分平台/后端需要额外的原生参数，这些参数会由 `Initialize` 转发给后端。例如 Windows OpenGL 需要 `HWND`，Android Vulkan 需要 `android_app*`；以对应后端的 `CreateInstance` 签名为准。

OpenGL 后端需要在线程上共享原生上下文时，可调用：

```cpp
instance->ShareContextOnThisThread();
```

不需要该操作的后端会忽略此调用。

### 4. 创建资源和视图

`ResourceFlags` 同时描述资源用途、内存位置、纹理维度、采样数和格式。标志通过 `Set` 添加：

```cpp
ResourceFlags vertex_flags;
vertex_flags.Set(ResourceFlagBits::VertexBuffer);
vertex_flags.Set(ResourceFlagBits::CopyDST);
vertex_flags.Set(ResourceFlagBits::DeviceLocal);

auto vertex_buffer = logical_device.CreateBuffer(
    4096u,
    vertex_flags
);

ResourceFlags color_flags;
color_flags.Set(ResourceFlagBits::Texture2D);
color_flags.Set(ResourceFlagBits::TextureView2D);
color_flags.Set(ResourceFlagBits::RenderAttachment);
color_flags.Set(ResourceFlagBits::CopySRC);
color_flags.Set(ResourceFlagBits::DeviceLocal);
color_flags.Set(ResourceFlagBits::Sample1);
color_flags.Set(ResourceFlagBits::R8G8B8A8Unorm);

auto color_texture = logical_device.CreateTexture(
    width,
    height,
    1u,
    1u,
    color_flags
);

auto color_view = logical_device.CreateTextureView(
    color_texture,
    0u,
    1u,
    0u,
    1u,
    color_flags
);
```

资源和视图是 move-only 对象。`Resource::ID()` 返回稳定的逻辑资源标识；`Size()` 用于缓冲区，`TextureExtent()` 用于纹理。

常见内存标志：

- `DeviceLocal`：GPU 本地资源。
- `HostVisible`：CPU 可见资源，常用于 staging。
- `DeviceReadback`：用于 GPU 到 CPU 的读取。

常见用途标志：

- `CopySRC` / `CopyDST`
- `VertexBuffer` / `IndexBuffer` / `UniformBuffer` / `StorageBuffer`
- `TextureBinding` / `SamplerBinding` / `StorageBinding`
- `RenderAttachment`

不要同时设置互斥的内存、格式、纹理维度或采样数标志。

### 5. 采样器、管线和资源组

采样器由 `SamplerDescriptor` 创建。图形和计算管线分别使用 `GraphicsPipelineDescriptor` 与 `ComputePipelineDescriptor`。Shader 程序通过 `SlangPipelineProgramDescriptor` 描述。

```cpp
auto sampler = logical_device.CreateSampler(SamplerDescriptor{
    // 按实际需求填写过滤、寻址和比较配置。
});

pipeline::GraphicsPipelineDescriptor pipeline_desc;
// 填写 program、vertex、primitive、rasterization、color_targets 等字段。
auto pipeline = logical_device.CreateGraphicsPipeline(pipeline_desc);
```

资源组是针对某条管线和某个 `space` 创建的不可变绑定集合：

```cpp
// 此代码位于 template <class Backend> 的渲染器中。
using BindingValue = pipeline::PipelineBindingValue<Backend>;
using Binding = pipeline::PipelineResourceBinding<Backend>;

std::vector<Binding> bindings;
bindings.push_back({
    .slot = 0u,
    .array_element = 0u,
    .value = BindingValue::FromCombined(texture_view, sampler)
});

auto group = logical_device.CreatePipelineResourceGroup(
    pipeline,
    0u,
    bindings
);
```

可使用 `FromBuffer`、`FromView`、`FromSampler` 或 `FromCombined` 构造绑定值。资源组内部对绑定资源为非拥有引用，因此相关资源、视图和采样器在资源组使用期间必须保持存活且不能移动。

### 6. 命令图模型

`CommandGraphBuilder` 中的注册索引只是图的绑定槽。图构建完成后，再把真实对象移动到对应槽位。

每个节点包含：

- 一个逻辑队列类型：`Graphics`、`Compute`、`Transfer` 或 `Present`。
- 零个或多个对其他节点的依赖。
- 对资源的声明式访问。
- 按顺序录制的命令。

访问声明必须与命令行为一致：

```cpp
using namespace fyuu_rhi::execution;

auto builder = scheduler.schedule();
auto staging_index = builder.RegisterResource();
auto destination_index = builder.RegisterResource();

auto transfer = builder.CreateNode(QueueType::Transfer);
transfer
    .Access({
        staging_index,
        AccessMode::Read,
        ResourceUsage::CopySource,
        {}
    })
    .Access({
        destination_index,
        AccessMode::Write,
        ResourceUsage::CopyDestination,
        {}
    })
    .Record(WriteBuffer{
        staging_index,
        0u,
        bytes
    })
    .Record(CopyBuffer{
        staging_index,
        destination_index,
        0u,
        0u,
        bytes.size()
    });
```

`ResourceRange` 为 `std::monostate` 时表示整个资源。也可以使用 `BufferRange` 或 `TextureRange` 声明子范围。

依赖可以在创建节点时指定，也可以随后添加：

```cpp
auto render = builder.CreateNode(QueueType::Graphics, transfer);
auto present = builder.CreateNode(QueueType::Present);
present.DependsOn(render);
```

常用命令包括：

- `BeginRendering` / `EndRendering`
- `BindPipeline` / `BindResourceGroup`
- `BindVertexBuffer` / `BindIndexBuffer`
- `Viewport` / `Scissor`
- `Draw` / `DrawIndexed` / `Dispatch`
- `WriteBuffer`
- `CopyBuffer`、纹理与缓冲区之间的复制命令
- `Present`

### 7. 执行、receiver 与对象回收

命令图执行是异步的。连接图时传入 receiver，然后绑定所有已注册槽位并调用 `start()`。

receiver 必须实现成功、错误和取消通道，并通过 `RecoverBindings` 接收错误或取消路径返还的对象。成功路径通过 `set_value` 接收 `CommandGraphResources`。

```cpp
template <class Backend>
struct Receiver {
    std::shared_ptr<State<Backend>> state;

    struct Environment {};

    Environment get_env() const noexcept {
        return {};
    }

    void RecoverBindings(
        fyuu_rhi::execution::CommandGraphResources<Backend>&& resources
    ) noexcept {
        state->resources.emplace(std::move(resources));
    }

    void set_value(
        fyuu_rhi::execution::CommandGraphResources<Backend>&& resources
    ) && noexcept {
        state->resources.emplace(std::move(resources));
        state->done = true;
    }

    void set_error(std::exception_ptr error) && noexcept {
        state->error = error;
        state->done = true;
    }

    void set_stopped() && noexcept {
        state->stopped = true;
        state->done = true;
    }
};
```

实际跨线程 receiver 应使用 mutex、condition variable 或其他同步机制保护共享状态。

```cpp
auto operation = std::move(builder).connect(Receiver<Backend>{ state });
operation.BindResource(staging_index, std::move(staging));
operation.BindResource(destination_index, std::move(destination));
operation.start();
```

执行期间绑定对象归 operation 所有，调用方不能继续访问。完成后使用以下接口取回：

```cpp
auto destination = resources.TakeResource(destination_index);
auto view = resources.TakeView(view_index);
auto sampler = resources.TakeSampler(sampler_index);
auto pipeline = resources.TakePipeline(pipeline_index);
auto group = resources.TakeResourceGroup(group_index);
```

每个槽位只能取回一次。重复取回会抛出异常。若连续执行相同绑定布局的图，可以使用 `FromLastBinding` 将上一轮的整组绑定直接移入新 operation。

### 8. 渲染与呈现

渲染节点需要声明附件和其他资源访问：

```cpp
auto render = builder.CreateNode(QueueType::Graphics);
render
    .Access({
        target_index,
        AccessMode::Write,
        ResourceUsage::ColorAttachment,
        {}
    })
    .Record(BindPipeline{ pipeline_index })
    .Record(BeginRendering{
        .area = { 0, 0, width, height },
        .colors = {{
            .resource = target_index,
            .view = target_view_index,
            .load = LoadOperation::Clear,
            .store = StoreOperation::Store,
            .clear = { 0.05f, 0.05f, 0.08f, 1.0f }
        }}
    })
    .Record(Draw{ 3u, 1u, 0u, 0u })
    .Record(EndRendering{});
```

呈现使用独立的 `Present` 节点，并显式依赖渲染节点：

```cpp
auto present = builder.CreateNode(QueueType::Present, render);
present
    .Access({
        target_index,
        AccessMode::Read,
        ResourceUsage::PresentationSource,
        {}
    })
    .Record(Present{
        .source = target_index,
        .target = 0u,
        .buffer_count = 3u,
        .vertical_sync = true
    });

auto operation = std::move(builder).connect(Receiver<Backend>{ state });
// 绑定资源、视图、管线……
operation.SetPresentationTarget(native_window_handle);
operation.start();
```

每张图中同一个呈现目标最多呈现一次。`Present::target` 是传给 `SetPresentationTarget` 的目标索引。窗口尺寸、buffer 数量或 present mode 改变时，后端会重建对应交换链。

### 9. 取消和错误语义

- 启动前或准备/录制阶段观察到停止请求时，receiver 收到 stopped 通道。
- 参数、图结构和绑定错误通过 error 通道返回。
- GPU 完成由后端 `CompletionToken` 轮询，公共层的 completion service 在完成后调用 receiver。
- 错误和取消路径通过 `RecoverBindings` 返还绑定，避免 move-only GPU 对象丢失。
- GPU 对象的销毁可能等待其最后一次提交完成；不要在延迟敏感线程上随意销毁仍在使用的对象。

### 10. 后端注意事项

- D3D12 与 Vulkan 支持图级多队列调度；实际物理队列由后端选择。
- Vulkan 会管理 queue-family ownership transfer、timeline semaphore，以及不支持 timeline 时的 binary semaphore/fence 回退。
- OpenGL 的上下文具有线程亲和性；使用前确认当前线程已共享正确上下文。
- `QueueType` 表示任务能力需求，不保证后端一定使用独立的物理队列。
- 跨后端代码应只依赖公共类型，不应访问 backend traits 中的原生句柄。

---

## English

### 1. Architecture

A typical FyuuRHI workflow is:

1. Initialize an `Instance` for the selected backend.
2. Enumerate and select a physical device.
3. Create a logical device, resources, views, samplers, and pipelines.
4. Create a `CommandScheduler` from the logical device.
5. Use `CommandGraphBuilder` to register binding slots and describe nodes, accesses, dependencies, and commands.
6. Connect the graph to a receiver, move concrete objects into the binding slots, and start asynchronous execution.
7. Recover every bound object from the receiver on success, error, or cancellation.

The graph declares resource relationships rather than backend barriers. Compilation derives batches, dependencies, and barriers; each backend selects physical queues, records commands, and submits the resulting work.

### 2. Build and import

FyuuRHI requires C++23 and CMake C++ Modules support. Link the library target:

```cmake
target_link_libraries(MyTarget PRIVATE FyuuRHI)
```

Import the primary module:

```cpp
import fyuu_rhi;
```

Common backend aliases include `D3D12Instance`, `VulkanInstance`, `OpenGLInstance`, `WebGPUInstance`, and `MetalInstance` where supported. Objects from different backends cannot be mixed.

### 3. Device initialization

```cpp
using namespace fyuu_rhi;

VulkanInstance::Initialize(
    "Example",
    Version{ 0, 1, 0, 0 },
    "FyuuEngine",
    Version{ 0, 1, 0, 0 }
);

auto instance = VulkanInstance::Get();
auto physical_devices = instance->EnumeratePhysicalDevices();
auto physical_device = BestPerformance(physical_devices);
auto logical_device = physical_device.CreateLogicalDevice();
auto scheduler = logical_device.CreateScheduler();
```

`Initialize` runs once for each backend type. `BestPerformance` prefers discrete GPUs and then considers device type and dedicated memory. Applications may instead inspect `PhysicalDeviceInfo` and apply their own selection policy.

Some platform/backend combinations require additional native arguments, which `Initialize` forwards to the backend. For example, Windows OpenGL requires an `HWND`, while Android Vulkan requires an `android_app*`; consult the corresponding backend's `CreateInstance` signature.

### 4. Resources and views

`ResourceFlags` describes usage, memory placement, texture dimension, sample count, and format:

```cpp
ResourceFlags flags;
flags.Set(ResourceFlagBits::Texture2D);
flags.Set(ResourceFlagBits::TextureView2D);
flags.Set(ResourceFlagBits::RenderAttachment);
flags.Set(ResourceFlagBits::DeviceLocal);
flags.Set(ResourceFlagBits::Sample1);
flags.Set(ResourceFlagBits::R8G8B8A8Unorm);

auto texture = logical_device.CreateTexture(
    width,
    height,
    1u,
    1u,
    flags
);

auto view = logical_device.CreateTextureView(
    texture,
    0u,
    1u,
    0u,
    1u,
    flags
);
```

Resources and views are move-only. Use `DeviceLocal` for GPU-local storage, `HostVisible` for upload/staging resources, and `DeviceReadback` for GPU-to-CPU transfers. Do not combine mutually exclusive memory, dimension, sample-count, or format flags.

### 5. Pipelines and resource groups

Create graphics and compute pipelines with `GraphicsPipelineDescriptor` and `ComputePipelineDescriptor`. Pipeline resource groups are immutable binding collections associated with one pipeline space.

Binding values are created with:

- `PipelineBindingValue::FromBuffer`
- `PipelineBindingValue::FromView`
- `PipelineBindingValue::FromSampler`
- `PipelineBindingValue::FromCombined`

Resource groups keep non-owning references to bound objects. Those resources, views, and samplers must remain alive and unmoved while the group is used.

### 6. Command graphs

Registration returns graph-local binding indices. Concrete objects are supplied only after the graph is connected.

```cpp
using namespace fyuu_rhi::execution;

auto builder = scheduler.schedule();
auto source_index = builder.RegisterResource();
auto destination_index = builder.RegisterResource();

auto transfer = builder.CreateNode(QueueType::Transfer);
transfer
    .Access({
        source_index,
        AccessMode::Read,
        ResourceUsage::CopySource,
        {}
    })
    .Access({
        destination_index,
        AccessMode::Write,
        ResourceUsage::CopyDestination,
        {}
    })
    .Record(CopyBuffer{
        source_index,
        destination_index,
        0u,
        0u,
        byte_count
    });
```

Nodes use `Graphics`, `Compute`, `Transfer`, or `Present` queue types. These express capability requirements; they do not guarantee a dedicated physical queue. Dependencies can be supplied to `CreateNode` or added with `DependsOn`.

Every command's resource behavior must be represented by matching `ResourceAccess` entries. Use `std::monostate` for the whole resource, or `BufferRange`/`TextureRange` for a subrange.

### 7. Execution and ownership

Connect the completed builder to a receiver, bind every registered slot, and start the operation:

```cpp
auto operation = std::move(builder).connect(Receiver<Backend>{ state });
operation.BindResource(source_index, std::move(source));
operation.BindResource(destination_index, std::move(destination));
operation.start();
```

The operation owns all bindings while work is in flight. The receiver obtains a `CommandGraphResources` object on completion. Recover objects exactly once with `TakeResource`, `TakeView`, `TakeSampler`, `TakePipeline`, or `TakeResourceGroup`.

The receiver contract contains:

- `set_value(CommandGraphResources&&)` for success.
- `set_error(std::exception_ptr)` for failure.
- `set_stopped()` for cancellation.
- `RecoverBindings(CommandGraphResources&&)` to recover move-only bindings before error or stopped completion.
- `get_env()` for optional stop-token propagation when the standard sender/receiver API is unavailable.

Use synchronization around receiver state because completion normally occurs on the internal completion-service thread.

### 8. Rendering and presentation

A rendering node declares attachment access and records `BeginRendering`, draw commands, and `EndRendering`. Presentation is a separate `Present` node that depends on rendering and declares `PresentationSource` access.

```cpp
auto present = builder.CreateNode(QueueType::Present, render);
present
    .Access({
        target_index,
        AccessMode::Read,
        ResourceUsage::PresentationSource,
        {}
    })
    .Record(Present{
        .source = target_index,
        .target = 0u,
        .buffer_count = 3u,
        .vertical_sync = true
    });

operation.SetPresentationTarget(native_window_handle);
```

`Present::target` indexes targets added with `SetPresentationTarget`. A graph may present each native target at most once. Backends recreate the swapchain when the window extent, buffer count, or presentation mode changes.

### 9. Error and cancellation behavior

- Invalid graphs and missing bindings complete through the error channel.
- A stop request observed before submission completes through the stopped channel.
- Backend completion tokens are polled by the shared completion service.
- Bindings are returned on every completion path.
- Destroying an in-flight GPU object may wait for its final submission; avoid doing so on latency-sensitive threads.

### 10. Backend notes

- D3D12 and Vulkan support graph-level multi-queue scheduling.
- Vulkan manages queue-family ownership transfers and uses timeline semaphores when supported, with a binary semaphore/fence fallback.
- OpenGL contexts are thread-affine; call `ShareContextOnThisThread` where required.
- Portable application code should use the public RHI types and avoid backend trait/native-handle access.
