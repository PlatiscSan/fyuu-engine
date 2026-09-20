using System.Collections.ObjectModel;
using System.Windows.Input;
using FyuuStudio.Services;

namespace FyuuStudio.ViewModels;

internal sealed class StartPageViewModel : ObservableObject
{
	private readonly IProjectDialogService _dialogs;
	private readonly Action<string> _openProject;
	private string _status = "Choose a project to begin.";

	public StartPageViewModel(
		IProjectDialogService dialogs,
		Action showCreateProject,
		Action<string> openProject
	)
	{
		_dialogs = dialogs;
		_openProject = openProject;
		NewProjectCommand = new RelayCommand(showCreateProject);
		OpenProjectCommand = new AsyncCommand(OpenProjectAsync);
	}

	public ObservableCollection<string> RecentProjects { get; } = [];
	public bool HasRecentProjects => RecentProjects.Count != 0;
	public bool HasNoRecentProjects => !HasRecentProjects;
	public ICommand NewProjectCommand { get; }
	public ICommand OpenProjectCommand { get; }

	public string Status
	{
		get => _status;
		private set => SetProperty(ref _status, value);
	}

	private async Task OpenProjectAsync()
	{
		var path = await _dialogs.SelectProjectAsync();
		if (path is null)
		{
			return;
		}
		Remember(path);
		Status = $"Open project: {path}";
		_openProject(path);
	}

	private void Remember(string path)
	{
		RecentProjects.Remove(path);
		RecentProjects.Insert(0, path);
		RaisePropertyChanged(nameof(HasRecentProjects));
		RaisePropertyChanged(nameof(HasNoRecentProjects));
	}
}
