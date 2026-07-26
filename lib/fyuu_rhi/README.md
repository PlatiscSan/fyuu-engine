# FyuuRHI

FyuuRHI 是 FyuuEngine 的跨 API 渲染硬件接口。本轮重构已经完成预定的架构闭环，当前可以作为上层渲染系统继续开发的稳定基础。这里的“完成”指核心设计与四后端主路径已经贯通，而不是今后不再扩展新能力。

## 设计原则

- `LogicalDevice` 只承担资源与执行对象的工厂职责。
- Shader 不作为公开对象存在，而是 Pipeline 创建过程的一部分。
- `Resource`、`View`、`Sampler` 与 `Pipeline` 是 move-only 值对象。
- 公共契约尽量使用少量 flag 组合，避免为每个后端能力引入大量枚举。
- `Format` 继续通过 `ResourceFlagBits` 表达，这是有意保留的 RHI 设计，而非待替换的临时接口。
- Command Buffer、Fence、Semaphore、SwapChain 与 Surface 不向上层暴露。
- 提交由 CommandGraph 和 Scheduler 表达；Scheduler 按标准 execution scheduler 的语义设计，并为标准库 sender/receiver 支持保留兼容入口。
- API 专有对象、缓存、命令池和同步池均放在后端内部。

## 当前架构

### Pipeline

Pipeline 同时描述 Shader 程序、固定功能状态、资源布局和渲染目标契约。Slang 负责程序编译与反射，但 Slang 类型不会成为通用 Shader 对象暴露给上层。

`PipelineResourceGroup` 表示一组可整体绑定的资源。公共绑定值使用互斥类型表达，避免同一绑定同时保存 buffer、view 和 sampler 等互斥状态。

D3D12 根签名、Pipeline State 和 Vulkan RenderPass 等可复用原生对象均已接入缓存。Vulkan 优先使用 Dynamic Rendering，并在功能不可用时回退到传统 RenderPass。

### CommandGraph

CommandGraph 是公开的工作描述形式，覆盖：

- Barrier
- Pipeline 与 PipelineResourceGroup 绑定
- Draw 与 Dispatch
- Buffer、Texture 及其组合复制
- Present

统一验证层会在进入后端前检查节点、资源访问、渲染作用域、复制区域、纹理布局以及队列能力。编译阶段生成批次计划、资源最后使用点和跨队列依赖；执行阶段再绑定本次提交使用的实际资源。

### Scheduler 与异步完成

Scheduler 由 LogicalDevice 创建，是具体执行资源的轻量句柄。`schedule()` 与 CommandGraph sender 均通过后端执行资源异步完成，不会在调用线程同步调用 receiver。

资源提交协调器负责跨 Graph 的读写依赖、激活、完成和取消竞态。执行失败和设备丢失会传播到相关依赖链。CompletionService 负责轮询无法自然接入回调的后端完成对象。

各后端内部实现包括：

- D3D12 CommandAllocator/CommandList 池与 fence 完成链
- Vulkan CommandPool、timeline semaphore 路径及 binary semaphore/fence 回退池
- OpenGL 共享执行 context 与内部 command recorder
- WebGPU CommandEncoder、Future 和 `WaitAny` 完成链

### Presentation

SwapChain 与 Surface 不属于公开 RHI 契约。Present 是 CommandGraph 命令，具体交换链的创建、复用、重建和帧飞行由 Scheduler 后端管理。

Presentation cache 以完整 native target 为 key，使用 LRU 管理多个呈现目标。帧飞行数量由提交参数控制，默认值为 3。D3D12、Vulkan、OpenGL 和 WebGPU 均已接入对应呈现路径。

### 资源数据通路

资源系统已支持：

- move-only `MappedResource`
- Map/Unmap sender
- Buffer Upload 与 Readback
- Buffer → Texture
- Texture → Buffer
- Texture → Texture
- 基于完成值的延迟销毁
- Upload/Readback staging 资源池

staging 池按用途和容量复用资源，采用按需创建和 best-fit 选择，并限制单项与总保留容量。成功执行的 staging 在 operation state 到达安全销毁边界后归还池；失败、取消和废弃路径进入延迟销毁体系。

纹理传输已处理 row pitch、rows per image、mip、array layer、3D slice 和 compressed block。OpenGL 额外处理 pixel pack/unpack，并为缺少 `glGetTextureSubImage` 的 OpenGL ES 提供 framebuffer readback 回退。

## 后端状态

| 能力 | D3D12 | Vulkan | OpenGL / ES | WebGPU |
| --- | --- | --- | --- | --- |
| Graphics Pipeline | 完成 | 完成 | 完成 | 完成 |
| Compute Pipeline | 完成 | 完成 | 完成 | 完成 |
| CommandGraph 执行 | 完成 | 完成 | 完成 | 完成 |
| Draw / Dispatch / Copy | 完成 | 完成 | 完成 | 完成 |
| Present 与帧飞行 | 完成 | 完成 | 完成 | 完成 |
| Map / Upload / Readback | 完成 | 完成 | 完成 | 完成 |
| 纹理传输矩阵 | 通过 | 通过 | 通过 | 通过 |

Vulkan 使用 Synchronization2 和 Dynamic Rendering 时会先检查启用功能，并保留旧路径回退。现代交换链扩展仅在可用时启用。WebGPU 的 Map 和 Queue 完成都使用 Future、`WaitAnyOnly` 与 CompletionService 驱动，避免依赖实现相关的 spontaneous callback 行为。

## 测试

`test` 目录当前包含：

- 资源身份与 move-only 语义测试
- 资源提交协调器测试
- `Enqueue / Activate / Complete / Cancel` 多线程竞态压力测试
- 资源 Map、Upload、Readback 单元测试
- staging 资源复用测试
- 四后端真实纹理传输集成测试
- tight/non-tight row、mip、array layer 与 3D slice 传输矩阵

本轮最终验证中，D3D12、Vulkan、OpenGL 和 WebGPU 的纹理传输矩阵均通过。Vulkan 驱动在 Visual Studio 中可能产生由驱动或诊断层内部处理的 `0x80000001: Not implemented` first-chance 异常；在没有 Validation Error 且进程以 0 退出时，它不表示 RHI 命令未实现。

## 已知边界

- 当前完成的是核心 RHI 闭环，不包括所有现代 GPU 特性的穷举支持。
- Ray Tracing、Mesh Shader、可变速率着色、稀疏资源和高级视频队列尚未进入公共契约。
- 更细粒度的内存预算、跨设备资源和离线 Pipeline Cache 管理仍可继续扩展。
- 上层 RenderGraph 的资源别名、瞬态资源规划和 pass 合并应建立在当前 CommandGraph 之上，而不是回到公开 Command Buffer。

## 下一阶段建议

RHI 暂时进入维护状态，后续优先从上层渲染系统反向验证接口。建议顺序为：

1. 接入一个最小真实渲染帧，覆盖 graphics、compute、copy 和 present。
2. 在上层实现 RenderGraph，并将其编译到 RHI CommandGraph。
3. 根据真实场景补充瞬态资源与资源别名规划。
4. 建立持续集成中的四后端冒烟测试和至少一个软件/无头回退环境。
5. 只有当真实渲染需求出现时，再扩展高级 GPU 特性。

除非测试或上层接入暴露契约缺陷，不建议再次进行大范围 RHI 重构。
