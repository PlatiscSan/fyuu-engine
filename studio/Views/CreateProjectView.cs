using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Markup.Xaml;
using Avalonia.VisualTree;

namespace FyuuStudio.Views;

internal sealed partial class CreateProjectView : UserControl
{
	public CreateProjectView()
	{
		AvaloniaXamlLoader.Load(this);
	}

	private void OnBackgroundPointerPressed(object? sender, PointerPressedEventArgs eventArguments)
	{
		for (var visual = eventArguments.Source as Visual; visual is not null; visual = visual.GetVisualParent())
		{
			if (visual is TextBox or Button)
			{
				return;
			}
		}
		Focus();
	}
}
