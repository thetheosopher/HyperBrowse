---
name: HyperBrowse C++ guidance
description: C++ implementation rules for HyperBrowse source and smoke tests.
applyTo: "src/**/*.h,src/**/*.hpp,src/**/*.cpp,tests/**/*.h,tests/**/*.hpp,tests/**/*.cpp"
---

- Follow the nearest existing ownership, threading, and naming pattern before introducing a new abstraction.
- Keep blocking filesystem, decode, metadata, persistent-cache, and shell work off the UI thread.
- Preserve worker cancellation, stale-result checks, and destruction ordering.
- Use RAII for Win32, COM, GDI, Direct2D, and thread-owned resources where the surrounding code supports it.
- Invalidate render or thumbnail caches by file/content identity when item removal or replacement can reuse an index.
- Preserve the last valid viewer image across asynchronous cache misses unless the product behavior explicitly requires clearing it.
- Prefer existing path normalization, logging, settings, diagnostics, and message helpers.
- Add focused smoke coverage for externally visible behavior and failure paths.
- Avoid unrelated formatting changes and comments that merely restate code.
