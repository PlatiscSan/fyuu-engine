# FyuuStudio

FyuuStudio 是建立在 `FyuuEngine`、`FyuuDesktop`、`FyuuUI` 与 `FyuuRHI` 上的桌面工具宿主。Studio 通过公开 C++ 模块使用各库，不直接调用 Runtime 的 C ABI。

## 启动与绘制调用链

```text
main
  -> fyuu_studio::Run
     -> StudioContext
     -> StudioUI
        -> LogicalTree
        -> 创建居中的 Border
     -> Desktop Platform
     -> RHIContext
	    -> Desktop presentation target 适配
        -> Instance
        -> PhysicalDevice
        -> LogicalDevice
        -> CommandScheduler
     -> UIRenderer
     -> Runtime::Run
        -> Desktop 发送窗口像素尺寸事件
        -> StudioApplication::Tick
           -> StudioUI::BuildDrawList
              -> LogicalTree::BuildVisualTree
              -> VisualTree::WriteDrawList
           -> UIRenderer::Submit
              -> DrawRectangleCommand 转换为顶点和索引
              -> RHI transfer、render、present
```

对象按声明的相反顺序析构。Runtime 最先析构，因此它借用的 Platform、Logger 和 Application 在整个运行期间都有效。

## 模块布局

- `module/fyuu_studio.cppm`：唯一公开模块入口。
- `module/internal/application.cpp`：Studio 服务组装、Runtime Application 和后端选择。
- `module/internal/ui.cpp`：拥有逻辑树，处理窗口尺寸并生成 FyuuUI DrawList。
- `module/internal/ui_renderer.cpp`：消费 DrawList，通过 FyuuRHI 绘制和呈现。
- `module/internal/rhi_context.cpp`：适配 Desktop target，并创建 RHI Instance、LogicalDevice 与 CommandScheduler。
- `src/main.cpp`：进程异常边界。

## 当前渲染边界

Studio 不包含第三方即时模式 UI。当前第一个控件是固定尺寸、居中排列的 `fyuu_ui::Border`。UIRenderer 目前只消费 `DrawRectangleCommand`，并保留 Transform 与矩形 Clip 栈；文字、图片、圆角和边框细分将在相应 FyuuUI 绘制能力完成后接入。
