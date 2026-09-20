using System.Runtime.InteropServices;
using System.Text;
using Avalonia;
using Avalonia.Media.Imaging;
using Avalonia.Platform;

namespace FyuuStudio.Runtime;

internal sealed class RuntimeViewportSession : IDisposable
{
	private const FyuuRenderTargetFormat ViewportFormat = FyuuRenderTargetFormat.R8G8B8A8Unorm;
	private readonly RendererHandle _renderer;
	private RenderSceneHandle? _scene;
	private RenderTargetHandle? _target;
	private WriteableBitmap? _bitmap;
	private FyuuCamera _camera;
	private bool _awaitingRender;

	public RuntimeViewportSession()
	{
		var name = Encoding.UTF8.GetBytes("Fyuu Studio");
		unsafe
		{
			fixed (byte* pointer = name)
			{
				var descriptor = new FyuuApplicationDescriptor
				{
					Name = new FyuuStringView((nint)pointer, (nuint)name.Length),
					Version = new FyuuVersion { Major = 0, Minor = 1 }
				};
				Check(NativeMethods.Fyuu_ApplicationCreate(in descriptor, out var application), nint.Zero);
				try
				{
					Check(NativeMethods.Fyuu_RendererCreate(application, 0, out var renderer), nint.Zero);
					_renderer = new RendererHandle(renderer);
				}
				finally
				{
					NativeMethods.Fyuu_ApplicationDestroy(application);
				}
			}
		}
		_camera = DefaultCamera();
	}

	public WriteableBitmap? Bitmap => _bitmap;
	public bool HasScene => _scene is not null;

	public void LoadScene(string path)
	{
		WaitUntilReady();
		var bytes = Encoding.UTF8.GetBytes(path);
		unsafe
		{
			fixed (byte* pointer = bytes)
			{
				Check(
					NativeMethods.Fyuu_RendererLoadScene(
						_renderer.DangerousGetHandle(),
						new FyuuStringView((nint)pointer, (nuint)bytes.Length),
						out var scene
					),
					_renderer.DangerousGetHandle()
				);
				_scene?.Dispose();
				_scene = new RenderSceneHandle(scene);
			}
		}
		FitCamera();
		_awaitingRender = false;
	}

	public void Resize(uint width, uint height)
	{
		if (width == 0 || height == 0 || !NativeMethods.Fyuu_RendererIsReady(_renderer.DangerousGetHandle()))
		{
			return;
		}
		if (_bitmap?.PixelSize == new PixelSize((int)width, (int)height))
		{
			return;
		}
		_target?.Dispose();
		var descriptor = new FyuuRenderTargetDescriptor
		{
			Width = width,
			Height = height,
			Format = ViewportFormat
		};
		Check(
			NativeMethods.Fyuu_RendererCreateRenderTarget(
				_renderer.DangerousGetHandle(), in descriptor, out var target
			),
			_renderer.DangerousGetHandle()
		);
		_target = new RenderTargetHandle(target);
		_bitmap?.Dispose();
		_bitmap = new WriteableBitmap(
			new PixelSize((int)width, (int)height),
			new Vector(96, 96),
			PixelFormat.Rgba8888,
			AlphaFormat.Unpremul
		);
		_awaitingRender = false;
	}

	public bool Tick()
	{
		if (_scene is null || _target is null || _bitmap is null ||
		    !NativeMethods.Fyuu_RendererIsReady(_renderer.DangerousGetHandle()))
		{
			return false;
		}
		if (!_awaitingRender)
		{
			Check(
				NativeMethods.Fyuu_RendererRender(
					_renderer.DangerousGetHandle(),
					_scene.DangerousGetHandle(),
					_target.DangerousGetHandle(),
					in _camera
				),
				_renderer.DangerousGetHandle()
			);
			_awaitingRender = true;
			return false;
		}
		using var framebuffer = _bitmap.Lock();
		var image = new FyuuMutableImageView
		{
			Pixels = framebuffer.Address,
			Size = (nuint)(framebuffer.RowBytes * framebuffer.Size.Height),
			RowPitch = (nuint)framebuffer.RowBytes,
			Width = (uint)framebuffer.Size.Width,
			Height = (uint)framebuffer.Size.Height,
			Format = ViewportFormat
		};
		Check(
			NativeMethods.Fyuu_RenderTargetCopyPixels(
				_renderer.DangerousGetHandle(), _target.DangerousGetHandle(), image
			),
			_renderer.DangerousGetHandle()
		);
		_awaitingRender = false;
		return true;
	}

	public void Dispose()
	{
		WaitUntilReady();
		_bitmap?.Dispose();
		_target?.Dispose();
		_scene?.Dispose();
		_renderer.Dispose();
	}

	private unsafe void FitCamera()
	{
		if (_scene is null || NativeMethods.Fyuu_RenderSceneGetBounds(
			_scene.DangerousGetHandle(), out var bounds) != FyuuResult.Success || bounds.Empty != 0)
		{
			_camera = DefaultCamera();
			return;
		}
		var centerX = (bounds.Minimum[0] + bounds.Maximum[0]) * 0.5f;
		var centerY = (bounds.Minimum[1] + bounds.Maximum[1]) * 0.5f;
		var centerZ = (bounds.Minimum[2] + bounds.Maximum[2]) * 0.5f;
		var extent = MathF.Max(
			MathF.Max(bounds.Maximum[0] - bounds.Minimum[0], bounds.Maximum[1] - bounds.Minimum[1]),
			bounds.Maximum[2] - bounds.Minimum[2]
		);
		_camera = DefaultCamera();
		_camera.Position[0] = centerX;
		_camera.Position[1] = centerY;
		_camera.Position[2] = centerZ + MathF.Max(extent * 1.5f, 1.0f);
	}

	private static unsafe FyuuCamera DefaultCamera()
	{
		var camera = new FyuuCamera
		{
			Projection = 0,
			VerticalFieldOfView = MathF.PI / 3.0f,
			OrthographicVerticalSize = 10.0f,
			NearPlane = 0.01f,
			FarPlane = 10000.0f
		};
		camera.Position[2] = 3.0f;
		camera.Rotation[3] = 1.0f;
		return camera;
	}

	private void WaitUntilReady()
	{
		while (!NativeMethods.Fyuu_RendererIsReady(_renderer.DangerousGetHandle()))
		{
			Thread.Yield();
		}
	}

	private static void Check(FyuuResult result, nint renderer)
	{
		if (result == FyuuResult.Success)
		{
			return;
		}
		var message = renderer == nint.Zero ? null : GetError(renderer);
		throw new InvalidOperationException(message ?? $"Fyuu Runtime failed with result {result}.");
	}

	private static string? GetError(nint renderer)
	{
		var value = NativeMethods.Fyuu_RendererGetLastError(renderer);
		return value.Str == nint.Zero || value.Length == 0
			? null
			: Marshal.PtrToStringUTF8(value.Str, checked((int)value.Length));
	}
}
