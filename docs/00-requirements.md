# Требования — Arli

Статус: исходники на `VERSION` (сейчас 0.3.19). Три процесса, loopback, оболочка Qt Quick.

## Задача

Локальный агент на одном хосте: окно, очередь ходов, модель и инструменты. Модель только советует и зовёт tools. Файлы и команды исполняет воркер внутри заданной папки.

## Продукт

| Часть | Бинарь | Обязанность |
|---|---|---|
| Оркестратор | `arli-daemon` | gRPC `127.0.0.1:4792`, очередь, lifecycle, relay, аудит. Код и LLM не исполняет. |
| Воркер | `arli-agent` | LLM, tools, `run_turn`. Сессия `AgentHub.Session`. |
| Оболочка | `arli-shell` | Qt 6 Quick. QML вызывает только `AgentFacade`. |

Слушатель — только loopback. `0.0.0.0`, `::` и `*` не принимаются.

## Контракт UI

Сервис `Agent` в `src/proto/arli.proto`: Health, диалоги (list/create/rename/delete/open), Send (stream), Cancel, HardStop, ConfirmDanger, AnswerAskUser, GetSettings/SetSettings, ListTasks, GetTurnTrace, AttachTurn.

Поверх local credentials — `ui_token`. Health без токена. Ключ модели по проводу только на запись.

## Контракт воркера

`AgentHub.Session` — двусторонний поток. Воркер шлёт Hello, затем события хода. Оркестратор отдаёт Claim, Cancel, ConfirmResult, Shutdown. Токен сессии — `internal_token`. Оболочка его не знает.

## Инструменты

В workdir: `list_dir`, `read_file`, `create_file`, `append_file`, `edit_file`, `rg_search`, `run_shell`.

`run_shell` — это `/bin/sh -c` с cwd = workdir. Команда, которая пишет файлы (`>`, `>>`, `tee`, `sed -i`, `cp`/`mv`), отклоняется до карточки подтверждения. Удаление (`rm`, `rmdir`, `unlink`, `find -delete`, `git clean`) проходит через карточку, пока включён тумблер «удаление только с разрешения». Остальные команды спрашивают подтверждение, кроме префиксов из «разрешать без вопроса» или пока включено «разрешить все команды». Список «всегда спросить» сильнее обоих. Долгий процесс (`start_background`, `&`, `nohup`) остаётся в столе и останавливается оттуда.

Запись файлов идёт через `edit_file` / `create_file` / `append_file`. Перед правкой создаётся проверяемая копия в приватном `state/backups/`, вне доступного модели дерева; пространство имён привязано к canonical workdir и относительному пути.

## Запрещено в дереве продукта

Kirigami, KF6, Node, Vite, WebEngine, системный браузер как оболочка, `Insecure*Credentials`, бинарники и AppImage внутри git. Пакеты собираются в `~/arli-dist`.

## Приёмка среза

1. `bash scripts/ci.sh` — сборка, ctest, закон дерева, qmllint, модули ≤ 300 строк.
2. `arli-daemon` слушает только loopback; `arli-probe --auth` проходит с `ui_token`.
3. `arli-shell` открывает стол: диалоги, лента, ход, настройки, страница роли.
4. Обрыв окна не отменяет активный ход. Повторное открытие подхватывает его через `AttachTurn`.
