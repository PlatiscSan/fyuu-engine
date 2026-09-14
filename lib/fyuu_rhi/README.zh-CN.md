# FyuuRHI

[English](README.md)

FyuuRHI 是 FyuuEngine 的底层渲染接口。它提供统一的 C++23 API，涵盖设备发现、资源与管线创建、命令图执行、同步和呈现，并通过不透明句柄隔离各图形 API 的原生对象。

接口仍在持续演进。某个后端出现在当前构建中，只表示当前程序可以选择它，并不代表它在所有目标平台上都经过了验证。

## 后端状态

| 后端 | 目标平台 | 当前验证情况 |
| --- | --- | --- |
| Direct3D 12 | Windows | 已在 Windows 构建并运行测试 |
| Vulkan | Windows、Linux | 已在 Windows 运行测试；Linux 仍需实机验证 |
| OpenGL | Windows、Linux | 提供 WGL、GLX、EGL 路径；已在 Windows 运行测试 |
| WebGPU | Windows、Linux、Apple 平台 | 基于 Dawn；已在 Windows 运行测试 |
| Metal | macOS、iOS | 已实现，但**尚未在 Apple 硬件上验证** |

Metal 目前仍是实验后端。在 Apple 工具链和真实设备上通过呈现、窗口缩放、诊断与退出测试之前，不应将它用于生产环境。

## 构建与导入

FyuuRHI 需要 CMake 3.28 或更高版本、C++23，以及支持命名模块的编译器。应用链接库目标后，导入公开模块即可：

```cmake
target_link_libraries(MyApplication PRIVATE FyuuRHI)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

```cpp
import fyuu_rhi;
```

可用后端取决于目标平台以及配置阶段找到的依赖。应用不应假定某个后端必然存在，而应查询当前构建实际提供的后端：

```cpp
for (auto backend : fyuu_rhi::EnumerateBackends()) {
    // 按应用自身的策略选择后端。
}
```

## 对象模型

公开对象按照渲染设备的生命周期组织：

1. `Instance` 枚举某个后端提供的物理设备。
2. `PhysicalDevice` 描述一个适配器，并创建 `LogicalDevice`。
3. `LogicalDevice` 创建资源、采样器、管线和调度器。
4. `Resource` 为自身创建缓冲区 View 或纹理 View。
5. `Pipeline` 按 Shader 反射结果创建资源组。
6. `CommandScheduler` 构建并执行命令图。

原生对象不会暴露到公开接口中。不同后端或不同逻辑设备创建的对象不能混用。

大多数 GPU 对象采用独占所有权，只能移动；`CommandScheduler` 可以复制，副本共享同一个调度上下文。命令图启动后，operation 会接管所有绑定对象；执行成功、失败或取消时，这些对象都会交还给 receiver。

## 初始化与选择设备

请求后端实例前，必须先初始化进程级 RHI 上下文。RHI 会同步地向日志 Sink 写入消息，因此 Sink 必须存活到最后一次 RHI 调用结束。

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);
```

`RequestInstance` 通过回调交付创建完成的实例：

```cpp
fyuu_rhi::RequestInstance(
    fyuu_rhi::Backend::Vulkan,
    [](fyuu_rhi::Instance instance) {
        auto physical_devices = instance.EnumeratePhysicalDevices();
        auto const& physical_device = fyuu_rhi::BestPerformance(physical_devices);
        auto device = physical_device.CreateLogicalDevice();
        auto scheduler = device.CreateScheduler();

        // 在这里创建资源并提交命令图。
    }
);
```

`PhysicalDevice::GetInfo()` 返回适配器名称和类型，以及可选的厂商编号、设备编号和专用显存容量。可选值缺失表示后端无法提供相应信息，而不是该项数值为零。

使用 OpenGL 后端时，可调用 `Instance::ShareContextOnThisThread()`，将共享上下文绑定到当前线程。其他后端不需要这一步，因此该函数不会执行任何操作。

## 资源与 View

`ResourceFlags` 描述资源的内存位置、用途、格式、纹理形状、采样数，以及允许创建的 View 类型。这些标志属于正确性约束：后端会拒绝互相矛盾的组合，命令中使用资源的方式也必须在创建资源时提前声明。

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

auto texture = device.CreateTexture(width, height, 1u, 1u, flags);
auto view = texture.CreateTextureView(0u, 1u, 0u, 1u, flags);
```

View 由资源自身创建，使实现能够在不暴露原生句柄的情况下检查后端和设备归属。缓冲区 View 使用 `CreateBufferView()` 创建。例如，经 `WriteBuffer` 写入的缓冲区必须包含 `CopyDST`；只有 `VertexBuffer` 标志并不足以允许上传。

## Shader、Pipeline 与资源组

管线通过 Slang 程序描述符接收 Shader。FyuuRHI 会编译其中声明的模块，并通过反射得到资源接口。创建图形管线时，还需要描述顶点输入、图元拓扑、光栅化、多重采样、深度模板、混合和附件格式。

```cpp
auto pipeline = device.CreateGraphicsPipeline(
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

资源组由对应管线创建，其中的 space、slot、数组元素和绑定类型必须与 Shader 反射接口一致。FyuuRHI 同时支持纹理与采样器分离绑定，以及显式的组合绑定：

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

## 命令图

命令图由绑定槽、带依赖关系的节点、逻辑队列需求、资源访问声明和具体命令组成。使用 `CommandScheduler` 创建命令图：

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

资源访问声明不是优化提示，而是命令图的正确性契约。后端会根据这些声明划分批次、转换资源状态、插入屏障、同步队列，并处理队列族所有权转移。`QueueType` 只表示节点需要哪类队列能力，不保证该节点会获得一条独立的物理队列。

## 呈现

呈现也是命令图的一部分，通常依赖产生呈现源的渲染节点：

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

使用 `SetPresentationTarget()` 绑定原生窗口。窗口及其事件循环仍由应用负责管理；应用需要处理平台事件和尺寸变化，并在窗口关闭前持续提交帧。

## 启动任务与接收完成结果

命令图构建完成后，将它连接到 receiver，为所有已注册槽位绑定对象，然后启动 operation：

```cpp
auto operation = std::move(builder).connect(Receiver{ state });
operation.BindResource(target_resource, std::move(texture));
operation.BindView(target_view, std::move(view));
operation.BindPipeline(graphics_pipeline, std::move(pipeline));
operation.SetPresentationTarget(native_window);
operation.start();
```

Receiver 提供 `get_env()`，并且只接收一次终止信号：

- 成功时调用 `set_value(CommandGraphResources&&) &&`；
- 失败时调用 `set_error(std::exception_ptr) &&`；
- 取消时调用 `set_stopped() &&`。

发送 error 或 stopped 信号前，`RecoverBindings(CommandGraphResources&&)` 会先归还 operation 持有的对象；执行成功时，这些对象直接随 `set_value` 返回。使用 `TakeResource`、`TakeView`、`TakeSampler`、`TakePipeline` 或 `TakeResourceGroup` 取回对应对象，每个槽位只能取出一次。

完成回调可能在内部工作线程上执行。Receiver 必须自行同步与该线程共享的应用状态。Stop 请求只在原生提交前有效；工作一旦提交到 GPU，通常就无法取消。

## 验证与诊断

命令执行分为准备、并行录制和队列提交三个阶段。绑定错误会在提交前报告，录制错误和 GPU 错误则通过 receiver 的 error 通道返回。

后端诊断会写入配置的 `fyuu_rhi::log::Sink`。自动化测试不能只检查进程退出码；即使进程正常退出，Error 或 Fatal 级别的日志仍应视为失败。

跨后端集成测试位于 `lib/fyuu_rhi/test/hello_triangle`。启用测试后，CTest 会为当前构建中的每个后端注册一次有限帧测试；也可以直接运行程序进行交互测试：

```text
HelloTriangle <d3d12|vulkan|opengl|webgpu|metal>
```

该测试覆盖设备创建、数据上传、管线与资源组创建、命令录制、绘制、呈现、完成处理和窗口缩放。只有在 Apple 硬件上通过真实窗口与事件循环测试后，Metal 才会列入已经验证的后端。
