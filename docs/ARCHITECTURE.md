# Архитектура Simple

## Идея

Официальный Telegram Desktop рисует UI кастомными виджетами. Скринридеры видят мало. Мы добавляем:

1. **QAccessible**-обёртки (`TgSimpleA11y::Factory`)
2. **Прямую речь NVDA** (`nvdaControllerClient.dll`) для навигации стрелками
3. **Короткие фразы** (`CompactPhrase`) вместо длинных MSAA-имён

## Модули

| Файл | Назначение |
|------|------------|
| `telegram_simple_accessibility.cpp` | Install(), фабрика, лог |
| `telegram_simple_names.h` | Имена кнопок и панелей |
| `telegram_simple_keyboard.h` | F6, фильтр клавиш, NVDA Speak |
| `telegram_simple_speech.h` | CompactPhrase, подписи чатов |
| `telegram_simple_live.h` | «печатает», клавиатура бота |
| `apply_upstream_patches.py` | TopBar, BotKeyboard, HistoryInner |

## Сборка

```
checkout (этот репо)
  → clone tdesktop
  → cp accessibility/*
  → patch CMake + application.cpp
  → inline CI patches (HistoryInner, Dialogs, RpWidget, …)
  → apply_upstream_patches.py
  → ninja build
  → bundle nvdaControllerClient.dll
```

Упрощение на будущее: перенести оставшиеся inline-патчи из workflow в один Python-скрипт и оставить в CI только вызов скрипта.
