---
name: cpp20-engineering
description: Implements and reviews native C++20 code for the editor, including ownership, concurrency, error handling, API design, performance and maintainability. Use for any C++ engine or application-core task.
---

# C++20 Engineering

## Standards
- C++20, modern RAII, value semantics by default, explicit ownership.
- Prefer `std::unique_ptr` for exclusive ownership and `std::shared_ptr` only for genuine shared lifetime.
- Use `std::span`, `std::string_view`, `std::expected` only if supported by the selected standard library; otherwise use a project Result type.
- Avoid raw owning pointers, global mutable state, hidden singletons and exceptions crossing module/plugin/FFI boundaries.
- Define thread ownership and lifetime in interface documentation.
- Use `const` correctness, narrow interfaces and explicit units.
- Avoid premature templates and over-generalized frameworks.

## Error handling
- Use typed error values for expected failures (I/O, decode, GPU, invalid project, unsupported codec).
- Exceptions may be used internally for exceptional failures if the project policy allows, but catch at subsystem boundaries.
- Never silently swallow errors.
- Include context in diagnostics without logging sensitive local file contents.

## Concurrency
- No blocking work on the Qt GUI thread.
- No blocking, allocation or unpredictable work on the real-time audio callback.
- Use cancellation-aware jobs for long-running import, proxy, decode and render tasks.
- Define shutdown order and join worker threads before destroying dependent resources.
- Avoid lock-order inversions; document locks and ownership.

## Code quality
- Small translation units and clear public headers.
- Avoid cyclic dependencies and include implementation details only in `.cpp`.
- Use clang-format and static analysis when configured.
- Add GoogleTest coverage for behavior and failure paths.
- Benchmark hot loops before optimizing; include reproducible benchmark inputs.

## Review checklist
Check lifetime, race conditions, overflow, invalid media, cancellation, shutdown, error propagation, ABI stability and platform assumptions.
