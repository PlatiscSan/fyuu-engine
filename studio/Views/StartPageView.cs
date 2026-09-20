using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace FyuuStudio.Views;

internal sealed partial class StartPageView : UserControl
{
	public StartPageView()
	{
		AvaloniaXamlLoader.Load(this);
	}
}
