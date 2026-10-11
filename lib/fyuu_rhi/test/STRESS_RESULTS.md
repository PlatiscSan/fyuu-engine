# RHI stress test — 2026-10-11

Configuration: Windows, MSVC 14.51.36231, Release, real GPU backends.
Metal and other operating systems were not tested.

## Workloads

- Completion stress: four producer threads share one scheduler. Eight waves
  alternate 1,024 and 8 graphs (4,128 graphs per run). Each graph creates an
  upload buffer, GPU buffer and readback buffer, copies through the GPU, and
  checks all 256 returned bytes. Terminal delivery must occur exactly once.
- Resource-group stress: three cycles create 4,096 live groups, release 2,048,
  create 2,048 replacements, then release the groups. This is 18,432 creations
  per run. A final draw and pixel readback checks that the pipeline still works.
- Each passing workload was repeated three times. GPU test processes ran
  sequentially; submission threads within each completion test ran concurrently.

## Initial results before synchronization/context fixes

| Backend | Concurrent GPU completion | Resource-group churn |
| --- | --- | --- |
| D3D12 | Passed three runs | Passed three runs |
| Vulkan | Passed three runs | Passed three runs |
| OpenGL | Stalled before first wave report | Passed three runs |
| WebGPU | Access violation before first wave report | Passed three runs |

CTest reported 6/8 workloads passing. The OpenGL process was explicitly stopped
after 84.8 seconds of no wave progress; this is a failed stress run, not a normal
completion or proof of a particular deadlock. WebGPU was reported as SEGFAULT
after 10.92 seconds. Follow-up diagnosis and corrected results appear below.

The preceding serial-submission smoke run passed all eight workloads, but its
completion workload only wrote a host-visible buffer; it does not replace the
concurrent GPU-copy results above.

## Memory observations and limitations

The serial run observed host available physical memory between approximately
4.5% and 8.1%. No artificial allocation was used to exhaust host memory. Process
private committed memory and working set were reported separately; short-run
plateaus are not proof of absence of leaks. GPU-local memory pressure, timeline
semaphore fallback, multiple presentation targets, resizing and Metal were not
covered by these workloads.

## Reproduction

```text
ctest --test-dir cmake-build/MSVC_Visual_Studio_2026_vcpkg -C Release -R "^(CompletionPool\..*\.stress|DrawProbe\..*\.pool_stress)$" --repeat until-fail:3 --output-on-failure -j 1
```

## Follow-up diagnosis and synchronization fix

WebGPU device creation did not request Dawn's `ImplicitDeviceSynchronization`.
The installed Dawn source creates its device mutex only when this feature is
enabled. Four producers were consequently entering an unsynchronized device.
Device creation now requests the feature when available. Otherwise the existing
device state supplies an external mutex for native API calls. Mapping waits
release that mutex, and the progress pump uses only zero-timeout waits under it.
No public RHI interface was changed.

The final MSVC Release binaries passed all 14 WebGPU completion/draw workloads
both with Dawn synchronization and with `FYUU_RHI_WEBGPU_FORCE_EXTERNAL_SYNC=1`
(28 workload executions). This includes the concurrent GPU-copy/readback test.
The switch does not request the feature, so it exercises external synchronization
on a real device rather than merely setting a flag after device creation.
After strengthening the payload to distinguish every graph (rather than just
the low byte of its index), native and external-sync completion stress each
passed three consecutive runs. External-sync pool churn, repeated constants and
dynamic bindings also passed three runs each: 15/15 additional executions.

OpenGL isolation reproduced the problem with one producer. A separate call to
`ShareContextOnThisThread()` failed with `Failed to create the shared WGL context`
before the first buffer was created or any graph submitted. The failure is in
shared WGL context initialization, not evidence of a GPU-fence retirement bug.
Capturing the native error returned `0xC00710DD` from
`wglCreateContextAttribsARB`. Matching the root pixel format alone did not fix
the failure. Detaching the sharing root and using a separate thread-local
context on the instance-creation thread did. The root now remains unbound after
initialization; producer and scheduler threads use their own shared contexts.
Shared-context failures preserve the native error code. This is a reproduced
driver behavior, not a claim that WGL universally forbids a current sharing root.

The final rebuilt MSVC Release binaries passed all ten stress workloads three
consecutive times (30/30 executions, 103.29 seconds): native completion and
resource-group workloads on all four backends, plus both WebGPU workloads with
forced external synchronization. Every completion run checked 4,128 GPU-copy
graphs and every resource-group run created 18,432 groups. No early completion,
duplicate delivery or readback mismatch was reported. An initial corrected
OpenGL run observed host available memory as low as approximately 3.1%; this
was natural host pressure, not controlled GPU-memory exhaustion.
All twelve existing OpenGL completion/draw regressions also passed on those
binaries (13.12 seconds), including layout switching, indexed layout switching,
float3, immediate constants and repeated constant updates.

Only synchronization has a new fallback here. Other optional Dawn capabilities
must not be described as emulated: the current BC decoder still requires native
BC support, and replacing depth precision, floating-point arithmetic or sampling
with a different representation is not an exact fallback by itself.
