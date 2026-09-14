# FyuuRHI

[English](README.md)

FyuuRHI 是 FyuuEngine 的底层渲染接口。它用一套 C++23 API 统一设备发现、资源与
管线创建、命令图执行、同步和呈现，同时把各图形 API 的原生对象封装在与后端无关
的句柄之后。

接口仍在持续演进。某个后端出现在当前构建中，只说明它可以被选择，并不代表该
后端的所有平台路径都已完成验证。

## 后端状态

| 后端 | 目标平台 | 当前验证情况 |
| --- | --- | --- |
| Direct3D 12 | Windows | 已在 Windows 构建并运行测试 |
| Vulkan | Windows、Linux | 已在 Windows 运行测试；Linux 仍需实机验证 |
| OpenGL | Windows、Linux | 提供 WGL、GLX、EGL 路径；已在 Windows 运行测试 |
| WebGPU | Windows、Linux、Apple 平台 | 基于 Dawn；已在 Windows 运行测试 |
| Metal | macOS、iOS | 已实现，但**尚未在 Apple 硬件上验证** |

Metal 目前是实验后端。在 Apple 工具链和真实设备上通过呈现、窗口缩放、诊断与
退出测试之前，不应将它视为生产可用。

## 构建与导入

FyuuRHI 需要 CMake 3.28 或更高版本、C++23，以及支持命名模块的编译器。应用链接
库目标后，导入公开模块即可：

```cmake
target_link_libraries(MyApplication PRIVATE FyuuRHI)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

```cpp
import fyuu_rhi;
```

可用后端由目标平台和配置阶段找到的依赖决定。不要在应用中假定某个后端必然
存在，应先查询当前构建：

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

大多数 GPU 对象都是只移动的所有者；`CommandScheduler` 可以复制，并共享调度
上下文。命令图启动时，所有绑定对象会被移动到 operation 中；无论成功、失败还是
取消，receiver 最终都会收到这些对象。

## 初始化与选择设备

请求后端实例前，先初始化进程级 RHI 上下文。日志 Sink 会被同步调用，因此只要
RHI 仍可能输出消息，它就必须保持有效。

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);
```

实例通过回调返回，以兼容需要异步创建实例的平台：

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

`PhysicalDevice::GetInfo()` 返回适配器名称和类型，以及可选的厂商编号、设备编号和
专用显存容量。可选值缺失表示后端无法提供该信息，并不等价于数值为零。

OpenGL 需要时，可用 `Instance::ShareContextOnThisThread()` 在当前线程绑定共享
上下文；其他后端会把该调用视为空操作。

## 资源与 View

`ResourceFlags` 描述内存位置、用途、格式、纹理形状、采样数和允许创建的 View
类型。这些标志是正确性约束：后端会拒绝互相矛盾的组合，录制命令时的实际用途也
必须在创建资源时声明。

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

View 由来源资源创建，因此后端和设备归属可以在实现内部完成校验，不需要暴露原生
句柄。缓冲区使用 `CreateBufferView()`。例如，经 `WriteBuffer` 写入的缓冲区必须
包含 `CopyDST`；只有 `VertexBuffer` 标志并不足以允许上传。

## Shader、Pipeline 与资源组

管线接收 Slang 程序描述符。FyuuRHI 会编译其中声明的模块，并反射资源接口。图形
管线还需要描述顶点输入、图元拓扑、光栅化、多重采样、深度模板、混合和附件格式。

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

资源组由对应管线创建。它的 space、slot、数组元素和绑定类型必须与反射接口一致。
FyuuRHI 同时支持纹理与采样器分离绑定，以及显式的组合绑定：

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

命令图描述绑定槽、具有依赖顺序的节点、逻辑队列需求、资源访问和具体命令。它从
`CommandScheduler` 开始构建：

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

资源访问声明不是优化提示，而是命令图的正确性契约。后端会据此划分批次、转换
资源状态、插入屏障、同步队列，并处理队列族所有权转移。`QueueType` 只表示节点
所需的能力，不保证一定分配独立物理队列。

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

使用 `SetPresentationTarget()` 绑定每个原生窗口。窗口和事件循环仍由应用所有；
应用需要处理平台事件与尺寸变化，并在窗口关闭前持续提交帧。

## 启动任务与接收完成结果

将构建完成的命令图连接到 receiver，填满所有已注册槽位，然后启动 operation：

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

在发送 error 或 stopped 信号前，`RecoverBindings(CommandGraphResources&&)` 会先
归还 operation 持有的对象；成功时，对象直接随 `set_value` 返回。分别使用
`TakeResource`、`TakeView`、`TakeSampler`、`TakePipeline` 或
`TakeResourceGroup` 取回对应对象，每个槽位只能取一次。

完成回调可能来自内部工作线程。Receiver 必须同步与该线程共享的应用状态。Stop
请求只在原生提交前有效；已经提交到 GPU 的工作通常无法取消。

## 验证与诊断

命令执行分为准备、并行录制和队列提交三个阶段。绑定错误会在提交前报告，录制和
GPU 错误则通过 receiver 的 error 通道返回。

后端诊断会写入配置的 `fyuu_rhi::log::Sink`。自动化测试不能只看进程退出码；即使
进程返回成功，Error 或 Fatal 级别的记录仍应视为失败。

跨后端集成测试位于 `lib/fyuu_rhi/test/hello_triangle`。启用测试后，CTest 会为
当前构建的每个后端注册一次有限帧运行；也可以直接进行交互测试：

```text
HelloTriangle <d3d12|vulkan|opengl|webgpu|metal>
```

该测试覆盖设备创建、上传、管线与资源组创建、命令录制、绘制、呈现、完成处理和
窗口缩放。只有在 Apple 硬件上通过真实窗口与事件循环测试后，Metal 才会进入已
验证后端列表。
