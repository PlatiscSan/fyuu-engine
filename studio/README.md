# FyuuStudio

FyuuStudio 是纯 Avalonia 应用，`studio` 内不保存 C++ 代码。Runtime
RenderTarget 的托管互操作边界位于 Runtime。

从仓库根目录构建：

```powershell
python build.py --preset msvc --config RelWithDebInfo --target FyuuStudio
```

运行并加载场景：

```powershell
./cmake-build/MSVC_Visual_Studio_2026_vcpkg/bin/RelWithDebInfo/FyuuStudio/FyuuStudio.exe path/to/scene.json
```

不传场景路径时视口保持为空。Avalonia 不接触 RHI 对象或后端原生图形句柄。
