# FyuuAsset

[English](README.md)

FyuuAsset 是 FyuuEngine 的动态资产导入与持久化库。它使用进程级 catalog，通过
UUID 或“运行时类型名 + 资产名称”定位资产，以递归动态值保存不同资产的数据，并在
共享工作线程上执行存储操作。

资产类型不是固定枚举。应用可以使用 `model`、`scene`、`miku_model` 等任意合法
类型名，其业务含义由应用和 Runtime 决定。

## 要求与集成

FyuuAsset 使用 C++23 和 C++ Modules：

```cmake
target_link_libraries(MyApplication PRIVATE FyuuAsset)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

```cpp
import fyuu_asset;
```

## API 文档

安装 Doxygen，使用 `-DFYUU_ASSET_BUILD_DOCUMENTATION=ON` 配置项目，然后构建
`FyuuAssetDocumentation` 生成英文文档，或构建 `FyuuAssetDocumentationZhCN` 生成
简体中文文档。HTML 分别输出到 FyuuAsset 文档构建目录下的 `en/html` 和
`zh-CN/html`。

执行任何资产操作前，必须初始化进程级上下文：

```cpp
fyuu_asset::InitializeAssetContext("assets", &log_sink);
```

只有第一次调用会确定规范化后的绝对资产根目录和借用的日志接收器。日志接收器可能
从工作线程同步调用，必须在整个资产系统使用期间保持存活。不需要诊断时可以省略。

## 动态值

`Value` 可以拥有 null、`bool`、有符号或无符号 64 位整数、`double`、
`std::string`、`Binary`、`File`、`Array`、`Object` 或 `UUID`。使用 `Is<T>()`
检查活动类型，使用 `As<T>()` 访问值：

```cpp
auto fields = fyuu_asset::Object{
    {"enabled", fyuu_asset::Value{true}},
    {"count", fyuu_asset::Value{std::uint64_t{4u}}}
};

auto const enabled = fields.at("enabled").As<bool>();
```

`Binary` 直接编码进 JSON。`File` 带有逻辑文件名，会保存到 `mesh`、`texture`、
`anime`、`skeleton`、`audio` 或 `shader` 等分类目录。UUID 值是不持有目标生命
周期的资产引用。

## 导入资产

`Importer` 是可等待的解析器，结果是中立的 `Object`；`Factory` 持久化由
应用层确定的最终字段。两个操作都在共享资产工作线程上执行：

```cpp
fyuu_asset::Importer::Register("miku_model", ImportMikuModel);
auto fields = co_await fyuu_asset::Importer{"miku_model", "sources/character.zip"};
/** 应用层在这里把解析结果转换为自己定义的字段。 */
auto asset = co_await fyuu_asset::Factory{"miku_model", "miku", fields};
auto uuid = asset.GetUUID();
```

`ImportMikuModel` 是返回 `Object` 的普通函数指针，在资产工作线程执行并允许抛出异常。
注册表以开放的运行时类型字符串为键。应用需要现有格式探测解析器时，可以显式调用
`RegisterBuiltIn(type)`；未注册类型会直接失败，不会被静默套用固定字段架构。

等待 Importer 会挂起调用方，直到资产工作线程完成解析；`await_resume()` 返回解析
后的 Object，或重新抛出解析异常。Importer 本身就是操作状态，在 await 表达式结束前
一直保存在调用方的协程帧中。

导入源可以是普通文件、目录或 ZIP 包。目录与 ZIP 可以通过 `asset.json` 声明一个
或多个入口：

```json
{"entry":"model/character.fbx"}
```

```json
{"entries":["body.fbx","accessory.obj"]}
```

没有清单时，系统会发现受支持的模型入口或唯一且无歧义的 JSON 入口。多个模型入口
会导入为同一资产的 `models` 数组。虚拟路径始终使用 UTF-8，包括 ZIP 包内部路径。

JSON 会转换为动态字段；受支持的模型格式由 Assimp 解析，PMD 提供内部回退解析器。
模型纹理会被解码、生成 mip 链并转换为 GPU 可消费的 BC 数据。没有专用解析器的格式
会无损保存在解析结果的 `binary` 字段。FyuuAsset 不会把解析字段自动注入 Asset；
应用层必须显式选择传给 `Factory` 的完整 Object。

## 内置导入器的预设字段

动态值系统本身是开放的。下列字段只是内置解析器的中立输出协议，不是 Asset 架构。

| 来源 | 顶层字段 |
| --- | --- |
| JSON 对象 | 完全保留 JSON 对象自身字段，不额外增加字段 |
| 未识别格式、独立二进制或图片 | `binary: Binary` |
| 单个模型 | `geometry`、`meshes`、`materials`、`textures`、`animation_count` |
| 多个模型入口 | `models: Array<Object>`；每个元素另有值为 `model_N` 的 `name` |

模型顶层字段如下：

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `geometry` | `File` | 所有 Mesh 共享的 `geometry.mesh` 字节缓冲 |
| `meshes` | `Array<Object>` | 通过流范围引用 `geometry` 的 Mesh 描述 |
| `materials` | `Array<Object>` | 由 Mesh 材质下标引用的材质值 |
| `textures` | `Array<Object>` | 已解码、生成 mip 并完成 BC 压缩的纹理 |
| `animation_count` | `std::uint64_t` | 源模型报告的动画数量 |

Assimp Mesh 固定包含 `name`、`material`、`vertex_count`、`index_count`、
`positions` 和 `indices`。只有源文件提供相应数据时，才存在 `normals`、
`texcoords`、`tangents` 和 `bitangents`。`material` 是同一模型对象中
`materials` 数组的下标。每个数据流都是包含字节 `offset` 和字节 `size` 的对象，
范围指向 `geometry`：位置、法线、切线和副切线使用紧密排列的 `float3`，UV 使用
`float2`，索引使用 `std::uint32_t`。

内部 PMD 回退路径还会产生 `bone_indices` 和 `bone_weights`：每个顶点分别保存两个
紧密排列的 `std::uint32_t` 骨骼下标和一个 `float2` 权重。PMD 回退 Mesh 当前没有
`name` 字段。

Assimp Material 包含 `name` 和 `textures`。每个纹理绑定包含 `semantic` 和
`texture`：前者是 Assimp 纹理语义的数值，后者是同一模型对象中 `textures` 数组的
下标。PMD 回退 Material 还包含 `diffuse`、`alpha`、`specularity`、`specular`
和 `ambient`。

每个 Texture 对象包含 `name`、基础 `width`、基础 `height`、`format`、
`mip_levels` 和 `pixels`。每个 mip 对象包含 `width`、`height`、字节 `offset`
和字节 `size`，范围指向 `pixels` 文件。当前格式字符串为 `bc1_rgba_unorm`、
`bc1_rgba_unorm_srgb`、`bc3_rgba_unorm`、`bc3_rgba_unorm_srgb` 和
`bc5_rg_unorm`。

`animation_count` 目前只是元数据：Assimp 路径尚未保存动画通道、节点和骨骼资产。
同样，独立 PNG 或 JPEG 当前会进入 `binary`；只有模型引用的纹理才执行 BC 转换。
Runtime 不应假定存在上述协议未列出的字段。

## 加载资产

资产可以通过 UUID，或者通过运行时类型名和名称加载：

```cpp
auto by_uuid = co_await fyuu_asset::Factory{uuid};
auto by_name = co_await fyuu_asset::Factory{"miku_model", "miku"};
auto& meshes = by_name.GetField("meshes").As<fyuu_asset::Array>();
```

`GetField()` 保留 Asset 的 const 属性和值类别。字段不存在时抛出
`std::out_of_range`；`As<T>()` 类型不匹配时抛出 `std::bad_variant_access`。

## 删除资产

删除也是异步操作，并返回实际删除的 UUID：

```cpp
auto deleted_uuid = co_await fyuu_asset::Deleter{uuid};
```

也可以使用类型名和名称：

```cpp
auto deleted_uuid = co_await fyuu_asset::Deleter{"miku_model", "miku"};
```

删除会先提交 catalog 变更，再清理独立资产目录。已经加载的旧 Asset 仍是有效快照，
但它们析构时不能重新创建已删除条目。如果 catalog 已提交而目录清理因文件占用或
权限失败，任务会报告文件系统异常，资产身份仍保持删除。

## 生命周期与持久化

`Asset` 是可复制、可移动的拥有型值。非 const `GetField()` 可以修改字段。析构会把
最终快照提交到共享工作线程，移动操作会转移持久化责任。

副本是互相独立的快照。多个可修改副本先后析构时，最后执行的保存成为磁盘状态；副本
不是并发编辑事务。析构保存是异步且 `noexcept` 的。Factory 创建成功时已经保存字段
和 catalog 身份；`Importer` 只解析源数据，不修改 catalog。

## 磁盘布局

每个资产名称在整个工作区中拥有一个独立目录。运行时类型只作为 catalog 元数据，
不会形成额外目录层级：

```text
asset_root/
  catalog.json
  miku/
    asset.json
    mesh/
      asset_0_geometry.mesh
    texture/
      asset_1_albedo.texture
    anime/
    skeleton/
```

catalog 只包含身份信息：

```json
{
  "assets": [
    {
      "uuid": "522d3fbb-c34c-4100-9425-8e447fbdc86f",
      "type": "miku_model",
      "name": "miku"
    }
  ]
}
```

`asset.json` 保存动态字段和无损恢复类型所需的内部标签。这些标签属于持久化实现，
不是公开交换格式。导入和删除成功后 catalog 会立即提交；Windows 和 POSIX 平台均
使用原生替换与持久化操作。

## 身份与 Runtime 所有权

`UUID` 是拥有型 16 字节值，`GetUUID()` 按值返回。`ParseUUID()` 和
`FormatUUID()` 负责文本转换。全零 UUID 是 nil，不会写入 catalog。

UUID 字段只表达身份，不提供强所有权。Runtime 根据场景需求加载目标资产，并管理
已加载资产与 GPU 资源缓存。FyuuAsset 负责 CPU 侧导入、动态表示和持久化，不创建
RHI 资源。

## 验证

启用 `BUILD_TESTING` 后，`FyuuAsset.Import` 覆盖 JSON、二进制回退、模型与纹理
导入、目录包、UTF-8 路径、多模型入口、catalog 加载、析构保存和删除行为：

```text
ctest --test-dir <build-directory> -R ^FyuuAsset.Import$ --output-on-failure
```

完成通知可能从内部工作线程触发，上层必须同步共享状态。资产上下文是进程级单例，
第一次初始化后不能切换根目录。
