# 0.2.0 — release preparation

Modern .NET 10 SafeHandle client and fixed-width C ABI, with a job-owned native
broker isolating shared allocator failures. Bounded correlated checksums use the
existing typed shared-memory transport. Added real poisoned-lock and owner-death
tests, resource failure injection, allocator exhaustion/recovery and callback
exception containment. Fixed kernel-handle rejection leaks and unchecked native
resource/wait failures. Restored all native upstream cases and added a historical
C++/CLI/NUnit compatibility CI lane. Pinned Boost 1.92 remains the modern runtime
dependency; compatibility tests use a separate original Boost 1.71 build.

Breaking: mapping token is now 1001. Both peers must use matching builds. C ABI
version is 1. The new managed API is the scoped checksum client, not a replacement
for every historical generic C++/CLI container/transport binding.

The earlier native foundation was a development milestone, not a stable release.
