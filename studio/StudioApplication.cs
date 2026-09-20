using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using FyuuStudio.Services;
using FyuuStudio.ViewModels;
using FyuuStudio.Views;

namespace FyuuStudio;

internal sealed partial class StudioApplication : Application
{
	public override void Initialize()
	{
		AvaloniaXamlLoader.Load(this);
	}

	public override void OnFrameworkInitializationCompleted()
	{
		if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
		{
			var window = new StudioWindow
			{
				Title = "Fyuu Studio",
				Width = 960.0,
				Height = 640.0,
				MinWidth = 760.0,
				MinHeight = 480.0
			};
			window.Content = new MainView
			{
				DataContext = new MainWindowViewModel(
					new ProjectDialogService(window),
					(title, changePage) =>
					{
						changePage();
						window.Title = title;
					}
				)
			};
			desktop.MainWindow = window;
		}

		base.OnFrameworkInitializationCompleted();
	}
}
