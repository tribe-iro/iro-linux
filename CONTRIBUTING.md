# Contributing

> **v0.1 — pre-alpha.** Interfaces are unstable. Breaking changes happen without notice.

## Development flow

1. Open an issue describing the change.
2. Create a focused branch.
3. Keep changes small, testable, and documented.
4. Run formatting/lint/test checks locally.
5. Open a PR with rationale, design notes, and test evidence.

## Build and test

`iro-tool` is currently the primary build/test entrypoint:

```bash
cd iro-tool
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Commit quality

- Use descriptive commit messages.
- Keep behavior changes and refactors separate when possible.
- Add or update tests for non-trivial behavior changes.

## License and sign-off

By contributing, you agree your work is licensed as `GPL-2.0-only`.

Include a sign-off line in commits:

```text
Signed-off-by: Your Name <you@example.com>
```
