using System.IO;
using System.Windows.Input;
using FyuuStudio.Services;

namespace FyuuStudio.ViewModels;

internal sealed class CreateProjectViewModel : ObservableObject
{
	private readonly IProjectDialogService _dialogs;
	private readonly Action<string> _createProject;
	private readonly RelayCommand _createCommand;
	private string _name = "Untitled";
	private string _location = "";
	private string _message = "Choose where the project file will be saved.";

	public CreateProjectViewModel(IProjectDialogService dialogs, Action cancel, Action<string> createProject)
	{
		_dialogs = dialogs;
		_createProject = createProject;
		CancelCommand = new RelayCommand(cancel);
		BrowseCommand = new AsyncCommand(BrowseAsync);
		_createCommand = new RelayCommand(Create, () => CanCreate);
		CreateCommand = _createCommand;
	}

	public string Name
	{
		get => _name;
		set
		{
			if (SetProperty(ref _name, value))
			{
				RefreshDerivedState();
			}
		}
	}

	public string Location
	{
		get => _location;
		set
		{
			if (SetProperty(ref _location, value))
			{
				RefreshDerivedState();
			}
		}
	}

	public string ProjectPath => string.IsNullOrWhiteSpace(Location) || string.IsNullOrWhiteSpace(Name)
		? "Project path will appear here"
		: Path.Combine(Location, Name.Trim());

	public string Message
	{
		get => _message;
		private set => SetProperty(ref _message, value);
	}

	public bool CanCreate => !string.IsNullOrWhiteSpace(Name) && !string.IsNullOrWhiteSpace(Location);
	public ICommand BrowseCommand { get; }
	public ICommand CancelCommand { get; }
	public ICommand CreateCommand { get; }

	private async Task BrowseAsync()
	{
		var location = await _dialogs.SelectProjectLocationAsync();
		if (location is not null)
		{
			Location = location;
		}
	}

	private void Create()
	{
		_createProject(ProjectPath);
	}

	private void RefreshDerivedState()
	{
		RaisePropertyChanged(nameof(ProjectPath));
		RaisePropertyChanged(nameof(CanCreate));
		_createCommand.RaiseCanExecuteChanged();
	}
}
