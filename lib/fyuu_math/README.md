# FyuuMath

[简体中文](README.zh-CN.md)

FyuuMath is FyuuEngine’s independent C++23 math interface library. It adapts
application types through Concepts and `MathTraits`, evaluates operator
expressions, and supports backend customization through ADL-discovered
`tag_invoke`. It supplies no public vector, matrix, quaternion, or storage class.

## Build integration

Use CMake 4.2.3 or newer and a toolchain with C++23 named-module support.
When the target is available in your build:

```cmake
target_link_libraries(MyApplication PRIVATE Fyuu::Math)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

Import the public module in C++ sources:

```cpp
import fyuu_math;
```

The target propagates the C++23 requirement and matching RTTI settings.
Applications should use public interfaces rather than implementation namespaces.

| Option | Default | Purpose |
| --- | --- | --- |
| `FYUU_MATH_ENABLE_SIMD` | `ON` | Enable internal SIMD dispatch on supported targets |
| `FYUU_MATH_WITH_GLM` | `OFF` | Build `Fyuu::MathGLM`; import `fyuu_math_glm` |
| `FYUU_MATH_WITH_EIGEN` | `OFF` | Build `Fyuu::MathEigen`; import `fyuu_math_eigen` |
| `BUILD_TESTING` | CTest default: `ON` | Build and register the contract tests |

GLM and Eigen headers must already be available to CMake when their adapters are
enabled. Link the corresponding adapter target in consumers. Adapters do not
require engine interfaces to expose backend types.

## Adapt an application type

Define a `MathTraits<T>` specialization before adapting the type. This complete
example uses an application-owned type, not a library storage class:

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

Current scalar support is `float` and `double`, with fixed dimensions.

| Category | Required shape metadata | Component read |
| --- | --- | --- |
| Vector | `extent` | `Read(value, index)` |
| Matrix | `rows`, `columns` | `Read(value, row, column)` |
| Quaternion | Four logical components | `Read(value, QuaternionComponent)` |

`Read` returns exactly `Scalar`. An owning output additionally declares
`is_owning = true`, implements `Create()` returning exactly the output type,
and implements `Write` with the same logical coordinates as `Read` plus a scalar.
`Create` establishes a valid writable object with independent storage; it need
not initialize components that the operation will overwrite. No default
constructor is required. No container type is imposed by the Traits protocol.

Optional `Data(value)` returns `Scalar const*` with `noexcept`, pointing to a real
contiguous sequence in logical order. Do not treat separate struct members as an
array. Omit `Data` for strided or differently ordered representations; `Read`
provides their logical components. `Write` is required for fallback outputs.
A mutable `Data(value)` overload may return `Scalar*` with the same logical
layout guarantee. Component SIMD kernels can then write directly to the output.

## Adaptation and lifetime

`AsVector(value)`, `AsMatrix(value)`, and `AsQuaternion(value)` select the matching
Traits category and create an expression operand. They accept adapted types;
raw arrays and spans do not acquire a mathematical category automatically.

- Lvalues are borrowed without copying or reading their components at capture.
- Rvalues are owned by the operand, subject to the type’s move/copy semantics.
- Borrowed data must remain valid until evaluation; changes are observed at evaluation.
- Owning a view or an Eigen expression does not extend its referenced data’s lifetime.

Use `expression >> As<Output>` while inputs are valid to obtain an independent
result. Reusing an owning expression can copy its stored values when composing
new expression nodes. Expression types are not an ABI or serialization format.

## Operations

In the table, `a`, `b`, `v`, `m`, and `q` are operands returned by the adaptation
functions or compatible expressions; `s` has the same scalar type.

| Expression | Meaning |
| --- | --- |
| `a + b`, `a - b`, `-a` | Vector or matrix addition, subtraction, negation |
| `a * s`, `s * a` | Vector or matrix scaling |
| `a / s` | Checked vector or matrix division |
| `a \| Dot{b}` | Vector dot product; returns a scalar immediately |
| `a \| Cross{b}` | Right-handed cross product of 3-component vectors |
| `m * b` | Matrix-matrix or matrix-vector multiplication |
| `q1 * q2` | Hamilton product; applies q2 before q1 |
| `q * v` | Unit-quaternion rotation of a 3-component vector |
| `v \| Length` | Length; returns a scalar immediately |
| `m \| Transpose`, `q \| Conjugate` | Transpose or conjugate |
| `v \| Normalize{tolerance}` | Checked vector or quaternion normalization |
| `m \| Inverse{tolerance}` | Checked 3×3 or 4×4 inverse |
| `Identity >> As<Output>` | Materialize a square identity matrix |

Qualify operation names with `fyuu_math::`, or use a namespace alias.
Parenthesize pipelines: `((a * b) | Transpose) >> As<Output>`.

`As<Output>` selects an owning result type. Shapes and scalar types must be
compatible. It supports changing backend representation, but currently does not
permit changing `float` to `double` or vice versa. Plain backend objects retain
their native operators until explicitly adapted.

## Error handling

Division, normalization, and inverse expressions materialize as
`std::expected<Output, MathError>`. Check the result before adapting its value:

```cpp
auto divided = (fm::AsVector(a) / 2.0f) >> fm::As<Position>;
if (!divided) {
    // Handle divided.error().
} else {
    auto length = fm::AsVector(*divided) | fm::Length;
}
```

Both positive and negative zero divisors return
`std::unexpected(MathError::DivisionByZero)` before evaluating the source or
calling its backend. Nonzero division preserves ordinary floating-point
behavior, including NaN and infinity; it is not a general finiteness check.

`Tolerance<S>{absolute, relative}` controls checked algorithms. Both fields must
be finite and nonnegative. Normalization can reject degenerate or nonfinite
inputs; inverse can report singular matrices. Transform and projection helpers
also report invalid geometry or conventions. Type and dimension errors are
compile-time errors, not `std::unexpected` results.

## Backend customization

Operation dispatch finds `tag_invoke` through ADL. Place an overload in an
operand type’s associated namespace. Ordinary operations use this signature:

```cpp
Output tag_invoke(fyuu_math::AddTag,
                 fyuu_math::ResultType<Output>,
                 InputA const&, InputB const&);
```

The result must be exactly the requested output type. Ambiguous or incorrectly
typed hooks fail compilation. Without a hook, the library uses its Traits-based
fallback. ADL is the lookup mechanism for `tag_invoke`, not a separate fallback
search for functions named after each operation. Normalization and inverse
hooks use checked results and include a tolerance argument.

## Transforms and projections

Transforms use column vectors. Matrix multiplication `A * B` applies B first.
Logical coordinates are independent of physical row-major or column-major storage.
Quaternions use logical XYZW components and Hamilton multiplication.

The public helpers are `TryComposeTransform`, `TryViewFromPose`,
`TryTransformPoint`, `TryTransformDirection`, `TryProjectPoint`, and
`TryTransformNormal`. They take Traits-adapted backend values directly and return
checked results. Points receive translation; directions do not. Normals use the
inverse transpose of the linear transform.

`TryPerspective` and `TryOrthographic` take descriptors with an explicit
`ProjectionConvention`: handedness, depth range, and forward or reversed depth.
Select these conventions to match the renderer; do not infer them from storage layout.

## SIMD and verification

SIMD is enabled by default. Internal kernels include x86 SSE2 and ARM64 NEON
paths; dispatch also depends on the operation, dimensions, and compiler.
Noncontiguous inputs can be gathered through Traits. `SIMDCompatible<T>` describes
logical contiguous input eligibility, not proof that a particular call uses SIMD.
Register types and architecture headers do not form part of the public interface.

From the repository root, run:

```powershell
./lib/fyuu_math/test/compare.ps1
```

The script builds the same tests in separate Release configurations with SIMD
`ON` and `OFF`, then runs CTest. It defaults to `clang++`; use `-Compiler` to select
a compatible compiler for its Ninja configuration. Alternatively, configure the
library with `BUILD_TESTING=ON`, build, and run `ctest --test-dir <build> -V`.

Tests cover ownership, constexpr evaluation, zero-division errors, float/double
vector operations, lane tails, NaN/infinity/signed zero, matrix multiplication,
matrix-vector multiplication, identity inverse, and quaternion operations.
Runtime checks remain active in Release. Tests consume public interfaces only.

The timing sample measures 64-component float addition. It does not characterize
all operations, and disabling library SIMD does not disable compiler automatic
vectorization. Current local validation used Clang on Windows x86-64; these runs
do not validate execution on ARM64 hardware.
