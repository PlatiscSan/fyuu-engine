using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace FyuuStudio.Services;

internal sealed class ProjectDialogService(Window owner) : IProjectDialogService
{
	public async Task<string?> SelectProjectLocationAsync()
	{
		var folders = await owner.StorageProvider.OpenFolderPickerAsync(
			new FolderPickerOpenOptions
			{
				Title = "Choose project location",
				AllowMultiple = false
			}
		);
		return folders.Count == 0 ? null : folders[0].TryGetLocalPath();
	}

	public async Task<string?> SelectProjectAsync()
	{
		var files = await owner.StorageProvider.OpenFilePickerAsync(
			new FilePickerOpenOptions
			{
				Title = "Open Fyuu project",
				AllowMultiple = false
			}
		);
		return files.Count == 0 ? null : files[0].TryGetLocalPath();
	}

	public async Task<string?> SelectSceneAsync()
	{
		var files = await owner.StorageProvider.OpenFilePickerAsync(
			new FilePickerOpenOptions
			{
				Title = "Open scene",
				AllowMultiple = false,
				FileTypeFilter =
				[
					new FilePickerFileType("Fyuu scene") { Patterns = ["*.json"] }
				]
			}
		);
		return files.Count == 0 ? null : files[0].TryGetLocalPath();
	}
}
