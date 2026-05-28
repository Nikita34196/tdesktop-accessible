## Cursor Cloud specific instructions

- This repository is an accessibility patch layer for Telegram Desktop, not a standalone Linux-runnable app or service. Standard project context is in `README.md`; the actual product build is the Windows workflow in `.github/workflows/build-windows.yml`.
- There are no in-repo package dependencies to refresh in Cursor Cloud. The VM startup update script is intentionally a no-op (`true`) unless future commits add a real package manifest or setup script.
- Full end-to-end validation requires a Windows desktop environment, the built `Telegram.exe`, a Telegram account, and at least one Windows screen reader such as NVDA, JAWS, or Narrator. Cursor Cloud's Linux VM can inspect and validate repository files, but it cannot run the Windows app or verify screen-reader behavior locally.
- Use GitHub Actions/release artifacts for build/run evidence. A successful release payload should include `Telegram.exe`, `nvdaControllerClient.dll`, and `Telegram-Accessible-Setup.exe`.
