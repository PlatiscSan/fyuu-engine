# WebGPU sRGB 呈现验证（2026-11-01）

环境：Windows、NVIDIA GeForce RTX 3060 Laptop GPU、Dawn D3D12，驱动
32.0.16.1742；Clang 23、C++23，禁用 RTTI。

## 修正

- 查询 Surface 支持格式和用途；RGBA8/BGRA8 sRGB 源优先选择对应 UNORM 呈现格式。
- 仅有 sRGB 后缀差异时使用 `CopyTextureToTexture`，保留像素编码。
- RGBA/BGRA 不同布局不能直接复制，先把源复制到同布局 UNORM 中间纹理，再用
  `textureLoad` 绘制到目标 UNORM Surface。源只需要呈现原有的 CopySRC 用途，
  不要求 TextureBinding，不执行 sRGB 解码或编码。
- 保持此前的 `vertical_sync` 模式选择及动态重新配置。

复制格式兼容规则见 [WebGPU 规范](https://www.w3.org/TR/webgpu/#copy-compatible)。

## 方法

测试程序增加 `--srgb` 参数，使用 RGBA8 sRGB 附件，清除的线性 RGB 值依次为
`(0.25, 0.5, 0.75)`、`(0.75, 0.25, 0.5)`、`(0.5, 0.75, 0.25)`。
预期的编码后 8 位 RGB 分别是 `(137,188,225)`、`(225,137,188)`、
`(188,225,137)`。允许每通道 1 的量化误差。

每阶段提交 12 帧，从 `vertical_sync=false` 开始每 3 帧切换一次同步设置。
等待完成、泵送窗口事件并调用 DwmFlush 后，在桌面采样窗口内的 5 个点。
三阶段使用同一个 HWND，每阶段析构旧设备和调度器。

本机 Surface 同时支持 RGBA8Unorm 和 BGRA8Unorm。为覆盖仅 BGRA 的情况，
曾临时将查询结果限制为实际支持的 BGRA8Unorm，测试后移除限制并重新构建。
这验证了转换路径，不代表本机 Surface 实际只支持 BGRA，也不是其他平台实测。

## 结果

| 能力列表 | 源格式 | 阶段结果 | 像素结果 |
| --- | --- | --- | --- |
| 临时限制为 BGRA8Unorm | RGBA8 sRGB | 3/3 通过 | 15 个采样点与预期完全一致 |
| 临时限制为 BGRA8Unorm | RGBA8 UNORM | 3/3 通过 | 红、绿、蓝检查全部通过 |
| 真实列表，选择 RGBA8Unorm | RGBA8 sRGB | 3/3 通过 | 15 个采样点与预期完全一致 |
| 真实列表，选择 RGBA8Unorm | RGBA8 UNORM | 3/3 通过 | 红、绿、蓝检查全部通过 |

日志保存在 `cmake-build/rhi-webgpu-srgb/`，其中 `bgra-srgb.log`、
`bgra-unorm.log` 对应临时限制测试，`rgba-srgb.log`、`rgba-unorm.log` 对应最终代码。

正常能力列表的复现命令：

```powershell
cmake --build cmake-build/Clang_Ninja_vcpkg --target FyuuRHIWindowReuse -j 4
$env:PATH = 'C:\vcpkg\installed\x64-windows\bin;' + $env:PATH
& cmake-build/Clang_Ninja_vcpkg/bin/FyuuRHIWindowReuse.exe --srgb webgpu webgpu webgpu
& cmake-build/Clang_Ninja_vcpkg/bin/FyuuRHIWindowReuse.exe webgpu webgpu webgpu
```

没有验证浏览器、Linux、Metal、HDR 或额外颜色管理环境。
