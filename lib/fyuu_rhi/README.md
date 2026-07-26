# FyuuRHI

跨 API 渲染硬件接口 / Cross-API Render Hardware Interface

FyuuRHI 是 FyuuEngine 的跨 API 渲染硬件抽象层，提供统一的 D3D12 / Vulkan / OpenGL(ES) / WebGPU 后端抽象。

FyuuRHI is the cross-API render hardware abstraction layer for FyuuEngine, providing unified backend abstraction across D3D12, Vulkan, OpenGL(ES), and WebGPU.

---

## 核心类型 / Core Types

### Instance / 实例

```cpp
namespace fyuu_rhi {
    // 初始化：每个后端只调用一次
    template <class Backend>
    static void Instance<Backend>::Initialize(
        std::string_view app_name,
        Version const& app_ver,        // {variant, major, minor, patch}
        std::string_view engine_name,
        Version const& engine_ver
    );

    // 获取单例指针
    template <class Backend>
    static Instance<Backend>* Instance<Backend>::Get();

    // 枚举物理设备（GPU）
    template <class Backend>
    auto Instance<Backend>::EnumeratePhysicalDevices() const
        -> std::vector<PhysicalDevice<Backend>>;
}
```

### PhysicalDevice / 物理设备

```cpp
namespace fyuu_rhi {
    template <class Backend>
    class PhysicalDevice {
        // 获取设备信息
        PhysicalDeviceInfo GetInfo() const;
    };

    // -- PhysicalDeviceInfo --
    // struct PhysicalDeviceInfo {
    //     std::string               name;             // e.g. "NVIDIA GeForce RTX 4090"
    //     std::optional<uint32_t>   vendor_id;
    //     std::optional<uint32_t>   device_id;
    //     std::optional<uint64_t>   dedicated_memory; // bytes
    //     enum class Type : uint8_t { DiscreteGPU, IntegratedGPU, CPU, Virtual };
    //     Type type;
    // };

    // 工具：从设备列表中选择最佳 GPU
    // 优先离散 GPU，其次集显，按专用内存排序
    template <class T>
    T BestPerformance(std::span<T const> devices);
}
```

### LogicalDevice / 逻辑设备

```cpp
namespace fyuu_rhi {
    template <class Backend>
    class LogicalDevice {
        // -- 资源创建 --
        Resource<Backend> CreateBuffer(size_t size_bytes, ResourceFlags const& flags);
        Resource<Backend> CreateTexture(
            size_t width, size_t height, size_t depth_layers,
            size_t mip_count, ResourceFlags const& flags
        );

        // -- 视图创建 --
        View<Backend> CreateBufferView(
            Resource<Backend> const& buf,
            size_t offset, size_t range, ResourceFlags const& flags
        );
        View<Backend> CreateTextureView(
            Resource<Backend> const& tex,
            size_t base_mip, size_t mip_count,
            size_t base_layer, size_t layer_count,
            ResourceFlags const& flags
        );

        // -- 采样器创建 --
        Sampler<Backend> CreateSampler(SamplerDescriptor const& descriptor);

        // -- 管线创建 --
        pipeline::Pipeline<Backend> CreateGraphicsPipeline(
            pipeline::GraphicsPipelineDescriptor const& descriptor
        );
        pipeline::Pipeline<Backend> CreateComputePipeline(
            pipeline::ComputePipelineDescriptor const& descriptor
        );
        pipeline::PipelineResourceGroup<Backend> CreatePipelineResourceGroup(
            pipeline::Pipeline<Backend> const& pipeline,
            uint32_t space,
            std::span<pipeline::PipelineResourceBinding<Backend> const> bindings
        );

        // -- 调度器创建 --
        execution::Scheduler<Backend> CreateScheduler(
            execution::SchedulerDescriptor const& descriptor
        );

        // -- 命令图创建与编译 --
        execution::CommandGraph<Backend> CreateCommandGraph(
            execution::CommandGraphDescriptor const& descriptor
        );
        execution::ExecutableGraph<Backend> CompileCommandGraph(
            execution::CommandGraph<Backend> const& graph
        );
    };
}
```

### ResourceFlags / 资源标志

资源类型和格式通过 `ResourceFlagBits` 标志位组合描述：

```cpp
// 用法标志
ResourceFlagBits::CopySRC          ResourceFlagBits::CopyDST
ResourceFlagBits::VertexBuffer     ResourceFlagBits::IndexBuffer
ResourceFlagBits::UniformBuffer    ResourceFlagBits::StorageBuffer
ResourceFlagBits::TextureBinding   ResourceFlagBits::StorageBinding
ResourceFlagBits::RenderAttachment ResourceFlagBits::TransientAttachment

// 维度
ResourceFlagBits::Texture1D        ResourceFlagBits::Texture2D       ResourceFlagBits::Texture3D

// 堆位置
ResourceFlagBits::DeviceLocal      ResourceFlagBits::HostVisible    ResourceFlagBits::DeviceReadback

// 格式（部分示例）
ResourceFlagBits::R8G8B8A8Unorm   ResourceFlagBits::R8G8B8A8Srgb   ResourceFlagBits::B8G8R8A8Srgb
ResourceFlagBits::R32Float         ResourceFlagBits::R32G32B32A32Float
ResourceFlagBits::D32Float         ResourceFlagBits::D24UnormS8Uint
ResourceFlagBits::Bc3Unorm         ResourceFlagBits::Bc5Unorm       ResourceFlagBits::Bc7UnormSrgb
```

包装为 `plastic::concurrency::AtomicFlags<ResourceFlagBits>`（线程安全位标志）。

### Scheduler / 调度器

```cpp
namespace fyuu_rhi::execution {
    // 调度器标志
    enum class SchedulerFlagBits : uint8_t {
        Graphics, Compute, Copy
    };
    struct SchedulerDescriptor {
        AtomicFlags<SchedulerFlagBits> flags;
        float priority = 0.5f;
    };

    template <class Backend>
    class Scheduler {
        // 创建发送器（符合 std::execution::scheduler_t 概念）
        Sender<Backend> schedule() const;
    };

    // Sender 产生 SchedulerCompletion：
    // struct SchedulerCompletion {
    //     void* operation;
    //     void (*SetValue)(void*);
    //     void (*SetError)(void*, std::exception_ptr const&);
    //     void (*SetStopped)(void*);
    // };
}
```

### CommandGraph / 命令图

```cpp
namespace fyuu_rhi::execution {
    // -- 命令图构建器 --
    class CommandGraphBuilder {
        GraphResourceID RegisterResource();
        GraphPipelineID RegisterPipeline();
        GraphViewID     RegisterView();
        GraphResourceGroupID RegisterResourceGroup();
        GraphPresentationID  RegisterPresentationTarget();

        GraphNodeID AddNode(GraphNodeFlagBits flags);
        void AddDependency(GraphNodeID node, GraphNodeID dependency);
        void AddAccess(GraphNodeID node, GraphResourceAccess access);
        void AddCommand(GraphNodeID node, GraphCommand command);

        CommandGraphDescriptor Build();  // 自动验证 + 拓扑排序
    };

    // -- 命令变体（GraphCommand = std::variant<...>） --
    struct BeginRenderingCommand { /* colors, depth_stencil, viewport */ };
    struct EndRenderingCommand {};
    struct BindPipelineCommand   { GraphPipelineID pipeline; };
    struct BindResourceGroupCommand { GraphResourceGroupID group; uint32_t index; };
    struct DrawCommand           { uint32_t vertex_count, instance_count, ...; };
    struct DrawIndexedCommand    { uint32_t index_count, instance_count, ...; };
    struct DispatchCommand       { uint32_t group_count_x, y, z; };
    struct CopyBufferCommand     { GraphResourceID src, dst; size_t offsets, size; };
    struct PresentCommand        { GraphResourceID source; GraphPresentationID target; };

    // -- 命令图绑定 --
    template <class Backend>
    class CommandGraphBindings {
        CommandGraphBindings(CommandGraphDescriptor const& descriptor);
        void Bind(GraphResourceID id, Resource<Backend> const& resource);
        void Bind(GraphPipelineID id, pipeline::Pipeline<Backend> const& pipeline);
        void Bind(GraphViewID id, View<Backend> const& view);
        void Bind(GraphResourceGroupID id, pipeline::PipelineResourceGroup<Backend> const& group);
        void Bind(GraphPresentationID id, Backend::PresentationTarget const& target);
    };

    // -- 提交 --
    template <class Backend>
    CommandGraphSender<Backend> Submit(
        Scheduler<Backend> const& scheduler,
        ExecutableGraph<Backend> const& graph,
        CommandGraphBindings<Backend>& bindings
    );
}
```

### Resource Mapping / 资源映射

```cpp
namespace fyuu_rhi::execution {
    template <class Backend>
    class MappedResource {
        // 只读访问
        std::span<std::byte const> Bytes() const;
        // 可写访问（需要 Write 标志）
        std::span<std::byte> WritableBytes();
        ResourceDataRange Range() const;
    };

    // Map sender：返回 MappedResource
    template <class Backend>
    ResourceMapSender<Backend> Map(
        Scheduler<Backend> const& scheduler,
        Resource<Backend>&& resource,
        ResourceDataRange const& range,
        ResourceMapFlags const& flags      // Read 或 Write
    );

    // Unmap sender：返回 Resource
    template <class Backend>
    ResourceUnmapSender<Backend> Unmap(
        MappedResource<Backend>&& mapped
    );
}
```

### Resource Transfer / 资源传输

```cpp
namespace fyuu_rhi::execution {
    // Buffer Upload
    template <class Backend>
    ResourceUploadSender<Backend> Upload(
        LogicalDevice<Backend>& device,
        Scheduler<Backend> const& scheduler,
        Resource<Backend>&& destination,
        size_t destination_offset,
        std::span<std::byte const> data
    );

    // Buffer Readback
    template <class Backend>
    ResourceReadbackSender<Backend> Readback(
        LogicalDevice<Backend>& device,
        Scheduler<Backend> const& scheduler,
        Resource<Backend>&& source,
        ResourceDataRange const& range
    );

    // Texture Upload
    template <class Backend>
    ResourceUploadSender<Backend> Upload(
        LogicalDevice<Backend>& device,
        Scheduler<Backend> const& scheduler,
        Resource<Backend>&& destination,
        TextureDataLayout const& layout,
        TextureRegion const& region,
        std::span<std::byte const> data
    );

    // Texture Readback
    template <class Backend>
    ResourceReadbackSender<Backend> Readback(
        LogicalDevice<Backend>& device,
        Scheduler<Backend> const& scheduler,
        Resource<Backend>&& source,
        TextureRegion const& region,
        TextureDataLayout const& layout
    );
}
```

---

## 后端别名 / Backend Aliases

### Windows

| 抽象类型 | D3D12 别名 |
|----------|-----------|
| Instance | `fyuu_rhi::D3D12Instance` |
| PhysicalDevice | `fyuu_rhi::D3D12PhysicalDevice` |
| LogicalDevice | `fyuu_rhi::D3D12LogicalDevice` |
| Resource | `fyuu_rhi::D3D12Resource` |
| View | `fyuu_rhi::D3D12View` |
| Sampler | `fyuu_rhi::D3D12Sampler` |
| Pipeline | `fyuu_rhi::pipeline::D3D12Pipeline` |
| PipelineResourceGroup | `fyuu_rhi::pipeline::D3D12PipelineResourceGroup` |
| Scheduler | `fyuu_rhi::execution::D3D12Scheduler` |
| CommandGraph | `fyuu_rhi::execution::D3D12CommandGraph` |
| ExecutableGraph | `fyuu_rhi::execution::D3D12ExecutableGraph` |

### Windows / Linux (非 Apple)

| 抽象类型 | Vulkan 别名 | OpenGL 别名 |
|----------|-------------|-------------|
| Instance | `fyuu_rhi::VulkanInstance` | `fyuu_rhi::OpenGLInstance` |
| PhysicalDevice | `fyuu_rhi::VulkanPhysicalDevice` | `fyuu_rhi::OpenGLPhysicalDevice` |
| LogicalDevice | `fyuu_rhi::VulkanLogicalDevice` | `fyuu_rhi::OpenGLLogicalDevice` |
| Resource | `fyuu_rhi::VulkanResource` | `fyuu_rhi::OpenGLResource` |
| Scheduler | `fyuu_rhi::execution::VulkanScheduler` | `fyuu_rhi::execution::OpenGLScheduler` |
| Pipeline | `fyuu_rhi::pipeline::VulkanPipeline` | `fyuu_rhi::pipeline::OpenGLPipeline` |

### 全平台 / All Platforms

| 抽象类型 | WebGPU 别名 |
|----------|-------------|
| Instance | `fyuu_rhi::WebGPUInstance` |
| Pipeline | `fyuu_rhi::pipeline::WebGPUPipeline` |

---

## 使用示例 / Usage Examples

### 1. 初始化与设备选择 / Initialization & Device Selection

```cpp
import fyuu_rhi;

int main() {
    using Backend = fyuu_rhi::d3d12::Backend;  // or vulkan::Backend, opengl::Backend

    // 初始化实例 / Initialize the backend instance
    fyuu_rhi::Instance<Backend>::Initialize(
        "MyApp",       fyuu_rhi::Version{ .major = 1, .minor = 0 },
        "FyuuEngine",  fyuu_rhi::Version{ .major = 0, .minor = 1 }
    );

    auto* instance = fyuu_rhi::Instance<Backend>::Get();
    auto phys_devices = instance->EnumeratePhysicalDevices();

    // 选择最佳 GPU / Pick the best GPU
    auto best_dev = fyuu_rhi::BestPerformance(phys_devices);
    auto info = best_dev.GetInfo();
    // info.name, info.type   e.g. "NVIDIA GeForce RTX 4090", DiscreteGPU

    // 创建逻辑设备 / Create the logical device
    auto logical_device = best_dev.CreateLogicalDevice();
}
```

### 2. 资源创建 / Resource Creation

```cpp
using RFB = fyuu_rhi::ResourceFlagBits;

// Buffer: 上传用暂存缓冲 / Staging buffer for upload
auto staging = logical_device.CreateBuffer(
    64 * 1024,
    fyuu_rhi::ResourceFlags{}  // flags are atomic, start empty
        .Set(RFB::HostVisible).Set(RFB::CopySRC)
);

// Buffer: GPU 顶点缓冲 / GPU vertex buffer
auto vertices = logical_device.CreateBuffer(
    3 * sizeof(float) * 1024,
    fyuu_rhi::ResourceFlags{}
        .Set(RFB::DeviceLocal).Set(RFB::VertexBuffer).Set(RFB::CopyDST)
);

// Texture: 2D 渲染目标 / 2D render target
auto render_target = logical_device.CreateTexture(
    1920, 1080, 1, 1,  // width, height, depth_layers, mip_count
    fyuu_rhi::ResourceFlags{}
        .Set(RFB::Texture2D).Set(RFB::RenderAttachment)
        .Set(RFB::R8G8B8A8Unorm)
);

// Sampler
auto sampler = logical_device.CreateSampler({
    .address_mode_u = fyuu_rhi::AddressMode::ClampToEdge,
    .address_mode_v = fyuu_rhi::AddressMode::ClampToEdge,
    .mag_filter     = fyuu_rhi::FilterMode::Linear,
    .min_filter     = fyuu_rhi::FilterMode::Linear,
});
```

### 3. 管线创建 / Pipeline Creation

```cpp
using namespace fyuu_rhi::pipeline;

auto pipeline = logical_device.CreateGraphicsPipeline({
    .program = SlangPipelineProgramDescriptor{
        .modules = {{
            SlangPipelineProgramDescriptor::Module{
                .name   = "shader",
                .source = R"(
                    struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };
                    [shader("vertex")]
                    VSOut vs_main(float3 pos : POSITION, float4 color : COLOR) {
                        VSOut out_val;
                        out_val.pos   = float4(pos, 1.0);
                        out_val.color = color;
                        return out_val;
                    }
                    [shader("fragment")]
                    float4 fs_main(VSOut input) : SV_Target0 {
                        return input.color;
                    }
                )"
            }
        }},
        .entry_points = {{
            {.name = "vs_main", .stage = PipelineStage::Vertex},
            {.name = "fs_main", .stage = PipelineStage::Fragment}
        }}
    },
    .vertex = VertexState{
        .buffers = {{ VertexBufferLayout{ .slot = 0, .stride = 28 } }},
        .attributes = {{
            { .location = 0, .slot = 0, .offset = 0, .format = RFB::R32G32B32Float },
            { .location = 1, .slot = 0, .offset = 12, .format = RFB::R32G32B32A32Float }
        }}
    },
    .primitive = PrimitiveState{ .topology = PrimitiveTopology::TriangleList },
    .rasterization = RasterizationState{
        .front_face = FrontFace::CounterClockwise,
        .cull_mode  = CullMode::Back
    },
    .color_targets = {{ ColorTargetState{ .format = RFB::R8G8B8A8Unorm } }}
});
```

### 4. 命令图构建与提交 / Command Graph Build & Submit

```cpp
using namespace fyuu_rhi::execution;

// 构建命令图 / Build the command graph
CommandGraphBuilder builder;

auto res_id     = builder.RegisterResource();
auto rt_id      = builder.RegisterResource();
auto pipe_id    = builder.RegisterPipeline();
auto present_id = builder.RegisterPresentationTarget();

auto node = builder.AddNode(GraphNodeFlagBits::Graphics);
builder.AddAccess(node, {
    .resource = rt_id,
    .flags    = GraphAccessFlagBits::Write | GraphAccessFlagBits::ColorAttachment
});

builder.AddCommand(node, BeginRenderingCommand{
    .colors = {{ GraphColorAttachment{ .resource = rt_id, .view = rt_view_id,
                                       .load = false, .store = true,
                                       .clear_red = 0.1f, .clear_green = 0.1f,
                                       .clear_blue = 0.2f, .clear_alpha = 1.0f } }},
    .width = 1920, .height = 1080
});
builder.AddCommand(node, BindPipelineCommand{ .pipeline = pipe_id });
builder.AddCommand(node, DrawCommand{ .vertex_count = 3, .instance_count = 1 });
builder.AddCommand(node, EndRenderingCommand{});

auto present_node = builder.AddNode(GraphNodeFlagBits::Present);
builder.AddDependency(present_node, node);
builder.AddCommand(present_node, PresentCommand{
    .source = rt_id, .target = present_id, .vertical_sync = true, .frames_in_flight = 3
});

auto desc = builder.Build();  // 验证 + 拓扑排序 / validates & topo-sorts

// 创建编译命令图 / Create & compile
auto graph          = logical_device.CreateCommandGraph(desc);
auto executable     = logical_device.CompileCommandGraph(graph);

// 绑定真实对象 / Bind concrete objects
CommandGraphBindings<Backend> bindings(desc);
bindings.Bind(res_id,     vertex_buffer);
bindings.Bind(rt_id,      render_target);
bindings.Bind(pipe_id,    pipeline);
bindings.Bind(present_id, native_window_handle);

// 创建调度器 / Create a scheduler
auto scheduler = logical_device.CreateScheduler({
    .flags = SchedulerDescriptor{}.Set(SchedulerFlagBits::Graphics),
    .priority = 1.0f
});

// 提交并等待完成（基于 Sender/Receiver 的异步方案见下文）
// Submit (async via Sender/Receiver — see next example)
```

### 5. 异步资源传输 / Async Resource Upload

```cpp
// 'data' 是 CPU 数据的 span
// upload 返回 ResourceUploadSender
auto sender = Upload(logical_device, scheduler,
    std::move(gpu_buffer),       // destination GPU buffer
    0u,                          // destination offset
    data                         // source CPU data
);

// 使用手动 Receiver 接收结果
struct MyReceiver {
    void set_value(fyuu_rhi::Resource<Backend> resource) && noexcept {
        // Upload 完成，resource 是可用的 GPU 缓冲
    }
    void set_error(std::exception_ptr const& e) && noexcept {
        try { std::rethrow_exception(e); }
        catch (std::exception const& ex) { /* 处理错误 / handle error */ }
    }
    void set_stopped() && noexcept {
        // 操作被取消
    }
    std::stop_token get_stop_token() const noexcept { return {}; }
};

auto op_state = std::move(sender).connect(MyReceiver{});
op_state.start();  // 启动异步操作
```

### 6. Map / Unmap 操作 / Map / Unmap Operations

```cpp
// Map GPU 缓冲以供 CPU 写入 / Map GPU buffer for CPU write
auto map_sender = Map(scheduler,
    std::move(host_visible_buffer),
    ResourceDataRange{ .offset = 0, .size = 4096 },
    ResourceMapFlags{}.Set(ResourceMapFlagBits::Write)
);

// 链式：Map → 写入 → Unmap（需要手动串接 receiver）
// 简化同步使用模式：
struct SyncReceiver {
    void set_value(auto...) && noexcept { /* 完成 */ }
    void set_error(std::exception_ptr const& e) && noexcept { /* 处理 */ }
    void set_stopped() && noexcept { /* 取消 */ }
};
```

### 7. 纹理 Readback / Texture Readback

```cpp
auto readback_sender = Readback(logical_device, scheduler,
    std::move(gpu_texture),
    TextureRegion{
        .mip_level      = 0,
        .base_array_layer = 0, .array_layer_count = 1,
        .width = 256, .height = 256, .depth = 1
    },
    TextureDataLayout{
        .bytes_per_row = 1024,
        .rows_per_image = 256
    }
);

// ResourceReadbackSender 在完成时提供：Resource<Backend> + std::vector<std::byte>
```

---

## 设计概要 / Design Overview

| 原则 / Principle | 说明 / Description |
|---|---|
| `LogicalDevice` 只承担工厂职责 | 所有资源与执行对象的创建集中管理 |
| Resource/View/Sampler/Pipeline 是 move-only 值对象 | 禁止复制，所有权明确 |
| Format 通过 ResourceFlagBits 表达 | 无意使用独立 Format 枚举 |
| Command Buffer 不向上层暴露 | 命令图是唯一工作描述形式 |
| Scheduler 遵循 `std::execution::scheduler_t` 概念 | 为 P2300 Sender/Receiver 预留兼容入口 |
| SwapChain/Surface 不是公开契约 | Present 是命令图命令，实现由后端管理 |

### 后端状态 / Backend Status

| 能力 | D3D12 | Vulkan | OpenGL / ES | WebGPU |
|------|-------|--------|-------------|--------|
| Graphics Pipeline | 完成 | 完成 | 完成 | 完成 |
| Compute Pipeline | 完成 | 完成 | 完成 | 完成 |
| CommandGraph Execution | 完成 | 完成 | 完成 | 完成 |
| Draw / Dispatch / Copy | 完成 | 完成 | 完成 | 完成 |
| Present & Frame Pacing | 完成 | 完成 | 完成 | 完成 |
| Map / Upload / Readback | 完成 | 完成 | 完成 | 完成 |
| Texture Transfer Matrix | 通过 | 通过 | 通过 | 通过 |

---

## 构建集成 / Build Integration

```cmake
# 在 CMakeLists.txt 中：
add_subdirectory(path/to/fyuu_rhi)
target_link_libraries(your_target PRIVATE FyuuRHI)

# 依赖项（由 CMake 自动查找）：
#   Boost, TBB, Vulkan 1.3.256+, VMA, Dawn, Slang,
#   SPIRV-Cross, glslang, DirectX-DXC, nlohmann_json
# Windows 额外：DirectX-Headers, DirectXTK12, D3D12MA, WIL, WinPixEventRuntime
```

---

## 测试 / Tests

```bash
# 启用测试构建
cmake -DBUILD_TESTING=ON ...
# 测试覆盖：
# - 资源身份与 move-only 语义
# - 资源提交协调器  Enqueue/Activate/Complete/Cancel
# - 资源 Map/Upload/Readback
# - Staging 资源复用
# - 四后端纹理传输矩阵（tight/non-tight row, mip, array, 3D slice）
```

---

## 已知边界 / Known Limitations

- Ray Tracing、Mesh Shader、VRS、稀疏资源等高级特性尚未进入契约
- 上层 RenderGraph 的资源别名与瞬态资源规划尚未实现
- WebGPU 和 Metal 后端功能覆盖相对有限
- 建议后续优先从上层渲染系统反向验证接口
