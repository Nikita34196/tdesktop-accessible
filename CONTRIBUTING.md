# Вклад в tdesktop-accessible-simple

## Тестирование

1. Скачайте сборку из [Releases](https://github.com/Nikita34196/tdesktop-accessible-simple/releases) или дождитесь зелёного CI на `main`.
2. NVDA + **Ctrl+Shift+T** (тестовая фраза).
3. При баге приложите `tg_simple_log.txt` с рабочего стола.

## Разработка

- Меняйте только `accessibility/` и `scripts/apply_upstream_patches.py`, если возможно.
- Новые фразы — в `telegram_simple_speech.h` / `docs/PHRASES.md`.
- Не дублируйте логику из `apply_upstream_patches.py` в workflow без необходимости.

## CI

Push в `main` или **workflow_dispatch**. Нужны секреты `API_ID` и `API_HASH`.
