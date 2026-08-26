# FyuuUI

FyuuUI 是 FyuuEngine 的独立 retained-mode UI 框架。它使用逻辑树保存控件状态，按可用尺寸和显式主题生成值语义的视觉树，最后由调用方批量消费绘制命令。

```cmake
target_link_libraries(MyTarget PRIVATE FyuuUI)
```

## Theme 与 Style

`Theme` 是普通值对象，不使用单例或全局注册。它包含语义色板、控件状态样式以及公共尺寸；`WidgetStyle` 分别描述 `normal`、`hovered`、`pressed`、`disabled`、`focused`、`selected` 和 `selected_hovered` 状态。

`StyleOverride` 用于局部修改节点样式。所有字段都是可选值，因此只需保存与主题不同的部分。`foreground` 和 `font_size` 会传递给后代，`background` 只影响当前节点。调用 `SetStyle({})` 可以清除节点的全部局部覆盖。

```cpp
import fyuu_ui;

auto theme = fyuu_ui::DarkTheme();
theme.button.selected.background = { 0.42f, 0.50f, 0.90f, 1.0f };

fyuu_ui::LogicalTree tree{ fyuu_ui::Overlay{} };
auto root = tree.GetRoot();
root.SetStyle(fyuu_ui::StyleOverride{
	.foreground = theme.text,
	.font_size = 14.0f
});
auto button = root.AddChild(fyuu_ui::Button{ "Create", true, true });
button.SetStyle(fyuu_ui::StyleOverride{
	.background = { 0.42f, 0.50f, 0.90f, 1.0f }
});
auto visual_tree = tree.BuildVisualTree(
	{ 1280.0f, 720.0f },
	theme,
	[&font_system](std::string_view text, float font_size) {
		return font_system.MeasureText(text, font_size);
	}
);

auto draw_commands = visual_tree.WriteDrawList();
```

调用链为：

1. `LogicalTree` 保存容器、控件状态和层级关系。
2. `BuildVisualTree(Size, Theme, TextMeasurer)` 使用渲染端的真实字体度量，正向传播可继承属性，再完成非递归测量与排列。
3. 视觉生成先解析主题中的控件状态，再应用继承值和节点局部覆盖。
4. `VisualTree::WriteDrawList` 将视觉节点批量转换为绘制命令。
5. 前端渲染器消费 `std::vector<DrawCommand>`；FyuuUI 不依赖具体 RHI 后端。

## 窗体

每棵 `LogicalTree` 最多只能拥有一个 `WindowLayer`；重复插入会立即抛出异常，删除该层后才允许重新创建。`WindowLayer` 是顶层窗体的宿主，直接子节点只能是 `Window`。`Window` 保存标题、位置、尺寸、活动状态和是否允许关闭。窗体视觉明确分为非客户区和客户区：非客户区包含标题栏、标题及关闭区，客户区承载窗体的逻辑子树。窗体在 `WindowLayer` 中的兄弟顺序就是视觉堆叠顺序，调用窗体节点的 `BringToFront()` 可将它移到最前。宿主通过非客户区的命中结果更新 `Window::position` 来拖动窗体，通过 `LogicalTree::Remove` 关闭并清理整个窗体子树。

调用链为：应用向 `WindowLayer` 添加 `Window` → `BuildVisualTree` 根据窗体状态计算边界并生成窗体视觉 → `WriteDrawList` 输出绘制命令 → 前端渲染器消费命令。FyuuStudio 当前通过 `Help` → `About` 创建并显示第一个窗体。
