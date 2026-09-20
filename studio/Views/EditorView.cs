using Avalonia.Controls;
using Avalonia.Markup.Xaml;

namespace FyuuStudio.Views;

internal sealed partial class EditorView : UserControl
{
	public EditorView()
	{
		AvaloniaXamlLoader.Load(this);
	}
}
