# FyuuAsset 使用指南 / FyuuAsset User Guide

FyuuAsset 是一个基于 C++ Modules 的资产系统，提供 UUID 标识、侵入式强弱引用、内存注册表、自动异步保存和按 UUID 加载。

FyuuAsset is a C++ Modules asset system providing UUID identity, intrusive strong/weak ownership, an in-memory registry, automatic asynchronous persistence, and UUID-based loading.

- [中文](#中文)
- [English](#english)

---

## 中文

### 初始化

链接 `FyuuAsset` 并导入主模块：

```cmake
target_link_libraries(MyTarget PRIVATE FyuuAsset)
```

```cpp
import fyuu_asset;
```

创建或加载资产前设置根目录。默认目录为 `./asset`。

```cpp
fyuu_asset::SetRoot("assets");
```

资产目录结构：

```text
asset_root/类型名/uuid.json
asset_root/类型名/uuid.bin   # 含二进制数据的资产
```

支持 C++26 静态反射时直接使用反射类型名；fallback 使用 Boost.TypeIndex，并删除命名空间、MSVC 模块后缀以及非字母、数字、下划线字符。

### 创建和访问资产

`Asset<T>::Create` 会将参数直接转发给 `T`：

```cpp
auto shader = fyuu_asset::Asset<fyuu_asset::Shader>::Create(
    std::string{ "triangle" },
    std::string{ "[shader(\"vertex\")] void vertexMain() {}" }
);

auto id = shader->GetID();
shader->Get().source += "\n";
```

`ManagedAsset` 是 `boost::intrusive_ptr`。资产对象、引用计数、UUID 和数据位于同一次分配中。

```cpp
using ShaderAsset = fyuu_asset::Asset<fyuu_asset::Shader>;

auto loaded = ShaderAsset::Find(id);
ShaderAsset::Weak weak{ loaded };

loaded.reset();
auto restored = weak.Lock();
```

`Find` 只查询当前已加载资产，不访问磁盘。

### 生命周期与自动保存

最后一个强引用释放时：

1. 从已加载注册表移除资产。
2. 将资产数据移动到序列化线程。
3. 转移序列化所有权后销毁原数据。
4. 已存在的弱引用可以继续存在，但不能复活数据。

系统有意不提供 `Unload`。释放所有权就是卸载。序列化在库内单线程异步执行；错误只写入日志，不会返回到释放引用的线程。

如果其他进程需要立即读取刚保存的文件，不要在释放资产后立刻结束进程。

### 加载资产

```cpp
fyuu_asset::execution::AssetLoader loader;
auto shader = loader.Load<fyuu_asset::Shader>(id);
```

`Load<T>` 首先调用 `Asset<T>::Find`。未找到时从资产根目录读取并注册实例。文件不存在或内容非法时会抛出异常。

`AssetScheduler` 是 execution 集成使用的单线程调度原语；普通同步 `Load` 不需要它。

### 定义普通 JSON 资产

公开字段会被保存为 JSON。优先使用 C++26 静态反射；不支持静态反射时使用 Boost.Describe：

```cpp
struct Settings {
    int width = 1280;
    std::string title;
};

#if !defined(__cpp_lib_reflection)
BOOST_DESCRIBE_STRUCT(
    Settings,
    (),
    (width, title)
)
#endif
```

fallback 加载逻辑会默认构造对象，然后给公开描述字段赋值。不适合这种方式的类型可以提供自定义持久化函数。

### 自定义持久化

类型提供以下签名时，优先于反射逻辑：

```cpp
void Serialize(std::filesystem::path const& json_path) const;
static T Deserialize(std::filesystem::path const& json_path);
```

传入路径是最终的 `类型名/uuid.json`。类型可以通过 `replace_extension` 生成附属文件，Bitmap、Audio 和 Mesh 都使用这种方式。建议先原子发布二进制文件，最后发布 JSON 元数据。

### 内置资产

| 类型 | 内容 | 存储方式 |
|---|---|---|
| `Bitmap` | 尺寸、像素格式、解码后的紧密排列像素 | JSON + `.bin` |
| `Audio` | 采样率、声道、PCM 格式、交错采样数据 | JSON + `.bin` |
| `Shader` | Slang 模块名和源码 | JSON |
| `Pipeline` | 图形/计算类型、Shader UUID 和入口点 | JSON |
| `Mesh` | 顶点步长、顶点字节、索引格式、索引字节 | JSON + `.bin` |
| `Material` | Pipeline UUID、数值参数、Bitmap UUID 绑定 | JSON |

二进制数据统一使用 `std::vector<std::byte>`。数据含义由格式和元数据决定，不会转换成 JSON 数字数组。

### Bitmap 示例

```cpp
auto bitmap = fyuu_asset::Asset<fyuu_asset::Bitmap>::Create(
    1u,
    1u,
    fyuu_asset::BitmapFormat::R8G8B8A8,
    std::vector<std::byte>{
        std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 }
    }
);
```

### Material 示例

```cpp
fyuu_asset::Material material{
    .pipeline = pipeline_id,
    .parameters = {
        { "base_color", { 1.0f, 1.0f, 1.0f, 1.0f } }
    },
    .bitmaps = {
        { "albedo", bitmap_id }
    }
};
```

资产间引用保存 UUID，而不是强指针。加载 Material 或 Pipeline 时不会自动加载其依赖资产。

---

## English

FyuuAsset is a small C++ module-based asset library. It provides UUID identity, intrusive strong/weak ownership, an in-memory registry, automatic asynchronous persistence when the last strong reference is released, and synchronous loading by UUID.

## Setup

Link `FyuuAsset` and import the primary module:

```cmake
target_link_libraries(MyTarget PRIVATE FyuuAsset)
```

```cpp
import fyuu_asset;
```

Set the asset root before creating or loading assets. The default is `./asset`.

```cpp
fyuu_asset::SetRoot("assets");
```

Files use this layout:

```text
asset_root/TypeName/uuid.json
asset_root/TypeName/uuid.bin   # assets with binary payloads
```

Type names come from C++26 reflection when available. The fallback uses Boost.TypeIndex and removes namespace, module suffix, and characters that are not letters, digits, or underscores.

## Create and access an asset

`Asset<T>::Create` forwards its arguments directly to `T`.

```cpp
auto shader = fyuu_asset::Asset<fyuu_asset::Shader>::Create(
    std::string{ "triangle" },
    std::string{ "[shader(\"vertex\")] void vertexMain() {}" }
);

auto id = shader->GetID();
shader->Get().source += "\n";
```

`ManagedAsset` is a `boost::intrusive_ptr`. The asset object, reference counters, UUID, and payload share one allocation.

```cpp
using ShaderAsset = fyuu_asset::Asset<fyuu_asset::Shader>;

auto loaded = ShaderAsset::Find(id);
ShaderAsset::Weak weak{ loaded };

loaded.reset();
auto restored = weak.Lock();
```

`Find` only searches currently loaded assets. It does not access storage.

## Lifetime and persistence

When the final strong reference is released:

1. The asset is removed from the loaded registry.
2. Its payload is moved to the serialization worker.
3. The payload is destroyed after serialization ownership is transferred.
4. Existing weak references remain valid but cannot resurrect the payload.

There is intentionally no `Unload` function. Releasing ownership unloads the asset. Serialization runs asynchronously on the library worker; failures are logged and are not reported to the releasing thread.

Do not terminate the process immediately after dropping an asset if the resulting file must be consumed by another process at once.

## Load an asset

```cpp
fyuu_asset::execution::AssetLoader loader;
auto shader = loader.Load<fyuu_asset::Shader>(id);
```

`Load<T>` first checks `Asset<T>::Find`. If absent, it reads the asset from the configured root and registers the loaded instance. Missing or malformed files throw.

`AssetScheduler` is the single-thread scheduling primitive used by the execution integration. It is not required for ordinary synchronous `Load` calls.

## Define a JSON asset

Public fields are serialized to JSON. C++26 reflection is preferred. On compilers without static reflection, describe the type with Boost.Describe:

```cpp
struct Settings {
    int width = 1280;
    std::string title;
};

#if !defined(__cpp_lib_reflection)
BOOST_DESCRIBE_STRUCT(
    Settings,
    (),
    (width, title)
)
#endif
```

The fallback loader default-constructs the object and assigns its public described fields. A type that cannot use this path may provide custom persistence.

## Custom persistence

Types with these signatures take precedence over reflection:

```cpp
void Serialize(std::filesystem::path const& json_path) const;
static T Deserialize(std::filesystem::path const& json_path);
```

The path is the final `TypeName/uuid.json` path. A custom type may derive sidecar paths with `replace_extension`, as Bitmap, Audio, and Mesh do. Writers should publish temporary files atomically and publish JSON metadata after its binary payload.

## Built-in assets

| Type | Contents | Storage |
|---|---|---|
| `Bitmap` | Dimensions, pixel format, decoded tightly packed pixels | JSON + `.bin` |
| `Audio` | Sample rate, channels, PCM format, interleaved samples | JSON + `.bin` |
| `Shader` | Slang module name and source | JSON |
| `Pipeline` | Graphics/compute type and Shader UUID entry points | JSON |
| `Mesh` | Vertex stride, vertex bytes, index format, index bytes | JSON + `.bin` |
| `Material` | Pipeline UUID, numeric parameters, Bitmap UUID bindings | JSON |

Binary payloads use `std::vector<std::byte>`. Their interpretation is determined by the corresponding format and metadata; bytes are never converted to JSON numbers.

## Example: Bitmap

```cpp
auto bitmap = fyuu_asset::Asset<fyuu_asset::Bitmap>::Create(
    1u,
    1u,
    fyuu_asset::BitmapFormat::R8G8B8A8,
    std::vector<std::byte>{
        std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 }
    }
);
```

## Example: Material

```cpp
fyuu_asset::Material material{
    .pipeline = pipeline_id,
    .parameters = {
        { "base_color", { 1.0f, 1.0f, 1.0f, 1.0f } }
    },
    .bitmaps = {
        { "albedo", bitmap_id }
    }
};
```

Asset references are UUIDs rather than strong pointers. Loading a Material or Pipeline does not automatically load its dependencies.
