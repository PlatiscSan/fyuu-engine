# FyuuMath

[English](README.md)

FyuuMath 是 FyuuEngine 的独立 C++23 数学接口库。它通过 Concepts 与 `MathTraits` 适配应用类型，用运算符表达式执行计算，并借助 ADL 查找到的 `tag_invoke` 扩展后端；库本身不提供公开的向量、矩阵、四元数或存储类型。

## 构建集成

需要 CMake 4.2.3 或更高版本，以及支持 C++23 命名模块的工具链：

```cmake
target_link_libraries(MyApplication PRIVATE Fyuu::Math)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

```cpp
import fyuu_math;
```

| 选项 | 默认值 | 用途 |
| --- | --- | --- |
| `FYUU_MATH_ENABLE_SIMD` | `ON` | 启用内部 SIMD 调度（平台支持时） |
| `FYUU_MATH_WITH_GLM` | `OFF` | 构建 `Fyuu::MathGLM`，导入 `fyuu_math_glm` |
| `FYUU_MATH_WITH_EIGEN` | `OFF` | 构建 `Fyuu::MathEigen`，导入 `fyuu_math_eigen` |
| `BUILD_TESTING` | CTest 默认 `ON` | 构建并注册协议测试 |

启用 GLM 或 Eigen 适配器时，其头文件需能被 CMake 找到。

## 适配应用类型

首次使用前，先为类型定义 `MathTraits<T>` 特化。以应用自有类型 `Position` 为例：

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

支持的标量为 `float` 与 `double`。定长运算的形状在编译期确定。

| 类别 | 形状信息 | 分量读取 |
| --- | --- | --- |
| 向量 | `extent` | `Read(value, index)` |
| 矩阵 | `rows`、`columns` | `Read(value, row, column)` |
| 四元数 | 四个逻辑分量 | `Read(value, QuaternionComponent)` |

`Read` 返回 `Scalar`；拥有型输出还需声明 `is_owning = true`、实现返回目标类型的 `Create()`，以及按 `Read` 相同逻辑坐标写入的 `Write`。`Create` 只需产出有效、可写、存储独立的对象，不必初始化随后会被覆盖的分量，也不要求默认构造。`Data(value)` 可选，以 `noexcept` 返回逻辑序连续数组的 `Scalar const*`；跨步或异序布局可不提供，由 `Read` 给出逻辑分量，回退路径经 `Write` 输出。

## 适配入口与生命周期

`AsVector`、`AsMatrix`、`AsQuaternion` 按对应 Traits 类别把值包装成表达式操作数。库内建标量容器适配：一维原始数组 `S[N]`、`std::array<S, N>` 与定长 `std::span<S, E>` 自动作为编译期定长向量；矩阵只能由二维形式 `S[R][C]` 或 `std::array<std::array<S, C>, R>` 表示。`std::array` 拥有存储、可作结果类型；裸数组与 span 只是借用视图。此外，`AsVector` 也接受**动态长度**的 `std::span<S>` / `std::span<S const>`，其向量长度在运行期取 `span.size()`，见「运行期长度向量」一节。

- 左值被借用，右值由操作数持有。
- 借用数据须存活到求值结束，求值前的修改会被观察到。
- 持有视图或 Eigen 表达式不会延长其底层数据的生命周期。

输入仍有效时，用 `expression >> As<Output>` 得到独立结果；复用拥有型表达式构造新节点可能复制其持有值。

## 运算接口

下表 `a`、`b`、`v`、`m`、`q` 指适配入口返回的操作数或兼容表达式，`s` 为相同标量类型。

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

运算名以 `fyuu_math::` 或命名空间别名限定；组合管道时加括号，如 `((a * b) | Transpose) >> As<Output>`。

`As<Output>` 选择拥有型结果类型，要求形状与标量类型兼容，可更换后端表示，但不能在 `float` 与 `double` 间转换。

## 错误处理

除法、归一化与求逆返回 `std::expected<Output, MathError>`，需先检查再使用：

```cpp
auto divided = (fm::AsVector(a) / 2.0f) >> fm::As<Position>;
if (!divided) {
    // Handle divided.error().
} else {
    auto length = fm::AsVector(*divided) | fm::Length;
}
```

正负零除数都会在调用后端前返回 `DivisionByZero`；非零除法遵循普通浮点语义（含 NaN 与无穷）。`Tolerance<S>{absolute, relative}` 控制检查类算法，两字段须有限非负。类型与维度不符是编译期错误，不产生 `std::unexpected`。

## 运行期长度向量（动态 span）

`AsVector` 也接受动态长度的 `std::span<S>` / `std::span<S const>`，每个元素视为一个向量分量，长度在运行期决定。它支持加减、取负、`* s`、`s *`、`/ s`，以及 `| Length`、`| Dot{...}`；不提供矩阵或四元数语义，也不做 `float` 与 `double` 互转。

结果不分配内存，也不由库持有，而是物化到调用方缓冲或定长 owning 目标：

```cpp
std::array<float, 4> out{};
auto r = (fm::AsVector(a) + fm::AsVector(b)) >> fm::As<std::array<float, 4>>;
// 运行期长度与 N 不符时返回 SizeMismatch
auto ok = (fm::AsVector(a) * 2.0f) >> std::span<float>{out};  // std::expected<void, MathError>
```

运行期长度不符——操作数不等长、或表达式长度与输出长度不一致——返回 `MathError::SizeMismatch`，且失败时输出不被改写。`/ 0` 与 `/-0` 返回 `DivisionByZero`。动态下的 `| Length` 与 `| Dot` 返回 `std::expected`：树内部长度不符也会以 `SizeMismatch` 失败。定长运算的类型/维度不符依旧是编译期错误。

## 后端定制

运算经 ADL 查找 `tag_invoke`，在操作数类型的关联命名空间提供重载：

```cpp
Output tag_invoke(fyuu_math::AddTag,
                 fyuu_math::ResultType<Output>,
                 InputA const&, InputB const&);
```

返回类型须与目标类型一致，否则编译失败；未定制时回退到基于 Traits 的默认实现。归一化与求逆的重载返回可带错误的结果，并接收容差参数。

## 变换与投影

采用列向量约定，`A * B` 表示先作用 `B`；逻辑坐标与物理的行主序或列主序存储无关。四元数使用逻辑 XYZW 分量与 Hamilton 乘法。

`TryComposeTransform`、`TryViewFromPose`、`TryTransformPoint`、`TryTransformDirection`、`TryProjectPoint`、`TryTransformNormal` 直接接受已适配 Traits 的后端值，返回可带错误的结果。点随平移变换，方向不受平移影响，法线以线性部分的逆转置变换。

`TryPerspective` 与 `TryOrthographic` 的描述符含显式 `ProjectionConvention`（左右手系、深度范围、正向或反向深度），按渲染器要求选择，不从存储顺序推断。

## SIMD 与验证

SIMD 默认开启，内置 x86 SSE2 与 ARM64 NEON 内核；是否生效取决于运算、维度与编译器。`SIMDCompatible<T>` 表示逻辑连续输入具备 SIMD 适配资格，不代表某次调用一定进入 SIMD。

在仓库根目录运行 `./lib/fyuu_math/test/compare.ps1`，以 SIMD `ON`、`OFF` 两套配置编译并运行同一测试（默认 clang++，可用 `-Compiler` 换编译器）；也可用 `BUILD_TESTING=ON` 配置后直接运行 `ctest`。
