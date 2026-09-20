using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Media;
using Avalonia.Threading;
using Avalonia.VisualTree;
using FyuuStudio.Runtime;

namespace FyuuStudio.Views;

internal sealed class RuntimeViewportControl : TemplatedControl
{
	public static readonly StyledProperty<string?> ScenePathProperty =
		AvaloniaProperty.Register<RuntimeViewportControl, string?>(nameof(ScenePath));

	private readonly DispatcherTimer _timer;
	private RuntimeViewportSession? _session;
	private Image? _image;
	private Border? _statusBorder;
	private TextBlock? _status;

	public RuntimeViewportControl()
	{
		_timer = new DispatcherTimer(TimeSpan.FromMilliseconds(16), DispatcherPriority.Render, Tick);
	}

	public string? ScenePath
	{
		get => GetValue(ScenePathProperty);
		set => SetValue(ScenePathProperty, value);
	}

	protected override void OnApplyTemplate(TemplateAppliedEventArgs eventArguments)
	{
		base.OnApplyTemplate(eventArguments);
		_image = eventArguments.NameScope.Find<Image>("PART_Image");
		_statusBorder = eventArguments.NameScope.Find<Border>("PART_StatusBorder");
		_status = eventArguments.NameScope.Find<TextBlock>("PART_Status");
		UpdateImage();
	}

	protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs eventArguments)
	{
		base.OnAttachedToVisualTree(eventArguments);
		if (Design.IsDesignMode)
		{
			SetStatus("Runtime viewport");
			return;
		}
		try
		{
			_session = new RuntimeViewportSession();
			LoadScene();
			ResizeTarget();
			_timer.Start();
		}
		catch (Exception error)
		{
			SetStatus(error.Message);
		}
	}

	protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs eventArguments)
	{
		if (Design.IsDesignMode)
		{
			base.OnDetachedFromVisualTree(eventArguments);
			return;
		}
		_timer.Stop();
		_session?.Dispose();
		_session = null;
		base.OnDetachedFromVisualTree(eventArguments);
	}

	protected override Size ArrangeOverride(Size finalSize)
	{
		var result = base.ArrangeOverride(finalSize);
		ResizeTarget();
		return result;
	}

	protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
	{
		base.OnPropertyChanged(change);
		if (change.Property == ScenePathProperty && _session is not null)
		{
			LoadScene();
		}
	}

	private void Tick(object? sender, EventArgs eventArguments)
	{
		try
		{
			ResizeTarget();
			if (_session?.Tick() == true)
			{
				UpdateImage();
				SetStatus(null);
			}
		}
		catch (Exception error)
		{
			_timer.Stop();
			SetStatus(error.Message);
		}
	}

	private void LoadScene()
	{
		if (_session is null || string.IsNullOrWhiteSpace(ScenePath))
		{
			SetStatus("Open a scene to start rendering");
			return;
		}
		_session.LoadScene(ScenePath);
		SetStatus("Preparing scene resources…");
	}

	private void ResizeTarget()
	{
		if (_session is null || Bounds.Width <= 0 || Bounds.Height <= 0)
		{
			return;
		}
		var scaling = VisualRoot?.RenderScaling ?? 1.0;
		var width = (uint)Math.Clamp(Math.Ceiling(Bounds.Width * scaling), 1, 4096);
		var height = (uint)Math.Clamp(Math.Ceiling(Bounds.Height * scaling), 1, 4096);
		_session.Resize(width, height);
		UpdateImage();
	}

	private void UpdateImage()
	{
		if (_image is not null && _session?.Bitmap is { } bitmap)
		{
			_image.Source = bitmap;
		}
	}

	private void SetStatus(string? message)
	{
		if (_status is null)
		{
			return;
		}
		_status.Text = message;
		if (_statusBorder is not null)
		{
			_statusBorder.IsVisible = !string.IsNullOrWhiteSpace(message);
		}
	}
}
