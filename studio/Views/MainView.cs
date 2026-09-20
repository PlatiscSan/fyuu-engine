using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace FyuuStudio.Views;

internal sealed partial class MainView : UserControl
{
	public MainView()
	{
		AvaloniaXamlLoader.Load(this);
	}
}
