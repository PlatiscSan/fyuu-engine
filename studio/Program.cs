using Avalonia;

namespace FyuuStudio;

internal static class Program
{
	[STAThread]
	public static void Main(string[] arguments)
	{
		BuildAvaloniaApp().StartWithClassicDesktopLifetime(arguments);
	}

	private static AppBuilder BuildAvaloniaApp()
	{
		return AppBuilder
			.Configure<StudioApplication>()
			.UsePlatformDetect();
	}
}
