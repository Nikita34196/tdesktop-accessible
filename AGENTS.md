# AGENTS.md

Guidance for AI agents working in this repository.

## Project

**Telegram Desktop Accessible** — C++/Qt accessibility patches for [telegramdesktop/tdesktop](https://github.com/telegramdesktop/tdesktop). This repo is a **patch layer** plus GitHub Actions; the full desktop app is built on **Windows** in CI, not as a Linux service.

| Path | Purpose |
|------|---------|
| `accessibility/` | `QAccessible` factory, names, keyboard navigation |
| `scripts/a11y_upstream_patches.py` | Extra patches applied to a cloned `tdesktop` tree |
| `.github/workflows/build-windows.yml` | Full Windows MSVC build and release |

There is **no** `package.json`, Docker dev stack, unit test suite, or dev server. Lint/build/run for the **runnable app** happen on Windows (CI or a Windows dev machine).

## Cursor Cloud specific instructions

### What works on Linux (Cloud Agent VM)

Use this loop to validate patch changes without MSVC:

1. **Python patch script** — `python3 -m py_compile scripts/a11y_upstream_patches.py`
2. **Workflow lint** — `actionlint .github/workflows/*.yml` (binary can live in repo root or on `PATH`); optional `yamllint -d relaxed .github/workflows/*.yml` (warnings for long lines are expected)
3. **Upstream integration test** — shallow clone `tdesktop`, copy `accessibility/*` into `Telegram/SourceFiles/ui/accessibility/`, run `TDESKTOP_ROOT=/workspace/tdesktop python3 scripts/a11y_upstream_patches.py`. Success prints lines like `top_bar_widget.h patched` and exits 0.

Keep a sibling clone at **`/workspace/tdesktop`** (not inside this repo’s git tree). Do **not** commit `tdesktop/`. Refresh when needed:

```bash
git clone --depth 1 https://github.com/telegramdesktop/tdesktop.git /workspace/tdesktop
```

Re-running the patch script on an already-patched tree may error; use a fresh clone or `git checkout --` on modified upstream files.

### What does **not** run on Linux Cloud

- **Compiling `Telegram.exe`** — requires Windows, Visual Studio, Ninja, `prepare.py` deps, and `API_ID` / `API_HASH` (GitHub Secrets in CI).
- **NVDA / GUI hello-world** — manual on Windows with a [Release](https://github.com/Nikita34196/tdesktop-accessible/releases) build; see README checklist (`Ctrl+Shift+T`, F6, arrows).

### CI build (authoritative)

Trigger: push to `main`/`dev`, tag `v*`, or **Actions → Windows Accessible Build → Run workflow**. Secrets: `API_ID`, `API_HASH`.

### Editing conventions

- Human-readable control names: `accessibility/telegram_accessibility_names.h`
- New widget types: `Factory` in `accessibility/telegram_accessibility.cpp`
- Upstream hook points that change with tdesktop releases: `scripts/a11y_upstream_patches.py` and inline steps in `build-windows.yml`
