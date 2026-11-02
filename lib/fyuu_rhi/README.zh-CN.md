# FyuuRHI

[API 文档](https://platiscsan.github.io/fyuu-engine/zh-CN/html/) · [English](README.md)

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

## API 文档

查看已发布的[简体中文 API 文档](https://platiscsan.github.io/fyuu-engine/zh-CN/html/)，
也可以切换到[英文 API 文档](https://platiscsan.github.io/fyuu-engine/en/html/)。

线上页面由 GitHub Actions 生成，不存放在源码分支中。如需在本地生成，请先安装
Doxygen，以 `-DFYUU_RHI_BUILD_DOCUMENTATION=ON` 配置工程，然后构建
`FyuuRHIDocumentation` 和 `FyuuRHIDocumentationZhCN`。

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

物理设备、逻辑设备、资源、View、采样器、管线、资源组和完成令牌使用 fast pimpl 内嵌缓冲区，
不再单独分配不透明实现对象。缓冲区大小与对齐在编译时检查，移动后源对象为空。

大多数 GPU 对象采用独占所有权，只能移动；`CommandScheduler` 可以复制，副本共享同一个调度上下文。命令图启动后，operation 会接管所有绑定对象；执行成功、失败或取消时，这些对象都会交还给 receiver。

## 初始化与选择设备

请求后端实例前，必须先初始化进程级 RHI 上下文。RHI 会同步地向日志 Sink 写入消息。
`InitializeRHIContext()` 允许重复调用：每次更新名称、版本、缓存路径及平台配置，并同步更换
Sink，传 `nullptr` 则停用日志。初始化不得与其他 RHI 操作并发；此前借用的名称字符串视图
可能失效。重新初始化前必须释放旧 Vulkan 派生对象；Vulkan Instance 会被清空，并在下次请求时
按新配置创建，其他后端实例保留。该函数不增加 Runtime 生命周期引用计数。
每次初始化还会等待活动编译完成并释放 Slang global session。每次缓存未命中的编译使用
局部 session，避免已加载模块名保留旧源码。磁盘缓存命中仍不创建 session，已有管线产物不受影响。
使用 `fyuu_rhi::RebindRHISink(new_sink)` 更换日志目标；无需新目标时传 `nullptr`，
并在返回后销毁旧 Sink。重绑会阻止新调用，等待已进入的旧 `Write()` 全部返回，
再发布新的借用指针。新目标为空时丢弃日志，GPU 工作不受影响。
初始化尚未完成时调用会抛出 `std::logic_error`。不得从 `Write()` 内部调用，
也不得持有 `Write()` 需要的锁；并发重绑的调用方必须协调各 Sink 的生命周期。

```cpp
fyuu_rhi::InitializeRHIContext(
    "MyApplication",
    { 0u, 1u, 0u, 0u },
    "MyEngine",
    { 0u, 1u, 0u, 0u },
    &log_sink
);
```

`RequestInstance` 直接返回所选后端在进程生命周期内有效的实例：

```cpp
auto& instance = fyuu_rhi::RequestInstance(fyuu_rhi::Backend::Vulkan);
auto physical_devices = instance.EnumeratePhysicalDevices();
auto const& physical_device = fyuu_rhi::BestPerformance(physical_devices);
auto device = physical_device.CreateLogicalDevice();
auto scheduler = device.CreateScheduler();

// 在这里创建资源并提交命令图。
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

附件纹理可向 `CreateTexture` 的最后一个参数传入 `TextureClearValue`：
颜色使用 `ColorClearValue{r, g, b, a}`，深度/模板使用
`DepthStencilClearValue{depth, stencil}`。这是默认清除值，不会在创建时初始化纹理。
附件设置 `LoadOperation::ClearDefault` 时使用纹理默认值；`LoadOperation::Clear`
仍使用附件中显式指定的值。深度和模板可以分别选择。

省略该参数时，颜色附件默认透明黑，深度/模板附件默认深度 1、模板 0。
非附件纹理不允许指定清除值。所有后端都会在渲染时使用这些默认值；D3D12
还会将其作为资源创建时的优化提示。显式覆盖始终有效，但不同的值仍可能触发
D3D12 的快速清除性能警告。

View 由资源自身创建，使实现能够在不暴露原生句柄的情况下检查后端和设备归属。缓冲区 View 使用 `CreateBufferView()` 创建。例如，经 `WriteBuffer` 写入的缓冲区必须包含 `CopyDST`；只有 `VertexBuffer` 标志并不足以允许上传。

### 可移植上传对齐

所有后端统一使用以下公开对齐契约，不需要上层按后端硬编码：

| 操作 | 公开常量 | 字节数 |
| --- | --- | --- |
| 纹理拷贝行距 | `TextureDataLayout::RowPitchAlignment` | 256 |
| 缓冲区内纹理拷贝起始偏移 | `TextureDataLayout::OffsetAlignment` | 512 |
| 映射起始偏移 | `Resource::MapOffsetAlignment` | 8 |
| 映射有效长度 | `Resource::MapSizeAlignment` | 4 |

`bytes_per_row` 必须非零且满足行距对齐；`rows_per_image` 必须非零。行距和分层间隔仍须满足格式区块及拷贝区域要求。纹理拷贝布局在命令图连接时校验，违规抛出 `std::invalid_argument`。

`Map` 的 `size == 0` 表示映射偏移之后的剩余字节；解析后的长度必须非零且满足长度对齐。映射范围在公共入口校验，违规不会提交原生映射请求。对齐值是统一的可移植要求，不是各后端的最小限制。

### 映射缓冲区

`Resource::Map()` 映射一段字节区间，并返回仅移动的
`ResourceMapScope`。带有 `HostVisible` 标志的缓冲区可以写入，调用
`ResourceMapScope::Write()` 复制数据即可，不需要持有后端映射指针。带有
`DeviceReadback` 标志的缓冲区通过 `ResourceMapScope::Read()` 读取。

WebGPU 的 `HostVisible` 缓冲区只允许附加 `CopySRC` 用途，`DeviceReadback` 缓冲区只允许附加 `CopyDST` 用途；两者均不能附加顶点、索引、Uniform、Storage、Texel 或 Indirect 用途。不支持的组合在创建时抛出 `std::invalid_argument`，不会静默取消可映射性。GPU 顶点等数据使用独立上传缓冲区复制。

```cpp
fyuu_rhi::ResourceFlags upload_flags;
upload_flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
upload_flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
auto upload = device.CreateBuffer(data.size(), upload_flags);

{
    auto mapping = upload.Map({ 0u, data.size() });
    mapping.Write(std::as_bytes(std::span(data)));
} // 在这里自动解除映射。
```

映射期间，GPU 不能同时读写该区间。映射回读缓冲区前，应先等待对应命令图完成。
纹理不能直接映射；需要先将目标区域复制到带有 `DeviceReadback` 标志的缓冲区，
再读取该缓冲区。

```cpp
auto mapping = readback.Map({ 0u, byte_count });
auto bytes = mapping.Read();
Consume(bytes);
mapping.Reset(); // 可以提前解除映射；不调用时由析构函数自动完成。
```

`Write()` 会拒绝只读映射以及大于映射区间的数据。`Reset()` 可以重复调用；
已经移动或已经重置的 Scope 不再持有映射。映射有效期间，对应的 `Resource`
必须保持存活，且不得移动，直到映射解除。

## Shader、Pipeline 与资源组

紧凑排列的 `float3` 顶点输入使用 `ResourceFlagBits::R32G32B32Float`，
每个属性占 12 字节。通过顶点布局设置实际的 buffer stride 和属性 offset。
此格式仅用于顶点输入，不表示纹理或类型化 buffer View 也支持该格式。

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

窗口关闭或切换后端前，先等待该逻辑设备上的所有命令图完成，再销毁旧调度器及相关后端资源。

原生 WebGPU 后端在 `vertical_sync=true` 时使用 FIFO；为 `false` 时按 Surface 能力优先选择 Immediate，其次 Mailbox，均不支持时使用 FIFO。同步设置变化会重新配置已有 Surface。浏览器 Canvas 的呈现时机由浏览器控制，不能通过此参数关闭垂直同步。

WebGPU 按 Surface 支持格式选择呈现格式。RGBA8/BGRA8 sRGB 源优先复制到对应的 UNORM Surface，保留已编码像素，不再执行 sRGB 转换。只有另一种通道布局可用时，RHI 通过内部 UNORM 中间纹理和呈现绘制完成 RGBA/BGRA 转换，不要求源纹理增加采样用途。

释放呈现对象不会重置原生窗口状态。Windows 的窗口像素格式一旦设置就不能更改，
不兼容的 OpenGL 目标会明确报错；跨后端复用同一个 HWND 不属于保证。
需要任意切换后端时，由宿主为新后端创建新的原生呈现窗口。

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

回读与绘制统一通过 `start()` 异步提交。完成回调可以继续提交命令图，但不得阻塞等待完成线程派发另一个 receiver；GPU 数据依赖通过命令图表达，回读缓冲区在图完成后读取。

完成回调可能在内部工作线程上执行。Receiver 必须自行同步与该线程共享的应用状态。Stop 请求只在原生提交前有效；工作一旦提交到 GPU，通常就无法取消。

## 验证与诊断

命令执行分为准备、并行录制和队列提交三个阶段。绑定错误会在提交前报告，录制错误和 GPU 错误则通过 receiver 的 error 通道返回。

后端诊断会写入配置的 `fyuu_rhi::log::Sink`。自动化测试不能只检查进程退出码；即使进程正常退出，Error 或 Fatal 级别的日志仍应视为失败。

跨后端回归测试位于 `lib/fyuu_rhi/test`。启用 BUILD_TESTING 后由 CTest 注册。运行 `DrawProbe <backend> hello` 可检查三角形像素；DrawShapes、BindingProbe、GraphValidation 和 CompletionPool 分别覆盖渲染状态、绑定、图契约与并发。窗口呈现与缩放由 原生窗口宿主集成检查验证。Metal 仍需在 Apple 硬件上验证。
## 着色器缓存清理

着色器缓存位于系统缓存目录下的
`<引擎>/v<版本>/<应用>/v<版本>/shaders/<后端>/shader-<模块名>/<变体哈希>/`。
后端目录为 `d3d12`、`vulkan`、`opengl`、`webgpu` 或 `metal`。

每个变体包含 `manifest.json`、`interface.json`、`reflection.json`，以及
`entry-0-shader-vertex_main.bin` 这类带入口名的编译产物。
`manifest.json` 记录完整模块名、入口名与编译标记，便于辨认用途。
多模块程序按第一个模块分组；目录名经过长度限制和字符替换，变体仍由哈希区分。

Windows 的系统缓存目录是 `%LOCALAPPDATA%`；Linux 使用 `$XDG_CACHE_HOME`
或 `~/.cache`；macOS 使用 `~/Library/Caches`。请先关闭使用 RHI 的应用，再清理。
可以删除单个变体、整个着色器目录或整个后端目录；下次创建对应管线时会重新编译。

自动清理以完整变体为单位，不会单独删除 manifest 或入口文件。

原生管线缓存放在相邻的 `pipelines/` 中：

```text
pipelines/
  d3d12/
    graphics/shader-模块名/<哈希>.bin
    compute/shader-模块名/<哈希>.bin
    root-signatures/<哈希>.bin
  opengl/
    graphics/shader-模块名/<哈希>.bin
    compute/shader-模块名/<哈希>.bin
  vulkan/device-shared/<哈希>.bin
```

D3D12 root signature 按布局共享，Vulkan pipeline cache 按设备和驱动共享，
因此不伪装成某个着色器的独立缓存。删除 `device-shared/` 会清理 Vulkan 原生共享缓存。
WebGPU、Metal 目前没有 RHI 管理的原生管线磁盘缓存，驱动或运行库自行管理的缓存不在此处。
关闭使用 RHI 的应用后，可删除单个文件或整个目录，缺失的缓存会重新生成。

## WebGPU 的进度推进与 CPU 占用

Batch 仍然并行录制。有在途队列工作时，调度器通过零超时轮询推进 Dawn，
每轮让出执行权，不再依赖毫秒定时等待。这能降低完成延迟，但等待 GPU 时会增加
CPU 占用。没有在途工作时，进度线程休眠，直到收到新提交或停止通知。
Receiver 调用和资源回收仍由完成执行器处理，不在 Dawn 回调或进度线程中执行。

## WebGPU 的 BC 纹理 fallback

对于非区块对齐的尺寸，WebGPU 同时保存原始 BC 区块和用于采样的解码纹理；
设备支持时，对齐尺寸仍使用原生 BC 采样，否则也走软件解码。两种路径都保留原尺寸和 mip 链，
包括 2287 × 2279 这类图片。缓冲区复制和读回使用原始区块，不做重新压缩；
纹理间复制同步更新两份表示。解码作为 GPU 命令在采样前执行，不改成同步 CPU 上传。
临时的原生 BC 纹理可以补齐尺寸，但应用着色器看到的纹理仍是原尺寸。

缺少 `TextureCompressionBC` 时，NVIDIA、Intel 和 AMD 路径自动启用 BC1–BC7 的
GPU 软件解码，不需要额外开关。BC6H/BC7 另有不依赖厂商的规范精确解码器。
AMD 的 BC1–BC5 尚待原生硬件验证；未知厂商的 BC1–BC5
会明确拒绝，不以近似值代替。

sRGB 保留 sRGB 语义。NVIDIA/Intel 的 BC4/BC5 使用有符号或无符号 normalized16
采样存储，BC6H 使用 RGBA16Float，BC1–BC3 和 BC7 使用 RGBA8。
双重存储和临时解码纹理会增加内存占用。
所有 WebGPU BC 纹理都保存原始区块，使复制和读回行为不依赖尺寸或 compatibility mode。
这并不意味着 BC 格式可以作为渲染目标或可写存储纹理使用。

## 内存保留策略

D3D12 的闲置命令列表、Vulkan 的命令池和二元完成对象按近期命令图需求与
可用主机内存决定保留数量，不永久保留历史峰值。内存状态最多每 100 ms 查询一次：
可用比例低于 25% 时按比例缩减闲置预算，低于 5% 时不保留闲置对象。
缩减发生在后续调度活动或对象退役时，不会释放在途对象。
此策略衡量主机内存，并不代表 GPU 显存预算。

Vulkan 资源组共享描述符池页。新页容量随需求增长，并在内存压力下缩小；
仍有存活资源组的既有页不能调整容量。最后一个资源组销毁后，空页随之释放。
D3D12 描述符堆在首次使用时才建立。

OpenGL 的提交快照仍独立持有数据，但管线元数据集中存储，并缩减闲置 VAO 与
呈现目标缓存。Slang 仅保留惰性创建的 global session，编译 session 随本次编译结束而释放。
WebGPU 在同一设备、同一解码格式的存活纹理之间共享 BC 解码管线；
fallback 常量使用录制期对齐分页，每次更新占用独立切片。
这些调整不改变资源所有权与绑定语义。

## 许可证

FyuuRHI 的原创源代码、构建脚本、测试和文档采用
[Apache License 2.0](LICENSE.txt)。这些文件以本组件的许可证为准，
不适用仓库根目录的默认许可证；其他 FyuuEngine 组件的许可证不变。

第三方代码，包括 `external/` 中的依赖和生成的加载器代码，保留各自的许可证
和版权声明。分发包含这些依赖的 FyuuRHI 时，也须遵守相应的第三方许可证。
