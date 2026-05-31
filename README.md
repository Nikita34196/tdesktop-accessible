# Telegram Desktop Accessible

Делаем официальный Telegram Desktop доступным для **NVDA**, **JAWS** и **Narrator**.

Готовые сборки — в разделе [Releases](../../releases). Скачал → запустил → работает.

---

## Для пользователей

### Скачать и запустить

1. Перейдите в [Releases](../../releases)
2. Скачайте `Telegram.exe` из последнего релиза
3. Запустите — установка не нужна (portable)
4. Включите NVDA

### Горячие клавиши

| Клавиша | Действие |
|---------|----------|
| **F6** | Переключение между панелями (чаты → сообщения → ввод → медиа/файлы → профиль, если открыты) |
| **Shift+F6** | Переключение в обратном порядке |
| **Ctrl+Tab** | То же, что F6 |
| **Escape** | Вернуться в список чатов (или «Список тем» в форуме) |
| **Стрелки** | Список чатов, сообщения в чате, **файлы / ссылки / фото** в боковой панели медиа |
| **Home** / **End** | В списке чатов и в списке медиа — к началу / концу |
| **Tab** / **Shift+Tab** | В сообщении: переход по ссылкам и вложениям в выбранном сообщении |
| **Enter** | Открыть вложение в чате или выбранный файл / фото / ссылку в списке медиа |
| **Ctrl+Shift+I** | Открыть / закрыть профиль чата |
| **Ctrl+Shift+F** | Открыть список **файлов** чата |
| **Ctrl+Shift+U** | Открыть список **ссылок** чата |
| **Ctrl+Shift+R** | Записать / отправить голосовое сообщение |
| **Ctrl+Shift+T** | Проверка озвучки NVDA (тестовая фраза) |
| **Ctrl+Shift+F6** | Диагностический дамп виджетов (для отчётов об ошибках) |

### Проверка с NVDA (чеклист)

После установки сборки с [Releases](../../releases):

1. Запустите NVDA, затем Telegram Accessible.
2. **Ctrl+Shift+T** — должна прозвучать фраза «Специальные возможности Telegram загружены».
3. **F6** три раза — озвучиваются «Список чатов», «Сообщения», «Поле ввода».
4. В списке чатов **стрелки вверх/вниз** — каждый чат озвучивается **один раз** (без дубля).
5. В открытом чате **стрелки** — сообщения с именем отправителя и кратким текстом/типом вложения.
6. **Tab** в сообщении со ссылкой — «Ссылка N из M» и описание.
7. **Enter** — открытие вложения или воспроизведение медиа.

Если что-то не озвучивается, приложите `tg_widgets_log.txt` и `tg_a11y_diag.txt` с рабочего стола — см. [CONTRIBUTING.md](CONTRIBUTING.md).

---

## Для разработчиков

### Как это работает

Telegram Desktop использует Qt, но рисует интерфейс кастомными виджетами в обход стандартных Qt-компонентов. Скринридеры «не видят» эти элементы.

Этот проект добавляет слой `QAccessibleInterface` поверх кастомных виджетов Telegram:
- **Фабрика** (`telegram_accessibility.cpp`) — перехватывает создание виджетов и назначает им accessibility-интерфейсы
- **Имена** (`telegram_accessibility_names.h`) — таблица человекочитаемых имён для всех кнопок и панелей
- **Клавиатура** (`telegram_accessibility_keyboard.h`) — навигация F6/Tab между панелями

### Структура

```
├── .github/workflows/
│   └── build-windows.yml        ← GitHub Actions: автосборка
├── accessibility/
│   ├── telegram_accessibility.h        ← Главный заголовок
│   ├── telegram_accessibility.cpp      ← Фабрика + реализация интерфейсов
│   ├── telegram_accessibility_names.h  ← Таблица имён элементов
│   └── telegram_accessibility_keyboard.h ← Навигация клавиатурой
└── README.md
```

### Как собирается

GitHub Actions автоматически:
1. Клонирует официальный `telegramdesktop/tdesktop`
2. Копирует наши файлы в `Telegram/SourceFiles/ui/accessibility/`
3. Патчит `CMakeLists.txt` (добавляет наши файлы в сборку)
4. Патчит `application.cpp` (вызывает `TgAccessibility::Install()` при старте)
5. Собирает всё на Windows с Visual Studio
6. Публикует `Telegram.exe` как артефакт / релиз

### Как настроить свой форк

#### 1. Форкните этот репозиторий

Нажмите **Fork** на GitHub.

#### 2. Получите API-ключи Telegram

1. Откройте https://my.telegram.org
2. Войдите по номеру телефона
3. Выберите «API development tools»
4. Создайте приложение (название — например, «Telegram Accessible»)
5. Запишите **api_id** и **api_hash**

> ⚠️ Это ключи *приложения*, а не ваши личные данные.
> Все пользователи вашей сборки будут использовать один и тот же бинарник.
> Ключи НЕ видны в публичном коде — они хранятся в GitHub Secrets.

#### 3. Добавьте секреты в GitHub

В вашем форке: **Settings → Secrets and variables → Actions → New repository secret**

| Имя | Значение |
|-----|----------|
| `API_ID` | Ваш api_id (число) |
| `API_HASH` | Ваш api_hash (строка) |

#### 4. Запустите сборку

- **Автоматически:** при push в main или dev
- **Вручную:** Actions → Build Windows Accessible → Run workflow

#### 5. Скачайте результат

После успешной сборки:
- **Actions → последний запуск → Artifacts → Telegram-Accessible-Win64**

Для публикации в Releases создайте тег:
```bash
git tag v1.0.0
git push origin v1.0.0
```

---

## Как помочь проекту

### Добавить имя элемента

Откройте `accessibility/telegram_accessibility_names.h`, добавьте строку в `rules[]`:

```cpp
{ "SendButton", "SendButton", nullptr, "Отправить" },
```

Имена также подставляются автоматически при старте (`ApplyNames` в `telegram_accessibility.cpp`).

### Добавить поддержку нового виджета

Откройте `accessibility/telegram_accessibility.cpp`, в функции `Factory` добавьте:

```cpp
if (classname.contains("MyNewWidget")) {
    return new ButtonAccessible(w);  // или другой тип
}
```

### Узнать имена виджетов

Добавьте в `Factory` временную строку:
```cpp
qDebug() << "TgAccess:" << classname << object->objectName();
```
Запустите Telegram из Visual Studio — имена классов появятся в Output.

Или используйте [Accessibility Insights for Windows](https://accessibilityinsights.io/).

---

## Связанные проекты

- [Eagalon/tdesktop-accessible](https://github.com/Eagalon/tdesktop-accessible) — форк с accessibility-патчами
- [zendalona/tdesktop](https://github.com/zendalona/tdesktop) — проект Zendalona
- [Issue #476](https://github.com/telegramdesktop/tdesktop/issues/476) — оригинальный запрос accessibility в tdesktop

## Лицензия

GPLv3 (как и Telegram Desktop)
