using System.IO;
using System.Windows.Input;

namespace FyuuStudio.ViewModels;

internal sealed class EditorViewModel : ObservableObject
{
	private readonly Services.IProjectDialogService _dialogs;
	private string? _scenePath;

	public EditorViewModel(
		string projectPath,
		Services.IProjectDialogService dialogs,
		Action closeProject
	)
	{
		_dialogs = dialogs;
		ProjectPath = projectPath;
		ProjectName = Path.GetFileName(projectPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
		CloseProjectCommand = new RelayCommand(closeProject);
		OpenSceneCommand = new AsyncCommand(OpenSceneAsync);
	}

	public string ProjectName { get; }
	public string ProjectPath { get; }
	public ICommand CloseProjectCommand { get; }
	public ICommand OpenSceneCommand { get; }
	public string? ScenePath
	{
		get => _scenePath;
		private set => SetProperty(ref _scenePath, value);
	}

	private async Task OpenSceneAsync()
	{
		var path = await _dialogs.SelectSceneAsync();
		if (path is not null)
		{
			ScenePath = path;
		}
	}
}
