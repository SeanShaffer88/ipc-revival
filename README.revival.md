# IPC revival 0.2.0 — release preparation

Independent MIT-licensed revival of Microsoft's Windows shared-memory IPC. The current release gate is in release.json; development snapshots are not stable releases. Read REVIEW.md for tested scope and limits, and REVIVAL.md for fork/alternative research.

The new supported interop surface is a Windows x64 C ABI and .NET 10 checksum client for a native blob worker. Each client owns a broker process; a stuck native allocator cannot hold the managed host indefinitely. The raw C++ transport still requires cooperative trusted peers and does not repair abandoned allocator metadata.

Build with Windows, MSVC x64 Build Tools/CMake, PowerShell 7, .NET 10 SDK and Boost 1.92:

```powershell
$boost = ./scripts/fetch-boost.ps1
./scripts/build.ps1 -BoostRoot $boost -Configuration Release
./scripts/build.ps1 -BoostRoot $boost -Configuration Debug
```

Configure performs no downloads. CI tests all 1,915 upstream native cases plus resource, native process and managed process suites in both configurations. A separate original solution/NUnit lane validates historical C++/CLI behavior using isolated Boost 1.71/.NET Framework 4.8 compatibility targets. Those old assemblies are not bundled with the new runtime. Runtime-only local machines may compile with installed VS Roslyn; publication additionally requires normal .NET SDK builds in CI.

```powershell
# Console one; enter a line to stop after clients finish:
./.build/x64/Release/ipc_worker.exe shm-server example
# Console two, native client:
./.build/x64/Release/ipc_worker.exe shm-client example 262144 100
# Or run the .NET integration sample:
dotnet ./.build/x64/Release/managed/Integration.dll run example
```

```csharp
using IpcRevival;
using var client = new ChecksumClient("example");
ChecksumResult result = client.Hash(new byte[] { 1, 2, 3 }, timeoutMilliseconds: 2000);
Console.WriteLine(result.Checksum);
```

Reference Managed/IpcRevival/IpcRevival.csproj, or the built IpcRevival.dll. Deploy ipc_bridge.dll and ipc_broker.exe beside the managed assembly. Requires .NET 10 and the MSVC x64 runtime; artifacts are framework-dependent, not self-contained. Payload limit: 256 KiB. One session serializes calls. The native C ABI header is Bridge/ipc_bridge.h.

A started operation's failure closes its session; reopen before retrying. Deadline cleanup can add up to one second. The broker lives in a kill-on-close job, so owner death reaps it. A poisoned native server may also require restart. No automatic retries, durable delivery or exactly-once execution are promised. The managed array is copied into shared memory; do not describe this bridge as zero-copy from .NET.

Both peers must match x64 ABI, compiler, Boost and Debug/Release configuration. Mapping token 1001 rejects historical 1000 mappings. Trusted local same-account IPC only; not an authentication boundary. ARM64/x86 and broad generic managed bindings are outside this release scope.

The release workflow packages only after modern and historical lanes pass the exact commit and release.json is ready. Source, x64 runtime, validation evidence and SHA-256 checksums are uploaded to a draft, digest-verified, then published. Preserve Microsoft's MIT LICENSE and Boost BSL-1.0 notice. Test fault hooks and the intentional allocator-crash fixture are never shipped as runtime tools.
