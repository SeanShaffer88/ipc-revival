# WR-001: Microsoft IPC native foundation

Development milestone, 2026-10-21. This fork is not a stable or Microsoft-supported release.
Upstream commit: `86df124c5f2a0447d6ec56af7a506dc41bbf5546`.
Microsoft's MIT license and original tree/history are retained.

## Selection evidence

The [upstream](https://github.com/microsoft/IPC) is archived. GitHub's direct-fork API returned 122 entries across two pages. The most recently pushed entry was [MIQ-sirrus7/ms_IPC](https://github.com/MIQ-sirrus7/ms_IPC), pushed July 2023; its default branch [compares identically with upstream](https://github.com/MIQ-sirrus7/ms_IPC/compare/microsoft:master...master). No currently maintained direct continuation was identified. This is not an exhaustive search of detached repositories or every fork branch; pushed dates alone are not proof of source changes.

| Candidate | Fit and decision |
|---|---|
| [Boost.Interprocess](https://github.com/boostorg/interprocess) | Active shared-memory primitives, relative pointers and allocators under BSL-1.0. Already foundational to this project. Direct use is preferable for a new small custom protocol; it does not preserve Microsoft's typed transport API by itself. |
| [iceoryx2](https://github.com/eclipse-iceoryx/iceoryx2) | Active larger messaging system. README lists Windows at tier 2 and C# bindings in a separate project, potentially behind main. Strong alternative for new cross-platform systems, but not a source/API-compatible continuation. Its Linux benchmark is not Windows evidence. |
| Win32 / .NET named pipes; [win-pipe revival](https://github.com/SeanShaffer88/win-pipe-revival) | Prefer for modest commands and broad interoperability. An included raw Win32 baseline measures the actual worker workload without adopting another transport dependency. |

Decision: restore the existing x64 native transport for a bounded blob-processing worker and test shared object ownership. Retaining allocator-backed objects across calls is the concrete benefit to investigate. No broad claim that shared memory is faster is justified; initial results show a modest advantage for larger reused blobs and close results for small requests. A real application's workload must decide adoption.

## First scope implemented

- C++17 CMake static core, original native calculator, worker and tests. MSVC x64 Debug/Release; VS 2022 and 2026 build discovery.
- Boost 1.92.0 header distribution pinned by archive SHA-256. Configure never downloads dependencies. Its BSL license is in the downloaded distribution. Native build no longer needs Boost.Thread binaries: a C++17 thread-local stack tracks nested scheduler callbacks.
- Compatibility fixes for allocator construction, removed Boost type traits and moved consume callbacks.
- Queue exception-safety repair: destroy a popped object even when its move or consumer throws, then return its slot. Regression checks verify lifetime counts, capacity and reuse.
- Scheduler drain repair: serialize the final count transition with the condition-variable wait to prevent a lost wake. Fifty rounds of 64 queued callbacks exercise shutdown.
- A native worker accepts immutable shared blobs of at most 256 KiB and returns correlated size/FNV-1a checksums. Shared buffers and their mappings remain alive until response/destruction. One client has one request outstanding. A separate Win32 pipe mode sends the same bytes and computes the same checksum.
- Sixteen separate-process checks cover five payload sizes, producer and server termination, normal/crash endpoint reuse, missing host, invalid bounds and response timeout. A watchdog fails hung children; it does not turn every library operation into a recoverable timeout.
- Original C++ calculator compiles. Its indefinite signal-based lifecycle and managed calculator are not modernized in this milestone.

## Benchmark contract

`tests/process_tests.ps1` writes measurements for each build configuration. One sequential client performs 100 round trips for each of 0, 1, 1024, 65536 and 262144 bytes. Shared memory reuses an immutable allocated blob; pipes copy each payload. Allocation, connection and initial filling are excluded. Both servers hash the full payload. Pipes include an extra drain acknowledgement. Results include checksum processing and scheduling, so they are descriptive end-to-end observations, not an isolated transport comparison, percentile latency, or equivalent protocol overhead. Debug numbers are correctness diagnostics.

## Dependencies and compatibility

The original native project used NuGet Boost 1.71 and toolset v142. The managed C++/CLI wrapper targets .NET Framework 4.6; it is not a modern .NET wrapper. Bond integration lives in a separate repository and is excluded. This milestone uses no Bond or managed dependencies. No transitive Boost DLL is needed for the native core after the TLS replacement; default MSVC CRT deployment requirements still apply to binaries.

Both peers must match architecture, Boost version, build configuration, compiler ABI and request/response layout. Do not exchange mappings with the historical NuGet build. The library's existing version token distinguishes Boost/configuration, but this is not a portable serialized wire protocol. Types need explicit version changes when layout changes.

## Review and remaining release gates

Agent-performed source review, not an independent audit. Resolved findings are listed above. Callbacks must contain exceptions; the worker handles a reply failing after client timeout. No callback may retain a bare mapped pointer beyond its owning connection/mapping.

Before a stable WR-001 release:

1. Restore and run the full original native test suite, including recursive async shutdown, allocation failures, invalid handles and wait failures. Review kernel-object ownership/error paths and callback data races more deeply.
2. Add a narrowly scoped C ABI plus SafeHandle-based modern .NET client, using an explicitly provisioned SDK. Run native/managed paired-process tests. Preserve old C++/CLI only if a concrete consumer requires it; do not claim compatibility without tests.
3. Add sustained saturation/multiple-client tests and crash injection during allocation/locking. Shared allocator spin locks are not robust owner-death locks; killing a process at an arbitrary instruction can poison a shared connection. Current termination fixtures do not prove arbitrary crash safety.
4. Validate access-control assumptions, version rejection and trusted-peer boundaries. A peer can corrupt shared pointers/allocator metadata; this is not a security boundary for mutually untrusted processes.
5. Repeat representative benchmarks, record CPU/OS/toolchain and distributions, review the actual consumer benefit, then package a reviewed scope with provenance, checksums and successful exact-commit GitHub CI.

No stable release or finished revival is claimed by this milestone. Development source and CI can be published while these gates remain open. Windows ARM64/x86, cross-account services, durable delivery and broad API modernization are outside the initial scope.

## 0.2.0 release preparation update

The native suite is restored (all 1,915 generated cases pass locally), with explicit modern Boost fixture sizing. A .NET 10 SafeHandle client now uses a C ABI and job-owned broker to bound host exposure to poisoned allocator locks. Fault tests cover six resource acquisitions, real allocation exhaustion, handle rejection/wait errors and callback containment. Managed tests include actual allocator-owner death and managed-owner job cleanup. Full Debug/Release validation and a separate original C++/CLI/NUnit CI lane gate stable publication. See REVIEW.md for the final scope, known raw-native crash limitations and release requirements. Earlier remaining-gate notes above describe the starting milestone; current check results and release status must be verified before publication.
