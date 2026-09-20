using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace FyuuStudio.Runtime;

internal enum FyuuResult : uint
{
	Success = 0,
	InvalidArgument = 1,
	InvalidState = 2,
	RendererError = 7
}

[StructLayout(LayoutKind.Sequential)]
internal readonly struct FyuuStringView(nint str, nuint length)
{
	public readonly nint Str = str;
	public readonly nuint Length = length;
}

[StructLayout(LayoutKind.Sequential)]
internal struct FyuuVersion
{
	public byte Variant;
	public byte Major;
	public byte Minor;
	public byte Patch;
}

[StructLayout(LayoutKind.Sequential)]
internal struct FyuuApplicationDescriptor
{
	public nint UserData;
	public FyuuStringView Name;
	public FyuuVersion Version;
	public nint Tick;
	public nint CloseRequested;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct FyuuCamera
{
	public fixed float Position[3];
	public fixed float Rotation[4];
	public int Projection;
	public float VerticalFieldOfView;
	public float OrthographicVerticalSize;
	public float NearPlane;
	public float FarPlane;
}

[StructLayout(LayoutKind.Sequential)]
internal unsafe struct FyuuRenderBounds
{
	public fixed float Minimum[3];
	public fixed float Maximum[3];
	public byte Empty;
}

internal enum FyuuRenderTargetFormat : uint
{
	R8G8B8A8Unorm = 0,
	R8G8B8A8Srgb = 1,
	B8G8R8A8Srgb = 2,
	R16G16B16A16Float = 3
}

[StructLayout(LayoutKind.Sequential)]
internal struct FyuuRenderTargetDescriptor
{
	public uint Width;
	public uint Height;
	public FyuuRenderTargetFormat Format;
}

[StructLayout(LayoutKind.Sequential)]
internal struct FyuuMutableImageView
{
	public nint Pixels;
	public nuint Size;
	public nuint RowPitch;
	public uint Width;
	public uint Height;
	public FyuuRenderTargetFormat Format;
}

internal static partial class NativeMethods
{
	private const string Library = "FyuuEngine";

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_ApplicationCreate(
		in FyuuApplicationDescriptor descriptor,
		out nint output
	);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial void Fyuu_ApplicationDestroy(nint application);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_RendererCreate(nint application, int backend, out nint output);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	[return: MarshalAs(UnmanagedType.I1)]
	internal static partial bool Fyuu_RendererIsReady(nint renderer);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuStringView Fyuu_RendererGetLastError(nint renderer);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial void Fyuu_RendererDestroy(nint renderer);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_RendererLoadScene(
		nint renderer,
		FyuuStringView path,
		out nint output
	);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_RenderSceneGetBounds(nint scene, out FyuuRenderBounds output);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial void Fyuu_RenderSceneDestroy(nint scene);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_RendererCreateRenderTarget(
		nint renderer,
		in FyuuRenderTargetDescriptor descriptor,
		out nint output
	);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial void Fyuu_RenderTargetDestroy(nint target);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_RendererRender(
		nint renderer,
		nint scene,
		nint target,
		in FyuuCamera camera
	);

	[LibraryImport(Library)]
	[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]
	internal static partial FyuuResult Fyuu_RenderTargetCopyPixels(
		nint renderer,
		nint target,
		FyuuMutableImageView destination
	);
}
