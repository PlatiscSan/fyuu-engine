# 跨后端复用窗口测试（2026-10-29）

结论：当前 RHI 在本机不能可靠地跨后端复用同一个 HWND。12 个有方向的
`A → B → A` 组合中，3 个通过、9 个失败；连续三轮结果一致。
每阶段重建窗口的对照中，12 个组合全部通过。

## 环境与方法

- Windows 10.0.26300.0；Clang 23、C++23、Release，禁用 RTTI。
- NVIDIA GeForce RTX 3060 Laptop GPU；WebGPU 使用 Dawn 的 D3D12 后端，
  其适配器描述记录驱动版本 32.0.16.1742。
- 测试直接导入 `fyuu_rhi`，不依赖 Runtime 或 Studio。
- 每个用例独立进程；默认模式从创建到退出只持有一个 `CS_OWNDC` 窗口，
  各阶段记录相同 HWND。客户端尺寸 320 × 240。
- 每阶段创建逻辑设备、调度器和纹理，提交 12 帧清除及呈现，三阶段依次使用红、绿、蓝。没有着色器、模型或材质参与。
- 等待命令图完成，额外泵送窗口事件 250 ms，调用 `DwmFlush()`，
  从桌面读取客户端内部 5 个点，逐点比较实际 RGB。
- 阶段结束调用 `ShutdownCompletionPool()` 排空完成通知，
  然后通过作用域析构所有该阶段的 RHI 对象。按该 API 的设计，排空后后续图的
  完成通知同步交付。没有并发向窗口呈现。

## 结果

表中是完整 `A → B → A` 的结果；“通过”表示三轮均通过。

| A | B = D3D12 | B = Vulkan | B = OpenGL | B = WebGPU |
| --- | --- | --- | --- | --- |
| D3D12 | 通过 | 第 2 阶段旧画面 | 第 2 阶段旧画面 | 通过 |
| Vulkan | 第 3 阶段旧画面 | 通过 | 第 2 阶段报错 | 第 3 阶段旧画面 |
| OpenGL | 第 3 阶段旧画面 | 通过 | 通过 | 第 3 阶段旧画面 |
| WebGPU | 通过 | 第 2 阶段旧画面 | 第 2 阶段旧画面 | 通过 |

- 单后端新窗口基线：4 个后端 × 3 轮，12/12 通过。
- 同后端释放并重建：4 个组合 × 3 轮，12/12 通过。
- 跨后端往返：12 个组合 × 3 轮，9/36 通过，27/36 失败。
- 上述 60 个用例全部正常退出，没有超时或崩溃。
- 每阶段重建窗口（`--fresh`）的对照：20/20 通过，其中跨后端往返 12/12，
  同后端往返 4/4，新窗口单后端基线 4/4。没有超时或崩溃。

## 已确认的失败表现

1. **旧画面残留**：例如 D3D12 → Vulkan → D3D12，第二阶段预期绿色，
   5 个点实际仍是第一阶段的红色 `(255, 0, 0)`。
   Vulkan 阶段的命令图没有返回错误。其他旧画面失败同样由实际像素确认，
   不能仅凭 receiver 成功判断跨后端呈现成功。
2. **Vulkan → OpenGL 不兼容**：新窗口初始像素格式为 0，Vulkan 呈现后为 1；
   本机 OpenGL 要求格式 11。修正后在获取目标时明确报告像素格式不兼容，
   要求新的原生窗口，不再忽略 `SetPixelFormat()` 失败后继续调用 `wglMakeCurrent()`。

## 已实施的修正

- 呈现资源由调度器析构时自动释放；切换后端前等待旧图完成，并销毁旧调度器及相关后端资源。
- OpenGL 校验窗口有效性和已有像素格式，检查格式设置失败，并对未入缓存的 HDC
  使用作用域清理，覆盖校验和缓存插入异常。

这是独立 RHI 测试中复现的窗口交接限制，不能归因于引擎侧调用。
销毁旧后端资源后仍然存在跨后端旧画面，不能将重建窗口的对照当作同窗口复用已修复。
旧画面残留在 DXGI、DWM、驱动及 RHI 路径中的具体成因仍需进一步定位；
当前证据不足以指定唯一根因。结果只覆盖上述 Windows / NVIDIA 环境，
没有验证其他 GPU、Linux、Metal、并发呈现或跨后端 GPU 资源共享。

## 重现

在仓库根目录执行：

```powershell
cmake -S . -B cmake-build/Clang_Ninja_vcpkg -DFYUU_RHI_BUILD_WINDOW_REUSE_TEST=ON
cmake --build cmake-build/Clang_Ninja_vcpkg --target FyuuRHIWindowReuse -j 4
& lib/fyuu_rhi/test/window_reuse/run.ps1 -OutputDirectory cmake-build/rhi-window-reuse-release
& lib/fyuu_rhi/test/window_reuse/run.ps1 -OutputDirectory cmake-build/rhi-window-reuse-release -Repeats 1 -FreshWindow
```

`run.ps1` 默认执行三轮。上述当前命令将原始标准输出、诊断、`results.json` 和
`fresh-results.json` 写入 `cmake-build/rhi-window-reuse-release/`；修正前的日志保留在
`cmake-build/rhi-window-reuse/`。独立运行单个用例：

```powershell
& cmake-build/Clang_Ninja_vcpkg/bin/FyuuRHIWindowReuse.exe d3d12 vulkan d3d12
& cmake-build/Clang_Ninja_vcpkg/bin/FyuuRHIWindowReuse.exe --fresh d3d12 vulkan d3d12
```

需要确保 vcpkg 的 DLL 目录在 PATH 中；批量脚本默认使用
`C:\vcpkg\installed\x64-windows\bin`。
