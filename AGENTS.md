# Arli

Локальный агент кресла. Имя продукта: **Arli**. Корень репозитория: этот каталог.

## Граница

Три процесса:

- оболочка — окно Qt 6 Quick (`arli-shell`); QML вызывает только методы `QObject` (`AgentFacade`);
- оркестратор (`arli-daemon`) — gRPC на `127.0.0.1`, очередь задач, Claim, Session hub, аудит; код и LLM не исполняет;
- агент (`arli-agent`) — LLM, tools, `run_turn`; подключается к `AgentHub.Session`.

`run_shell` / удаление / извлечение архивов ждут подтверждения в UI (relay через оркестратор). Старый веб-Арли (`/home/owned/local-agent`) в продукт не входит — только справочник поведения. См. `docs/03-orchestrator-agent.md`.

## Сборка

CMake, Qt 6 Quick, protobuf, grpc++, libcurl, SQLite. Kirigami и KF6 не входят. Node, Vite и HTTP UI не входят.

## Законы кода

- Транспорт только `arli::auth` (local credentials + bearer). `Insecure*Credentials` запрещены.
- Секреты (`ui_token`, `internal_token`, `llm_api_key`) не попадают в UI-ответы, логи, аудит.
- Модуль ≤ 300 строк (`scripts/ci.sh` проверяет; legacy-исключения перечислены там и сокращаются).
- Протокол `arli.v1` только расширяется: новые поля/RPC, без переименований и смены семантики.
- Таблицы SQLite: `messages` пишет воркер; `tasks`, `steps`, `settings`, `file_pass`, `plans` — оркестратор.
- Версия — только файл `VERSION`; `kSlice` выводится из него.

## Канон

- Требования: `docs/00-requirements.md`
- Статус этапов: `docs/01-work-plan.md` (подробный план — `/data/repos/locagent/arli-cpp-plan.md`)
- Процессы и состояние: `docs/03-orchestrator-agent.md`
- Оболочка: `docs/04-shell.md`
- Токены и ключ модели: `docs/05-secrets.md`
- Сборка, законы, `.cursorignore`: `docs/07-development.md`
- Поставка: `docs/08-ops.md`
- Проверка: `bash scripts/ci.sh`

## Встраивание в Emperion

Э9: `qml/AgentPage.qml` + статическая `libarli_embed.a` (`AgentFacade`, `arli::embed::*`) + `src/proto/arli.proto`. Канон: `docs/09-emperion.md`, CMake: `docs/arli-embed.cmake`.
