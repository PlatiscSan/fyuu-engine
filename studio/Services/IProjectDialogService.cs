namespace FyuuStudio.Services;

internal interface IProjectDialogService
{
	Task<string?> SelectProjectLocationAsync();
	Task<string?> SelectProjectAsync();
	Task<string?> SelectSceneAsync();
}
