using Microsoft.Win32.SafeHandles;

namespace FyuuStudio.Runtime;

internal sealed class RendererHandle : SafeHandleZeroOrMinusOneIsInvalid
{
	private RendererHandle() : base(true) { }
	internal RendererHandle(nint handle) : base(true) => SetHandle(handle);
	protected override bool ReleaseHandle()
	{
		NativeMethods.Fyuu_RendererDestroy(handle);
		return true;
	}
}

internal sealed class RenderSceneHandle : SafeHandleZeroOrMinusOneIsInvalid
{
	private RenderSceneHandle() : base(true) { }
	internal RenderSceneHandle(nint handle) : base(true) => SetHandle(handle);
	protected override bool ReleaseHandle()
	{
		NativeMethods.Fyuu_RenderSceneDestroy(handle);
		return true;
	}
}

internal sealed class RenderTargetHandle : SafeHandleZeroOrMinusOneIsInvalid
{
	private RenderTargetHandle() : base(true) { }
	internal RenderTargetHandle(nint handle) : base(true) => SetHandle(handle);
	protected override bool ReleaseHandle()
	{
		NativeMethods.Fyuu_RenderTargetDestroy(handle);
		return true;
	}
}
