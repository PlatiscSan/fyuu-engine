# FyuuMath

[English](README.md)

FyuuMath 是 FyuuEngine 独立的 C++23 数学接口库。它通过 Concepts 和 `MathTraits` 适配应用类型，以运算符表达式执行计算，并通过 ADL 查找 `tag_invoke` 实现后端定制。库不提供公开的向量、矩阵、四元数或存储类。

## 构建集成

需要 CMake 4.2.3 或更新版本，以及支持 C++23 命名模块的工具链。在已有 FyuuMath 目标的工程中链接：

```cmake
target_link_libraries(MyApplication PRIVATE Fyuu::Math)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

在 C++ 源文件中导入：

```cpp
import fyuu_math;
```

目标会传播 C++23 要求及一致的 RTTI 设置。应用代码应使用公开接口，不访问实现命名空间。

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `FYUU_MATH_ENABLE_SIMD` | `ON` | 在支持的平台启用内部 SIMD 调度 |
| `FYUU_MATH_WITH_GLM` | `OFF` | 构建 `Fyuu::MathGLM`，导入 `fyuu_math_glm` |
| `FYUU_MATH_WITH_EIGEN` | `OFF` | 构建 `Fyuu::MathEigen`，导入 `fyuu_math_eigen` |
| `BUILD_TESTING` | CTest 默认 `ON` | 构建并注册协议测试 |

启用 GLM 或 Eigen 适配器时，其头文件必须能被 CMake 找到；使用方链接对应适配器目标。引擎公开接口仍可使用自己的数据类型，不必暴露后端类型。

## 适配应用类型

在首次使用类型前定义 `MathTraits<T>` 特化。下面的 `Position` 属于应用，不是库内存储类型：

```cpp
#include <cstddef>
import fyuu_math;

struct Position {
    float x, y, z;
};

template <> struct fyuu_math::MathTraits<Position> {
    using Scalar = float;
    static constexpr auto category = fyuu_math::Category::Vector;
    static constexpr bool is_owning = true;
    static constexpr std::size_t extent = 3;

    static constexpr float Read(Position const& v, std::size_t i) noexcept {
        return i == 0 ? v.x : i == 1 ? v.y : v.z;
    }
    static constexpr Position Create() noexcept { return {}; }
    static constexpr void Write(Position& v, std::size_t i, float s) noexcept {
        if (i == 0) v.x = s;
        else if (i == 1) v.y = s;
        else v.z = s;
    }
};

int main() {
    namespace fm = fyuu_math;
    Position a{1, 2, 3}, b{4, 5, 6};
    auto sum = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<Position>;
    auto dot = fm::AsVector(a) | fm::Dot{fm::AsVector(b)};
    return sum.x == 5 && dot == 32 ? 0 : 1;
}
```

当前标量支持 `float`、`double`，维度在编译期确定。

| 类别 | 形状信息 | 分量读取 |
| --- | --- | --- |
| 向量 | `extent` | `Read(value, index)` |
| 矩阵 | `rows`、`columns` | `Read(value, row, column)` |
| 四元数 | 四个逻辑分量 | `Read(value, QuaternionComponent)` |

`Read` 必须准确返回 `Scalar`。拥有型输出还需声明 `is_owning = true`，实现准确返回目标类型的 `Create()`，以及使用相同逻辑坐标、额外接收标量的 `Write`。`Create` 创建有效、可写且存储独立的对象，无须初始化随后会被覆盖的分量，也不要求类型具有默认构造函数。Traits 协议不再规定容器类型。

可选的 `Data(value)` 必须以 `noexcept` 返回 `Scalar const*`，指向按逻辑顺序排列的真实连续数组。不要将结构体中独立声明的成员当成数组访问。跨步或不同排列的数据可省略 `Data`，由 `Read` 提供逻辑分量。默认算法的输出必须提供 `Write`；可写 `Data(value)` 重载可返回具有相同排列保证的 `Scalar*`，让逐分量 SIMD 内核直接写入目标对象。

## 适配入口与生命周期

`AsVector(value)`、`AsMatrix(value)`、`AsQuaternion(value)` 根据对应 Traits 类别创建表达式操作数。原始数组或 span 不会自动获得数学类别，需要调用方提供适配。

- 左值被借用，捕获时不复制、不读取分量。
- 右值由操作数持有，遵循该类型的移动或复制语义。
- 被借用的数据须存活至求值完成；修改原对象会影响之后的求值。
- 持有视图或 Eigen 表达式对象，不会延长其引用数据的生命周期。

在输入仍然有效时，通过 `expression >> As<Output>` 获取独立结果。复用拥有型表达式来构造新节点可能复制其持有值。表达式类型不用于 ABI 或序列化。

## 运算接口

下表的 `a`、`b`、`v`、`m`、`q` 指适配入口返回的操作数或兼容表达式，`s` 使用相同标量类型。

| 表达式 | 含义 |
| --- | --- |
| `a + b`、`a - b`、`-a` | 向量或矩阵加减、取负 |
| `a * s`、`s * a` | 向量或矩阵缩放 |
| `a / s` | 带错误返回的向量或矩阵除法 |
| `a \| Dot{b}` | 向量内积，立即返回标量 |
| `a \| Cross{b}` | 三维向量右手叉积 |
| `m * b` | 矩阵乘矩阵或矩阵乘向量 |
| `q1 * q2` | Hamilton 乘积，先作用 q2 再作用 q1 |
| `q * v` | 单位四元数旋转三维向量 |
| `v \| Length` | 长度，立即返回标量 |
| `m \| Transpose`、`q \| Conjugate` | 转置、共轭 |
| `v \| Normalize{tolerance}` | 向量或四元数归一化，可返回错误 |
| `m \| Inverse{tolerance}` | 3×3 或 4×4 求逆，可返回错误 |
| `Identity >> As<Output>` | 生成方阵单位矩阵 |

名称需使用 `fyuu_math::` 或命名空间别名限定。组合管道时添加括号，例如 `((a * b) | Transpose) >> As<Output>`。

`As<Output>` 选择拥有型结果，要求形状及标量类型兼容。它可改变后端表示，但当前不能用于 `float` 与 `double` 之间的转换。未经过适配入口的后端对象仍使用自己的原生运算符。

## 错误处理

除法、归一化和求逆在物化时返回 `std::expected<Output, MathError>`。先检查结果，再将值接入后续表达式：

```cpp
auto divided = (fm::AsVector(a) / 2.0f) >> fm::As<Position>;
if (!divided) {
    // Handle divided.error().
} else {
    auto length = fm::AsVector(*divided) | fm::Length;
}
```

正零和负零除数都会在求值源操作数及调用后端之前返回 `std::unexpected(MathError::DivisionByZero)`。非零除法遵循普通浮点行为，包括 NaN 和无穷大；它不会额外保证结果有限。

`Tolerance<S>{absolute, relative}` 控制检查算法，两个字段必须有限且非负。归一化可拒绝退化或非有限输入，求逆可报告奇异矩阵，变换和投影还会检查几何参数或约定。类型和维度不匹配是编译期错误，不转换为 `std::unexpected`。

## 后端定制

运算通过 ADL 查找 `tag_invoke`。在操作数类型的关联命名空间中定义重载。普通运算的签名形式为：

```cpp
Output tag_invoke(fyuu_math::AddTag,
                 fyuu_math::ResultType<Output>,
                 InputA const&, InputB const&);
```

返回值必须准确匹配目标类型；重载歧义或错误返回类型会导致编译失败。没有定制时，使用基于 Traits 的默认实现。ADL 是 `tag_invoke` 的查找机制，不是额外按运算名称寻找函数的一层回退。归一化与求逆定制使用带错误的返回值，并接收容差参数。

## 变换与投影

采用列向量，`A * B` 先作用 B。逻辑坐标不依赖物理存储的行主序或列主序。四元数使用逻辑 XYZW 分量和 Hamilton 乘法。

公开辅助函数包括 `TryComposeTransform`、`TryViewFromPose`、`TryTransformPoint`、`TryTransformDirection`、`TryProjectPoint` 和 `TryTransformNormal`。这些函数直接接收具有 Traits 的后端值，返回带错误的结果。点接受平移，方向不接受平移，法线使用线性部分的逆转置。

`TryPerspective`、`TryOrthographic` 接收包含显式 `ProjectionConvention` 的描述符，指定左右手系、深度范围和正向或反向深度。应按渲染器要求选择，不从存储顺序推断。

## SIMD 与验证

默认开启 SIMD。内部包含 x86 SSE2 和 ARM64 NEON 路径，实际调度也取决于运算、维度和编译器。非连续输入可以通过 Traits 收集分量。`SIMDCompatible<T>` 表示逻辑连续输入的适配资格，不能证明某次调用一定进入 SIMD。寄存器类型和架构头文件不属于公开接口。

在仓库根目录运行：

```powershell
./lib/fyuu_math/test/compare.ps1
```

脚本分别以 SIMD `ON`、`OFF` 建立 Release 构建目录，编译同一套测试并运行 CTest。默认使用 `clang++`，可通过 `-Compiler` 选择兼容其 Ninja 配置的编译器。也可自行配置 `BUILD_TESTING=ON`，构建后运行 `ctest --test-dir <build> -V`。

测试覆盖所有权、编译期求值、零除错误、float/double 向量运算、SIMD 尾部、NaN、无穷大、正负零、矩阵乘法、矩阵向量乘法、单位矩阵求逆及四元数运算。运行期检查在 Release 中有效，测试只使用公开接口。

计时样例测量 64 元素 float 加法，不代表所有运算。关闭库内 SIMD 不会禁止编译器自动向量化。目前本机验证使用 Windows x86-64 上的 Clang，这些运行结果不构成 ARM64 真机验证。
