# 0.2.0 review and release scope

Agent-performed review, not an independent audit. The native implementation,
C ABI, broker lifetime, .NET wrapper, tests and release workflow were inspected.

## Fixes

- Popped queue objects are destroyed before their slot is restored when a move
  or consumer throws. Lifetime/capacity regressions exercise both paths.
- A scheduler's final count transition shares the condition-variable mutex;
  nested callback detection uses a thread-local stack rather than Boost.Thread.
- Historical C++/CLI callbacks use named local `auto_handle` guards instead of
  returning managed stack-semantics objects by value; current MSVC rejects the
  old return. Native headers use `use_count()` rather than deprecated `unique()`.
- Failed native event/work/wait/timer/pool acquisition throws before use. A
  duplicate named-event handle is closed when create-only verification rejects
  it. Invalid handles and failed waits throw, and negative/overflow finite waits
  are rejected. Thread-pool minimum setup is checked; zero maximum is rejected.
- Exceptions are contained at Windows callback boundaries. A throwing wait
  callback stops its waiter; timer/work exceptions emit debugger diagnostics.
  Applications should still report and contain their own callback failures.
- Bridge I/O captures Win32 errors immediately and drains cancelled overlapped
  I/O before releasing stack buffers/events. One budget covers each operation.
- The bridge authenticates its control-pipe peer by the process ID it created,
  uses a random pipe name, rejects remote pipe clients, inherits no handles, and
  assigns its kill-on-close job atomically at process creation using the Windows
  job-list startup attribute. A later assignment would leave a parent-death
  orphan window even with a suspended child; that window is closed.
- SafeHandle P/Invoke parameters pin native session lifetime during calls.
  A timed mutex serializes per-session requests, including concurrent managed
  callers; invalid sizes/timeouts are rejected before starting work.
- A peer crash can poison a native allocator lock. The bridge never accesses
  that allocator in the application process: deadline failure kills its broker
  and closes the session. An actual lock-owner-death fixture checks this path.

## Contracts and limits

Supported new surface: Windows x64 C ABI and .NET 10 client for the included
checksum worker. Managed byte arrays are copied into broker-owned shared memory;
this is not zero-copy from the managed heap. Reuse is inside the native session.
Timeout includes control I/O and native work; cleanup can add up to one second.
A timeout while waiting for another caller's session lock does not cancel that
other request. Once a request starts, transport failure poisons/closes its session;
callers must create a fresh session before retrying. No automatic replay.

`ipc_close` requires a valid handle with no concurrent raw C callers; the .NET
marshaller enforces its lifetime across Dispose. Opaque pointers are not a
security boundary. API clients must supply valid C buffers and handles.

The raw C++ API remains cooperative trusted-peer shared memory. It cannot repair
allocator metadata after owner death. Only the isolated bridge offers bounded
host-side failure containment. A server or its other native clients may need a
process restart after allocator corruption; do not reuse a tainted connection.
The tests prove their injected cases, not arbitrary instruction-level crash safety.

Default token-derived Windows access controls apply. Same-account trusted local
peers, matching architecture/compiler ABI/Boost/configuration and schema are
required. No cross-account authentication, durable/exactly-once semantics, ARM64
or x86 support is claimed. Revival memory token 1001 separates old 1000 mappings.
The pipe benchmark has extra drain acknowledgement overhead and is descriptive.

## Validation gates

1. All 1,915 generated upstream native cases in modern MSVC x64 Debug/Release,
   plus core/resource/native-process/managed-process suites. Connector fixtures
   use 256 KiB rather than historical 32 KiB because modern Boost queue metadata
   grew; their malformed-host/connection assertions are retained.
2. Separate historical solution/NUnit lane, using .NET Framework 4.8 compatibility
   targeting and the original Boost 1.71 NuGet dependencies. Its binaries are not
   mixed with the Boost 1.92 release runtime. This gate must succeed before release.
3. Real .NET SDK builds in GitHub CI. Runtime-only local validation may use VS
   Roslyn and installed .NET 10 runtime assemblies; that alone cannot open release.
4. Resource injection uses a separate instrumented native library. Production
   outputs do not contain fault hooks. Real shared allocator exhaustion/recovery,
   rejected-event handle counts, callback throws, actual poisoned-lock broker
   termination and managed-owner death are checked.
5. Both modern configurations and legacy lane must pass for the same commit.
   Publication verifies commit/source hashes and bundle hashes, creates a draft,
   verifies all uploaded asset digests, then publishes. `release.json` is an
   explicit final readiness gate; it stays false while checks/review are pending.

The new interop lifetime follows [Microsoft's SafeHandle guidance](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices).
