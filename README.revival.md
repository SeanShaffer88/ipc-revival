# Microsoft IPC revival — development

Focused restoration of Microsoft's MIT-licensed Windows shared-memory transport. See [REVIVAL.md](REVIVAL.md) for fork/alternative research, changes, ownership rules and remaining stable-release gates. This is an independent development fork.

Requires Windows, MSVC x64 Build Tools with CMake, PowerShell 7 and Boost 1.92.0.

```powershell
$boost = ./scripts/fetch-boost.ps1
./scripts/build.ps1 -BoostRoot $boost -Configuration Release
./scripts/build.ps1 -BoostRoot $boost -Configuration Debug
```

Or provide an existing verified Boost distribution via `-BoostRoot`. CMake configure performs no downloads. Both peers must use matching ABI, Boost and build configuration.

```powershell
# In one console; enter a line to shut down after the client finishes:
./.build/x64/Release/ipc_worker.exe shm-server example
# In another:
./.build/x64/Release/ipc_worker.exe shm-client example 262144 100
```

The worker retains one immutable shared buffer across correlated checksum requests. Use it only among trusted local processes. The process tests also run a raw named-pipe baseline, termination/timeout/restart fixtures and bound checks. The original native calculator is built as `ipc_calc.exe`; the historic managed wrapper remains unvalidated.

No stable release is available yet. GitHub Actions validates the native development milestone in Debug and Release. Preserve LICENSE when redistributing; Boost is separately licensed under BSL-1.0 and is fetched rather than vendored.
