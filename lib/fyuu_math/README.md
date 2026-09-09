# FyuuMath

[简体中文](README.zh-CN.md)

FyuuMath is FyuuEngine's standalone C++23 math interface library. It adapts
application types through Concepts and `MathTraits`, evaluates operator
expressions, and lets backends hook in through ADL-found `tag_invoke`. It
defines no public vector, matrix, quaternion, or storage types of its own.

## Build integration

Requires CMake 4.2.3 or newer and a toolchain with C++23 named-module support:

```cmake
target_link_libraries(MyApplication PRIVATE Fyuu::Math)
set_target_properties(MyApplication PROPERTIES CXX_EXTENSIONS OFF)
```

```cpp
import fyuu_math;
```

| Option | Default | Purpose |
| --- | --- | --- |
| `FYUU_MATH_ENABLE_SIMD` | `ON` | Enable internal SIMD dispatch on supported targets |
| `FYUU_MATH_WITH_GLM` | `OFF` | Build `Fyuu::MathGLM`; import `fyuu_math_glm` |
| `FYUU_MATH_WITH_EIGEN` | `OFF` | Build `Fyuu::MathEigen`; import `fyuu_math_eigen` |
| `BUILD_TESTING` | CTest default: `ON` | Build and register the contract tests |

GLM and Eigen adapters require their headers to be findable by CMake.

## Adapt an application type

Specialize `MathTraits<T>` before first use. The example below adapts an
application-owned `Position`:

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

Supported scalars are `float` and `double`; dimensions are fixed at compile
time.

| Category | Required shape metadata | Component read |
| --- | --- | --- |
| Vector | `extent` | `Read(value, index)` |
| Matrix | `rows`, `columns` | `Read(value, row, column)` |
| Quaternion | Four logical components | `Read(value, QuaternionComponent)` |

`Read` returns `Scalar`. Owning outputs also set `is_owning = true`, implement
`Create()` to return their own type, and provide `Write` over the same logical
coordinates as `Read`. `Create` only has to build a valid, writable, independently
stored object: it need not initialize components an operation overwrites, and no
default constructor is required. `Data(value)` is optional and returns a
`Scalar const*` (`noexcept`) to the contiguous storage in logical order. Strided
or reordered layouts may omit it — `Read` supplies their logical components and
fallback results are written through `Write`.

## Adaptation and lifetime

`AsVector`, `AsMatrix`, and `AsQuaternion` wrap a value as an operand of its
Traits category. Raw arrays and spans gain no category automatically; adapt them
explicitly.

- Lvalues are borrowed; rvalues are owned by the operand.
- Borrowed data must stay alive until evaluation completes; earlier edits are observed.
- Owning a view or Eigen expression does not extend the life of its underlying data.

While inputs are valid, `expression >> As<Output>` materializes an independent
result. Reusing an owning expression to compose new nodes may copy the values it
holds.

## Operations

Here `a`, `b`, `v`, `m`, and `q` are operands from the adaptation entry points
or compatible expressions; `s` shares their scalar type.

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

Qualify operation names with `fyuu_math::` or a namespace alias, and parenthesize
pipelines: `((a * b) | Transpose) >> As<Output>`.

`As<Output>` selects an owning result type whose shape and scalar type are
compatible. It can change the backend representation, but not convert between
`float` and `double`.

## Error handling

Division, normalization, and inverse return `std::expected<Output, MathError>`.
Check before using the value:

```cpp
auto divided = (fm::AsVector(a) / 2.0f) >> fm::As<Position>;
if (!divided) {
    // Handle divided.error().
} else {
    auto length = fm::AsVector(*divided) | fm::Length;
}
```

Any zero divisor — positive or negative — yields `DivisionByZero` before the
backend runs; nonzero division follows ordinary floating-point semantics.
`Tolerance<S>{absolute, relative}` drives the checked algorithms; both fields
must be finite and nonnegative. Type or dimension mismatches are compile-time
errors, never `std::unexpected`.

## Backend customization

Operations reach `tag_invoke` through ADL; define an overload in an operand
type's associated namespace:

```cpp
Output tag_invoke(fyuu_math::AddTag,
                 fyuu_math::ResultType<Output>,
                 InputA const&, InputB const&);
```

The return type must match the target type or compilation fails. Without a hook,
operations fall back to the Traits-based default. Normalization and inverse
hooks return checked results and take a tolerance argument.

## Transforms and projections

Column-vector convention: `A * B` applies `B` first, independent of physical
row-major or column-major storage. Quaternions use logical XYZW components and
Hamilton multiplication.

`TryComposeTransform`, `TryViewFromPose`, `TryTransformPoint`,
`TryTransformDirection`, `TryProjectPoint`, and `TryTransformNormal` take
Traits-adapted values directly and return checked results. Points translate,
directions do not, and normals transform by the inverse transpose of the linear
part.

`TryPerspective` and `TryOrthographic` take a descriptor with an explicit
`ProjectionConvention` — handedness, depth range, forward or reversed depth.
Match it to the renderer; do not infer it from storage order.

## SIMD and verification

SIMD is on by default, with x86 SSE2 and ARM64 NEON kernels; actual dispatch
depends on the operation, dimensions, and compiler. `SIMDCompatible<T>` marks
logically contiguous input as SIMD-eligible, not as a guarantee that a given
call uses SIMD.

From the repository root, run `./lib/fyuu_math/test/compare.ps1` to build and
run the same tests with SIMD `ON` and `OFF` (clang++ by default, selectable with
`-Compiler`). Alternatively configure with `BUILD_TESTING=ON` and run `ctest`.
