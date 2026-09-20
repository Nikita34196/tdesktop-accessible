# Обновление базы Telegram Desktop (tdesktop)

Сборки используют **зафиксированный** тег upstream, а не произвольный `main`. Так вы получаете конкретную версию фич Telegram и предсказуемый CI.

## Где задана версия

В [`.github/workflows/build-windows.yml`](../.github/workflows/build-windows.yml), job `windows`:

```yaml
env:
  TDESKTOP_REF: v7.2.9
```

Актуальный стабильный релиз: [telegramdesktop/tdesktop releases](https://github.com/telegramdesktop/tdesktop/releases).

## Чеклист bump (поднять TDESKTOP_REF)

1. **Выбрать тег** — обычно последний `v7.x.y` с [Releases](https://github.com/telegramdesktop/tdesktop/releases), не сырой `main`, пока не проверили патчи.
2. **Изменить** `TDESKTOP_REF` в `build-windows.yml` (и в этом файле обновить пример, если нужно).
3. **Локально (Linux, без MSVC)** — свежий клон и прогон патчей:
   ```bash
   rm -rf /workspace/tdesktop
   git clone --depth 1 --branch v7.2.9 --recursive \
     https://github.com/telegramdesktop/tdesktop.git /workspace/tdesktop
   cp accessibility/* /workspace/tdesktop/Telegram/SourceFiles/ui/accessibility/
   TDESKTOP_ROOT=/workspace/tdesktop python3 scripts/a11y_upstream_patches.py
   python3 -m py_compile scripts/patch_*.py
   ```
   Ошибки `ERROR: landmark not found` — править соответствующий `scripts/patch_*.py` или шаг в workflow.
4. **PR** → дождаться зелёного **Windows Accessible Build** (полная сборка ~4–5 ч).
5. **Релиз** Accessible — тег `v*` как обычно; в release notes указать новый `TDESKTOP_REF`.
6. **Ручная проверка (Windows + NVDA)** — чеклист из [README](../README.md): Ctrl+Shift+T, F6, стрелки, Tab, Ctrl+Shift+F (файлы), контекстное меню.

## Проверка «будущего» upstream без смены пина

**Actions → Windows Accessible Build → Run workflow** → поле **tdesktop_ref**:

- `main` или `v7.3.0-beta` — эксперимент;
- пусто — используется `TDESKTOP_REF` из workflow.

Не меняйте пин в репозитории, пока экспериментальный run не зелёный.

## После bump — на что смотреть в логе CI

| Сообщение | Значение |
|-----------|----------|
| `already has upstream a11y` / `skipping` | Патч не нужен — хорошо |
| `upstream Qt jom retry already present` | `patch_prepare_qt_modules_inst.py` не трогает prepare.py |
| `ERROR: ... landmark not found` | Upstream поменял код — обновить патч |
| Падение на `prepare.py` / Qt | Смотреть [prepare.py](https://github.com/telegramdesktop/tdesktop/blob/dev/Telegram/build/prepare/prepare.py) и кэш Libraries |

Ключ кэша включает `TDESKTOP_REF`, поэтому после смены тега библиотеки пересобираются заново (дольше, но без смешивания версий).

## Связь с официальным a11y

При bump сверяйтесь с [#476](https://github.com/telegramdesktop/tdesktop/issues/476) и [CONTRIBUTING.md](../CONTRIBUTING.md) (раздел про upstream): часть наших inject-патчей можно удалять, если то же уже в tdesktop.
