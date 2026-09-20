using FyuuStudio.Services;

namespace FyuuStudio.ViewModels;

internal sealed class MainWindowViewModel : ObservableObject
{
	private readonly IProjectDialogService _dialogs;
	private readonly Action<string, Action> _transition;
	private object _currentPage;

	public MainWindowViewModel(IProjectDialogService dialogs, Action<string, Action> transition)
	{
		_dialogs = dialogs;
		_transition = transition;
		_currentPage = CreateStartPage();
	}

	public object CurrentPage
	{
		get => _currentPage;
		private set => SetProperty(ref _currentPage, value);
	}

	private StartPageViewModel CreateStartPage() => new(_dialogs, ShowCreateProject, ShowEditor);

	private void ShowCreateProject()
	{
		var page = new CreateProjectViewModel(
			_dialogs,
			ShowStartPage,
			ShowEditor
		);
		_transition("Fyuu Studio", () => CurrentPage = page);
	}

	private void ShowEditor(string projectPath)
	{
		var page = new EditorViewModel(projectPath, _dialogs, ShowStartPage);
		_transition($"{page.ProjectName} — Fyuu Studio", () => CurrentPage = page);
	}

	private void ShowStartPage()
	{
		var page = CreateStartPage();
		_transition("Fyuu Studio", () => CurrentPage = page);
	}
}
