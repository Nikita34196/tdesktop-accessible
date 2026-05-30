# Публикация в отдельный репозиторий `tdesktop-accessible-simple`

Содержимое упрощённого гибрида лежит в ветке **`export/tdesktop-accessible-simple`** этого репозитория (только Simple, без файлов Full).

## Шаг 1 — создать пустой репозиторий на GitHub

1. https://github.com/new  
2. Имя: `tdesktop-accessible-simple`  
3. Без README / .gitignore (репозиторий пустой)

## Шаг 2 — запушить ветку как `main`

```bash
git clone https://github.com/Nikita34196/tdesktop-accessible.git
cd tdesktop-accessible
git fetch origin export/tdesktop-accessible-simple
git checkout export/tdesktop-accessible-simple

git remote add simple https://github.com/Nikita34196/tdesktop-accessible-simple.git
git push -u simple export/tdesktop-accessible-simple:main
```

## Шаг 3 — секреты CI

В **Settings → Secrets** нового репозитория добавьте те же `API_ID` и `API_HASH`, что и в полной версии (https://my.telegram.org).

## Шаг 4 — первая сборка

**Actions → Windows Accessible Build → Run workflow** на ветке `main`.

---

После публикации ссылку на Simple можно оставить в README полной версии:  
https://github.com/Nikita34196/tdesktop-accessible-simple
