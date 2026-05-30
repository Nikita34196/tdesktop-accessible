# Telegram Desktop Accessible — Simple

Упрощённый **гибрид** поверх официального [Telegram Desktop](https://github.com/telegramdesktop/tdesktop): те же чаты, файлы, голосовые и боты, но озвучка короче и предсказуемее для NVDA, JAWS и Narrator.

Полная версия с расширенными патчами: [tdesktop-accessible](https://github.com/Nikita34196/tdesktop-accessible).

Готовые сборки — в [Releases](https://github.com/Nikita34196/tdesktop-accessible-simple/releases).

---

## Для пользователей

### Скачать

1. [Releases](https://github.com/Nikita34196/tdesktop-accessible-simple/releases) → последний релиз
2. `Telegram.exe` + `nvdaControllerClient.dll` (portable) или установщик
3. Запустите NVDA, затем Telegram

### Горячие клавиши

| Клавиша | Действие |
|---------|----------|
| **F6** / **Shift+F6** | Панели: чаты → сообщения → ввод |
| **Escape** | Список чатов |
| **Стрелки** | Чаты и сообщения (краткая озвучка) |
| **Tab** / **Enter** | Ссылки и вложения в сообщении |
| **Ctrl+Shift+R** | Голосовое сообщение |
| **Ctrl+Shift+B** | Клавиатура бота |
| **Ctrl+Shift+T** | Тест озвучки NVDA |
| **Home** / **End** | Первый / последний чат в списке |

### Быстрая проверка

1. **Ctrl+Shift+T** — «Режим простой озвучки Telegram загружен».
2. **F6** три раза — «Список чатов», «Сообщения», «Поле ввода».
3. Стрелки в чатах — одна короткая фраза на элемент без лишних повторов.

Лог: `%USERPROFILE%\Desktop\tg_simple_log.txt`

---

## Чем Simple отличается от полной версии

| | **Simple** (этот репозиторий) | **Full** |
|---|------------------------------|----------|
| Цель | Короткие фразы, меньше шума | Максимум функций и диагностики |
| Озвучка | `CompactPhrase()` — одна строка | Расширенные подписи, больше контекста |
| Патчи upstream | `scripts/apply_upstream_patches.py` | То же + доп. inline-патчи в CI |
| Лог | `tg_simple_log.txt` | `tg_widgets_log.txt` |

Оба проекта — **патч-слой** к tdesktop, не отдельный клиент с нуля.

---

## Для разработчиков

```
├── accessibility/          # TgSimpleA11y — фабрика Qt + NVDA
├── scripts/
│   └── apply_upstream_patches.py
├── .github/workflows/
│   └── build-windows.yml
└── docs/
    ├── ARCHITECTURE.md
    └── PHRASES.md
```

Сборка в GitHub Actions (~2–3 ч): клонируется tdesktop, копируются файлы, патчи, MSVC.

Секреты репозитория: `API_ID`, `API_HASH` с https://my.telegram.org

Подробнее: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Лицензия

Код доступности распространяется на тех же условиях, что и Telegram Desktop (GPL). Сборки — неофициальные; используйте на свой риск.
