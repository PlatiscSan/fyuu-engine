# FyuuRHI

跨 API 渲染硬件接口

FyuuRHI 是 FyuuEngine 的跨 API 渲染硬件抽象层，提供统一的 D3D12 / Vulkan / OpenGL(ES) / WebGPU 后端抽象。

---

## 中文文档

### 核心类型

#### Instance

```cpp
namespace fyuu_rhi {
    template <class Backend>
    static void Instance<Backend>::Initialize(
        std::string_view app_name,
        Version const& app_ver,
        std::string_view engine_name,
        Version const& engine_ver
    );

    template <class Backend>
    static Instance<Backend>* Instance<Backend>::Get();

    template <class Backend>
    auto Instance<Backend>::EnumeratePhysicalDevices() const
        -> std::vector<PhysicalDevice<Backend>>;
}
```

#### PhysicalDevice

```cpp
namespace fyuu_rhi {
    template <class Backend>
    class PhysicalDevice {
        PhysicalDeviceInfo GetInfo() const;
    };
}
```

`PhysicalDeviceInfo` 包含名称、厂商 ID、设备 ID、专用内存大小、设备类型（离散 GPU / 集显 / CPU / 虚拟）。

工具函数 `BestPerformance(devices)` 按离散 GPU > 集显 > 专用内存排序选择最优设备。

#### LogicalDevice

```cpp
namespace fyuu_rhi {
    template <class Backend>
    class LogicalDevice {
        // 资源创建
        Resource<Backend> CreateBuffer(size_t size_bytes, ResourceFlags const& flags);
        Resource<Backend> CreateTexture(
            size_t width, size_t height, size_t depth_layers,
            size_t mip_count, ResourceFlags const& flags
        );

        // 视图创建
        View<Backend> CreateBufferView(Resource<Backend> const& buf, size_t offset, size_t range, ResourceFlags const& flags);
        View<Backend> CreateTextureView(Resource<Backend> const& tex, size_t base_mip, size_t mip_count, size_t base_layer, size_t layer_count, ResourceFlags const& flags);

        // 采样器
        Sampler<Backend> CreateSampler(SamplerDescriptor const& descriptor);

        // 管线
        pipeline::Pipeline<Backend> CreateGraphicsPipeline(pipeline::GraphicsPipelineDescriptor const& descriptor);
        pipeline::Pipeline<Backend> CreateComputePipeline(pipeline::ComputePipelineDescriptor const& descriptor);
        pipeline::PipelineResourceGroup<Backend> CreatePipelineResourceGroup(pipeline::Pipeline<Backend> const& pipe, uint32_t space, std::span<pipeline::PipelineResourceBinding<Backend> const> bindings);

        // 调度器
        execution::Scheduler<Backend> CreateScheduler(execution::SchedulerDescriptor const& descriptor);

        // 命令图
        execution::CommandGraph<Backend> CreateCommandGraph(execution::CommandGraphDescriptor const& descriptor);
        execution::ExecutableGraph<Backend> CompileCommandGraph(execution::CommandGraph<Backend> const& graph);
    };
}
```

#### ResourceFlags

`ResourceFlagBits` 同时描述资源用途、维度、堆位置和像素格式，组合为 `ResourceFlags`。

```cpp
// 用途
CopySRC, CopyDST, VertexBuffer, IndexBuffer, UniformBuffer, StorageBuffer,
TextureBinding, StorageBinding, RenderAttachment, TransientAttachment

// 维度
Texture1D, Texture2D, Texture3D

// 堆位置
DeviceLocal, HostVisible, DeviceReadback

// 格式（部分）
R8G8B8A8Unorm, R8G8B8A8Srgb, B8G8R8A8Srgb,
R32Float, R32G32B32A32Float,
D32Float, D24UnormS8Uint,
Bc1Unorm, Bc3Unorm, Bc5Unorm, Bc7UnormSrgb
```

#### Scheduler

```cpp
namespace fyuu_rhi::execution {
    struct SchedulerDescriptor {
        AtomicFlags<SchedulerFlagBits> flags;  // Graphics, Compute, Copy
        float priority = 0.5f;
    };

    template <class Backend>
    class Scheduler {
        Sender<Backend> schedule() const;
    };
}
```

#### CommandGraph

命令图使用构建器模式构造：

```cpp
namespace fyuu_rhi::execution {
    class CommandGraphBuilder {
        GraphResourceID RegisterResource();
        GraphPipelineID RegisterPipeline();
        GraphViewID RegisterView();
        GraphResourceGroupID RegisterResourceGroup();
        GraphPresentationID RegisterPresentationTarget();

        GraphNodeID AddNode(GraphNodeFlagBits flags);           // Graphics/Compute/Copy/Present
        void AddDependency(GraphNodeID node, GraphNodeID dep);
        void AddAccess(GraphNodeID node, GraphResourceAccess access);
        void AddCommand(GraphNodeID node, GraphCommand cmd);    // 见下方命令变体

        CommandGraphDescriptor Build();                         // 验证 + 拓扑排序
    };

    // 节点命令（std::variant）
    // BeginRenderingCommand, EndRenderingCommand, BindPipelineCommand,
    // BindResourceGroupCommand, BindVertexBufferCommand, SetViewportCommand,
    // SetScissorCommand, BindIndexBufferCommand, DrawCommand, DrawIndexedCommand,
    // DispatchCommand, CopyBufferCommand, CopyBufferToTextureCommand,
    // CopyTextureToBufferCommand, CopyTextureCommand, PresentCommand
}
```

#### 资源传输 API

```cpp
// Map/Unmap
Map(scheduler, resource, range, flags)       // -> ResourceMapSender -> MappedResource
Unmap(mapped)                                // -> ResourceUnmapSender -> Resource

// Upload/Readback
Upload(device, scheduler, dest, offset, data)               // Buffer 上传
Upload(device, scheduler, dest, layout, region, data)        // 纹理上传
Readback(device, scheduler, source, range)                   // Buffer 回读
Readback(device, scheduler, source, region, layout)          // 纹理回读
```

### 后端别名

| 抽象类型 | D3D12 | Vulkan | OpenGL | WebGPU |
|----------|-------|--------|--------|--------|
| Instance | `D3D12Instance` | `VulkanInstance` | `OpenGLInstance` | `WebGPUInstance` |
| PhysicalDevice | `D3D12PhysicalDevice` | `VulkanPhysicalDevice` | `OpenGLPhysicalDevice` | `WebGPUPhysicalDevice` |
| LogicalDevice | `D3D12LogicalDevice` | `VulkanLogicalDevice` | `OpenGLLogicalDevice` | `WebGPULogicalDevice` |
| Resource | `D3D12Resource` | `VulkanResource` | `OpenGLResource` | `WebGPUResource` |
| Scheduler | `D3D12Scheduler` | `VulkanScheduler` | `OpenGLScheduler` | `WebGPUScheduler` |
| Pipeline | `pipeline::D3D12Pipeline` | `pipeline::VulkanPipeline` | `pipeline::OpenGLPipeline` | `pipeline::WebGPUPipeline` |
| CommandGraph | `execution::D3D12CommandGraph` | `execution::VulkanCommandGraph` | `execution::OpenGLCommandGraph` | `execution::WebGPUCommandGraph` |

### 使用示例

#### 初始化

```cpp
import fyuu_rhi;

int main() {
    using Backend = fyuu_rhi::d3d12::Backend;

    fyuu_rhi::Instance<Backend>::Initialize(
        "MyApp",       fyuu_rhi::Version{ .major = 1, .minor = 0 },
        "FyuuEngine",  fyuu_rhi::Version{ .major = 0, .minor = 1 }
    );

    auto* instance = fyuu_rhi::Instance<Backend>::Get();
    auto phys_devices = instance->EnumeratePhysicalDevices();
    auto best_dev = fyuu_rhi::BestPerformance(phys_devices);
    auto logical_device = best_dev.CreateLogicalDevice();
}
```

#### 创建资源

```cpp
using RFB = fyuu_rhi::ResourceFlagBits;

// 暂存缓冲（Host 写入 → GPU 读取）
auto staging = logical_device.CreateBuffer(64 * 1024,
    fyuu_rhi::ResourceFlags{}.Set(RFB::HostVisible).Set(RFB::CopySRC));

// GPU 顶点缓冲
auto vertices = logical_device.CreateBuffer(3 * sizeof(float) * 1024,
    fyuu_rhi::ResourceFlags{}.Set(RFB::DeviceLocal).Set(RFB::VertexBuffer).Set(RFB::CopyDST));

// 2D 渲染目标纹理
auto render_target = logical_device.CreateTexture(1920, 1080, 1, 1,
    fyuu_rhi::ResourceFlags{}.Set(RFB::Texture2D).Set(RFB::RenderAttachment).Set(RFB::R8G8B8A8Unorm));

// 采样器
auto sampler = logical_device.CreateSampler({
    .address_mode_u = fyuu_rhi::AddressMode::ClampToEdge,
    .address_mode_v = fyuu_rhi::AddressMode::ClampToEdge,
    .mag_filter     = fyuu_rhi::FilterMode::Linear,
    .min_filter     = fyuu_rhi::FilterMode::Linear,
});
```

#### 创建管线

```cpp
using namespace fyuu_rhi::pipeline;

auto pipeline = logical_device.CreateGraphicsPipeline({
    .program = {
        .modules = {{{
            .name   = "shader",
            .source = R"(
                struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };
                [shader("vertex")]
                VSOut vs_main(float3 pos : POSITION, float4 color : COLOR) {
                    VSOut out_val; out_val.pos = float4(pos, 1.0); out_val.color = color; return out_val;
                }
                [shader("fragment")]
                float4 fs_main(VSOut input) : SV_Target0 { return input.color; }
            )"
        }}},
        .entry_points = {{ {.name = "vs_main", .stage = PipelineStage::Vertex},
                           {.name = "fs_main", .stage = PipelineStage::Fragment} }}
    },
    .vertex = { .buffers = {{ {.slot = 0, .stride = 28} }},
                .attributes = {{ {.location = 0, .slot = 0, .offset = 0, .format = RFB::R32G32B32Float},
                                 {.location = 1, .slot = 0, .offset = 12, .format = RFB::R32G32B32A32Float} }} },
    .primitive = { .topology = PrimitiveTopology::TriangleList },
    .rasterization = { .front_face = FrontFace::CounterClockwise, .cull_mode = CullMode::Back },
    .color_targets = {{ {.format = RFB::R8G8B8A8Unorm} }}
});
```

#### 命令图

```cpp
using namespace fyuu_rhi::execution;

CommandGraphBuilder builder;
auto rt_id = builder.RegisterResource();
auto pipe_id = builder.RegisterPipeline();
auto present_id = builder.RegisterPresentationTarget();

auto node = builder.AddNode(GraphNodeFlagBits::Graphics);
builder.AddAccess(node, { .resource = rt_id, .flags = GraphAccessFlagBits::Write | GraphAccessFlagBits::ColorAttachment });
builder.AddCommand(node, BeginRenderingCommand{
    .colors = {{ {.resource = rt_id, .load = false, .store = true, .clear_red = 0.1f, .clear_green = 0.1f, .clear_blue = 0.2f, .clear_alpha = 1.0f} }},
    .width = 1920, .height = 1080
});
builder.AddCommand(node, BindPipelineCommand{ .pipeline = pipe_id });
builder.AddCommand(node, DrawCommand{ .vertex_count = 3, .instance_count = 1 });
builder.AddCommand(node, EndRenderingCommand{});

auto present_node = builder.AddNode(GraphNodeFlagBits::Present);
builder.AddDependency(present_node, node);
builder.AddCommand(present_node, PresentCommand{ .source = rt_id, .target = present_id });

auto desc = builder.Build();
auto graph = logical_device.CreateCommandGraph(desc);
auto executable = logical_device.CompileCommandGraph(graph);

CommandGraphBindings<Backend> bindings(desc);
bindings.Bind(rt_id, render_target);
bindings.Bind(pipe_id, pipeline);
bindings.Bind(present_id, native_window_handle);

auto scheduler = logical_device.CreateScheduler({
    .flags = SchedulerDescriptor{}.Set(SchedulerFlagBits::Graphics), .priority = 1.0f
});

// Submit(scheduler, executable, bindings) 返回 CommandGraphSender
```

#### Map/Unmap

```cpp
using RFB = fyuu_rhi::ResourceFlagBits;

// Map 用于 CPU 写入
auto map_sender = Map(scheduler,
    std::move(host_visible_buffer),
    ResourceDataRange{ .offset = 0, .size = 4096 },
    ResourceMapFlags{}.Set(ResourceMapFlagBits::Write));
```

#### Upload

```cpp
// Buffer 上传
auto upload_sender = Upload(logical_device, scheduler,
    std::move(gpu_buffer), 0u, cpu_data_span);

// 纹理上传
auto tex_upload_sender = Upload(logical_device, scheduler,
    std::move(gpu_texture),
    TextureDataLayout{ .bytes_per_row = 1024, .rows_per_image = 256 },
    TextureRegion{ .mip_level = 0, .base_array_layer = 0, .array_layer_count = 1, .width = 256, .height = 256, .depth = 1 },
    data_span);
```

#### Readback

```cpp
// Buffer 回读
auto readback_sender = Readback(logical_device, scheduler,
    std::move(gpu_buffer), ResourceDataRange{ .offset = 0, .size = 4096 });

// 纹理回读
auto tex_readback_sender = Readback(logical_device, scheduler,
    std::move(gpu_texture),
    TextureRegion{ .mip_level = 0, .base_array_layer = 0, .array_layer_count = 1, .width = 256, .height = 256, .depth = 1 },
    TextureDataLayout{ .bytes_per_row = 1024, .rows_per_image = 256 });
```

### 设计原则

- `LogicalDevice` 只承担工厂职责，不参与执行
- `Resource` / `View` / `Sampler` / `Pipeline` 是 move-only 值对象
- Format 通过 `ResourceFlagBits` 表达，不使用独立格式枚举
- Command Buffer / Fence / Semaphore / SwapChain 不向上层暴露
- Scheduler 遵循 `std::execution::scheduler_t` 概念，兼容 P2300 sender/receiver
- Present 是命令图命令，交换链由后端内部管理

### 后端完成状态

| 能力 | D3D12 | Vulkan | OpenGL/ES | WebGPU |
|------|-------|--------|-----------|--------|
| Graphics Pipeline | 完成 | 完成 | 完成 | 完成 |
| Compute Pipeline | 完成 | 完成 | 完成 | 完成 |
| CommandGraph 执行 | 完成 | 完成 | 完成 | 完成 |
| Draw/Dispatch/Copy | 完成 | 完成 | 完成 | 完成 |
| Present & 帧飞行 | 完成 | 完成 | 完成 | 完成 |
| Map/Upload/Readback | 完成 | 完成 | 完成 | 完成 |
| 纹理传输矩阵 | 通过 | 通过 | 通过 | 通过 |

### 测试

```bash
cmake -DBUILD_TESTING=ON ...
```

测试覆盖：资源语义、提交协调器 Enqueue/Activate/Complete/Cancel、Map/Upload/Readback、staging 复用、四后端纹理传输矩阵。

### 已知边界

- Ray Tracing、Mesh Shader、VRS、稀疏资源尚未进入公共契约
- 上层 RenderGraph 的资源别名与瞬态资源规划尚未实现
- WebGPU 和 Metal 后端功能覆盖有限

---

## English Documentation

### Core Types

#### Instance

```cpp
namespace fyuu_rhi {
    template <class Backend>
    static void Instance<Backend>::Initialize(
        std::string_view app_name,
        Version const& app_ver,
        std::string_view engine_name,
        Version const& engine_ver
    );

    template <class Backend>
    static Instance<Backend>* Instance<Backend>::Get();

    template <class Backend>
    auto Instance<Backend>::EnumeratePhysicalDevices() const
        -> std::vector<PhysicalDevice<Backend>>;
}
```

#### PhysicalDevice

```cpp
namespace fyuu_rhi {
    template <class Backend>
    class PhysicalDevice {
        PhysicalDeviceInfo GetInfo() const;
    };
}
```

`PhysicalDeviceInfo` provides: name, vendor/device ID, dedicated memory size, device type (DiscreteGPU / IntegratedGPU / CPU / Virtual).

`BestPerformance(devices)` selects the best device by prioritizing discrete GPUs over integrated, then by dedicated memory size.

#### LogicalDevice

```cpp
namespace fyuu_rhi {
    template <class Backend>
    class LogicalDevice {
        // Resource creation
        Resource<Backend> CreateBuffer(size_t size_bytes, ResourceFlags const& flags);
        Resource<Backend> CreateTexture(
            size_t width, size_t height, size_t depth_layers,
            size_t mip_count, ResourceFlags const& flags
        );

        // View creation
        View<Backend> CreateBufferView(Resource<Backend> const& buf, size_t offset, size_t range, ResourceFlags const& flags);
        View<Backend> CreateTextureView(Resource<Backend> const& tex, size_t base_mip, size_t mip_count, size_t base_layer, size_t layer_count, ResourceFlags const& flags);

        // Sampler
        Sampler<Backend> CreateSampler(SamplerDescriptor const& descriptor);

        // Pipeline
        pipeline::Pipeline<Backend> CreateGraphicsPipeline(pipeline::GraphicsPipelineDescriptor const& descriptor);
        pipeline::Pipeline<Backend> CreateComputePipeline(pipeline::ComputePipelineDescriptor const& descriptor);
        pipeline::PipelineResourceGroup<Backend> CreatePipelineResourceGroup(pipeline::Pipeline<Backend> const& pipe, uint32_t space, std::span<pipeline::PipelineResourceBinding<Backend> const> bindings);

        // Scheduler
        execution::Scheduler<Backend> CreateScheduler(execution::SchedulerDescriptor const& descriptor);

        // Command graph
        execution::CommandGraph<Backend> CreateCommandGraph(execution::CommandGraphDescriptor const& descriptor);
        execution::ExecutableGraph<Backend> CompileCommandGraph(execution::CommandGraph<Backend> const& graph);
    };
}
```

#### ResourceFlags

`ResourceFlagBits` encodes usage, dimension, heap location, and pixel format as bitflags, combined into `ResourceFlags`.

```cpp
// Usage
CopySRC, CopyDST, VertexBuffer, IndexBuffer, UniformBuffer, StorageBuffer,
TextureBinding, StorageBinding, RenderAttachment, TransientAttachment

// Dimension
Texture1D, Texture2D, Texture3D

// Heap location
DeviceLocal, HostVisible, DeviceReadback

// Formats (subset)
R8G8B8A8Unorm, R8G8B8A8Srgb, B8G8R8A8Srgb,
R32Float, R32G32B32A32Float,
D32Float, D24UnormS8Uint,
Bc1Unorm, Bc3Unorm, Bc5Unorm, Bc7UnormSrgb
```

#### Scheduler

```cpp
namespace fyuu_rhi::execution {
    struct SchedulerDescriptor {
        AtomicFlags<SchedulerFlagBits> flags;  // Graphics, Compute, Copy
        float priority = 0.5f;
    };

    template <class Backend>
    class Scheduler {
        Sender<Backend> schedule() const;
    };
}
```

#### CommandGraph

The command graph uses a builder pattern:

```cpp
namespace fyuu_rhi::execution {
    class CommandGraphBuilder {
        GraphResourceID RegisterResource();
        GraphPipelineID RegisterPipeline();
        GraphViewID RegisterView();
        GraphResourceGroupID RegisterResourceGroup();
        GraphPresentationID RegisterPresentationTarget();

        GraphNodeID AddNode(GraphNodeFlagBits flags);           // Graphics/Compute/Copy/Present
        void AddDependency(GraphNodeID node, GraphNodeID dep);
        void AddAccess(GraphNodeID node, GraphResourceAccess access);
        void AddCommand(GraphNodeID node, GraphCommand cmd);

        CommandGraphDescriptor Build();                         // validates + topo-sorts
    };

    // Node commands (std::variant):
    // BeginRenderingCommand, EndRenderingCommand, BindPipelineCommand,
    // BindResourceGroupCommand, BindVertexBufferCommand, SetViewportCommand,
    // SetScissorCommand, BindIndexBufferCommand, DrawCommand, DrawIndexedCommand,
    // DispatchCommand, CopyBufferCommand, CopyBufferToTextureCommand,
    // CopyTextureToBufferCommand, CopyTextureCommand, PresentCommand
}
```

#### Resource Transfer API

```cpp
// Map/Unmap
Map(scheduler, resource, range, flags)       // -> ResourceMapSender -> MappedResource
Unmap(mapped)                                // -> ResourceUnmapSender -> Resource

// Upload/Readback
Upload(device, scheduler, dest, offset, data)               // Buffer upload
Upload(device, scheduler, dest, layout, region, data)        // Texture upload
Readback(device, scheduler, source, range)                   // Buffer readback
Readback(device, scheduler, source, region, layout)          // Texture readback
```

### Backend Aliases

| Abstract Type | D3D12 | Vulkan | OpenGL | WebGPU |
|---------------|-------|--------|--------|--------|
| Instance | `D3D12Instance` | `VulkanInstance` | `OpenGLInstance` | `WebGPUInstance` |
| PhysicalDevice | `D3D12PhysicalDevice` | `VulkanPhysicalDevice` | `OpenGLPhysicalDevice` | `WebGPUPhysicalDevice` |
| LogicalDevice | `D3D12LogicalDevice` | `VulkanLogicalDevice` | `OpenGLLogicalDevice` | `WebGPULogicalDevice` |
| Resource | `D3D12Resource` | `VulkanResource` | `OpenGLResource` | `WebGPUResource` |
| Scheduler | `D3D12Scheduler` | `VulkanScheduler` | `OpenGLScheduler` | `WebGPUScheduler` |
| Pipeline | `pipeline::D3D12Pipeline` | `pipeline::VulkanPipeline` | `pipeline::OpenGLPipeline` | `pipeline::WebGPUPipeline` |
| CommandGraph | `execution::D3D12CommandGraph` | `execution::VulkanCommandGraph` | `execution::OpenGLCommandGraph` | `execution::WebGPUCommandGraph` |

### Usage Examples

#### Initialization

```cpp
import fyuu_rhi;

int main() {
    using Backend = fyuu_rhi::vulkan::Backend;

    fyuu_rhi::Instance<Backend>::Initialize(
        "MyApp",       fyuu_rhi::Version{ .major = 1, .minor = 0 },
        "FyuuEngine",  fyuu_rhi::Version{ .major = 0, .minor = 1 }
    );

    auto* instance = fyuu_rhi::Instance<Backend>::Get();
    auto phys_devices = instance->EnumeratePhysicalDevices();
    auto best_dev = fyuu_rhi::BestPerformance(phys_devices);
    auto logical_device = best_dev.CreateLogicalDevice();
}
```

#### Resource Creation

```cpp
using RFB = fyuu_rhi::ResourceFlagBits;

// Staging buffer (CPU writable, GPU readable)
auto staging = logical_device.CreateBuffer(64 * 1024,
    fyuu_rhi::ResourceFlags{}.Set(RFB::HostVisible).Set(RFB::CopySRC));

// GPU vertex buffer
auto vertices = logical_device.CreateBuffer(3 * sizeof(float) * 1024,
    fyuu_rhi::ResourceFlags{}.Set(RFB::DeviceLocal).Set(RFB::VertexBuffer).Set(RFB::CopyDST));

// 2D render target texture
auto render_target = logical_device.CreateTexture(1920, 1080, 1, 1,
    fyuu_rhi::ResourceFlags{}.Set(RFB::Texture2D).Set(RFB::RenderAttachment).Set(RFB::R8G8B8A8Unorm));

// Sampler
auto sampler = logical_device.CreateSampler({
    .address_mode_u = fyuu_rhi::AddressMode::ClampToEdge,
    .address_mode_v = fyuu_rhi::AddressMode::ClampToEdge,
    .mag_filter     = fyuu_rhi::FilterMode::Linear,
    .min_filter     = fyuu_rhi::FilterMode::Linear,
});
```

#### Pipeline Creation

```cpp
using namespace fyuu_rhi::pipeline;

auto pipeline = logical_device.CreateGraphicsPipeline({
    .program = {
        .modules = {{{
            .name   = "shader",
            .source = R"(
                struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };
                [shader("vertex")]
                VSOut vs_main(float3 pos : POSITION, float4 color : COLOR) {
                    VSOut out_val; out_val.pos = float4(pos, 1.0); out_val.color = color; return out_val;
                }
                [shader("fragment")]
                float4 fs_main(VSOut input) : SV_Target0 { return input.color; }
            )"
        }}},
        .entry_points = {{ {.name = "vs_main", .stage = PipelineStage::Vertex},
                           {.name = "fs_main", .stage = PipelineStage::Fragment} }}
    },
    .vertex = { .buffers = {{ {.slot = 0, .stride = 28} }},
                .attributes = {{ {.location = 0, .slot = 0, .offset = 0, .format = RFB::R32G32B32Float},
                                 {.location = 1, .slot = 0, .offset = 12, .format = RFB::R32G32B32A32Float} }} },
    .primitive = { .topology = PrimitiveTopology::TriangleList },
    .rasterization = { .front_face = FrontFace::CounterClockwise, .cull_mode = CullMode::Back },
    .color_targets = {{ {.format = RFB::R8G8B8A8Unorm} }}
});
```

#### Command Graph

```cpp
using namespace fyuu_rhi::execution;

CommandGraphBuilder builder;
auto rt_id = builder.RegisterResource();
auto pipe_id = builder.RegisterPipeline();
auto present_id = builder.RegisterPresentationTarget();

auto node = builder.AddNode(GraphNodeFlagBits::Graphics);
builder.AddAccess(node, { .resource = rt_id, .flags = GraphAccessFlagBits::Write | GraphAccessFlagBits::ColorAttachment });
builder.AddCommand(node, BeginRenderingCommand{
    .colors = {{ {.resource = rt_id, .load = false, .store = true, .clear_red = 0.1f, .clear_green = 0.1f, .clear_blue = 0.2f, .clear_alpha = 1.0f} }},
    .width = 1920, .height = 1080
});
builder.AddCommand(node, BindPipelineCommand{ .pipeline = pipe_id });
builder.AddCommand(node, DrawCommand{ .vertex_count = 3, .instance_count = 1 });
builder.AddCommand(node, EndRenderingCommand{});

auto present_node = builder.AddNode(GraphNodeFlagBits::Present);
builder.AddDependency(present_node, node);
builder.AddCommand(present_node, PresentCommand{ .source = rt_id, .target = present_id });

auto desc = builder.Build();
auto graph = logical_device.CreateCommandGraph(desc);
auto executable = logical_device.CompileCommandGraph(graph);

CommandGraphBindings<Backend> bindings(desc);
bindings.Bind(rt_id, render_target);
bindings.Bind(pipe_id, pipeline);
bindings.Bind(present_id, native_window_handle);

auto scheduler = logical_device.CreateScheduler({
    .flags = SchedulerDescriptor{}.Set(SchedulerFlagBits::Graphics), .priority = 1.0f
});

// Submit(scheduler, executable, bindings) returns a CommandGraphSender
```

#### Map/Unmap

```cpp
// Map for CPU write access
auto map_sender = Map(scheduler,
    std::move(host_visible_buffer),
    ResourceDataRange{ .offset = 0, .size = 4096 },
    ResourceMapFlags{}.Set(ResourceMapFlagBits::Write));
```

#### Upload

```cpp
// Buffer upload
auto upload_sender = Upload(logical_device, scheduler,
    std::move(gpu_buffer), 0u, cpu_data_span);

// Texture upload
auto tex_upload_sender = Upload(logical_device, scheduler,
    std::move(gpu_texture),
    TextureDataLayout{ .bytes_per_row = 1024, .rows_per_image = 256 },
    TextureRegion{ .mip_level = 0, .base_array_layer = 0, .array_layer_count = 1, .width = 256, .height = 256, .depth = 1 },
    data_span);
```

#### Readback

```cpp
// Buffer readback
auto readback_sender = Readback(logical_device, scheduler,
    std::move(gpu_buffer), ResourceDataRange{ .offset = 0, .size = 4096 });

// Texture readback
auto tex_readback_sender = Readback(logical_device, scheduler,
    std::move(gpu_texture),
    TextureRegion{ .mip_level = 0, .base_array_layer = 0, .array_layer_count = 1, .width = 256, .height = 256, .depth = 1 },
    TextureDataLayout{ .bytes_per_row = 1024, .rows_per_image = 256 });
```

### Design Principles

- `LogicalDevice` is a factory only, not involved in execution
- `Resource` / `View` / `Sampler` / `Pipeline` are move-only value types
- Format is expressed through `ResourceFlagBits` rather than a separate format enum
- Command Buffer / Fence / Semaphore / SwapChain are not exposed publicly
- `Scheduler` follows the `std::execution::scheduler_t` concept, compatible with P2300 sender/receiver
- Present is a command graph command; swapchain management is internal to backends

### Backend Status

| Capability | D3D12 | Vulkan | OpenGL/ES | WebGPU |
|------------|-------|--------|-----------|--------|
| Graphics Pipeline | Done | Done | Done | Done |
| Compute Pipeline | Done | Done | Done | Done |
| CommandGraph Execution | Done | Done | Done | Done |
| Draw/Dispatch/Copy | Done | Done | Done | Done |
| Present & Frame Pacing | Done | Done | Done | Done |
| Map/Upload/Readback | Done | Done | Done | Done |
| Texture Transfer Matrix | Pass | Pass | Pass | Pass |

### Tests

```bash
cmake -DBUILD_TESTING=ON ...
```

Coverage: resource semantics, submission coordinator Enqueue/Activate/Complete/Cancel, Map/Upload/Readback, staging reuse, multi-backend texture transfer matrix.

### Known Limitations

- Ray Tracing, Mesh Shader, VRS, sparse resources are not yet in the public contract
- RenderGraph resource aliasing and transient resource planning are not yet implemented
- WebGPU and Metal backend coverage is limited
