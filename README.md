# Arli

**Лицензия:** [MIT](LICENSE). Код Arli открыт для использования, изменения
и распространения; сторонние компоненты сохраняют свои лицензии.

Локальный C++ агент: оркестратор + воркер на gRPC loopback + оболочка Qt 6 Quick. Канон как у Emperion — без веба и без Kirigami.

| Слой | Технология |
|------|------------|
| Оболочка | C++20, Qt 6 Quick (`arli-shell`) |
| Оркестратор | C++20, grpc++ (`arli-daemon`): очередь, lifecycle, relay, аудит |
| Воркер | C++20 (`arli-agent`): LLM, tools, `run_turn` |
| LLM | локальный OpenAI-compatible endpoint (llama.cpp) |
| State | SQLite в `$XDG_DATA_HOME/arli` (или `ARLI_STATE_DIR`) |

- [Требования](docs/00-requirements.md)
- [План работ](docs/01-work-plan.md)
- [Gemma 4 chat template](docs/02-gemma4-chat-template.md)
- [Оркестратор и воркер](docs/03-orchestrator-agent.md)
- [Оболочка](docs/04-shell.md)
- [Ключи и токены](docs/05-secrets.md)
- [Разработка и индекс Cursor](docs/07-development.md)
- [Поставка](docs/08-ops.md)
- [Emperion (встраивание)](docs/09-emperion.md)
- [Правила для агента](AGENTS.md)

## Сборка и запуск

Зависимости сборки (Fedora): `cmake ninja-build gcc-c++ grpc-devel grpc-plugins protobuf-devel openssl-devel libcurl-devel sqlite-devel qt6-qtbase-devel qt6-qtdeclarative-devel`.

Зависимости времени выполнения: `ripgrep` (`rg_search`), `git`. Пакет и `arli-doctor` также ждут `bubblewrap` на `PATH`. `run_shell` запускает `/bin/sh -c` в workdir и отклоняет команды, которые пишут файлы. Проверка: `arli-doctor`.

```bash
bash scripts/ci.sh                # offline: build + ctest + закон дерева + qmllint
bash scripts/run-dev.sh           # daemon (сам поднимает agent) + окно shell
# без GUI
cmake -S src -B build -G Ninja && cmake --build build
./build/arli-daemon 127.0.0.1:4792 &
./build/arli-probe 127.0.0.1:4792 --auth
./build/arli-smoke 127.0.0.1:4792
```

Слушатель только `127.0.0.1:4792`. Версия — файл `VERSION` (из него генерируется `version.hpp`: `kVersion`, `kSlice = M<minor>.<patch>`).

## Безопасность (zero-trust loopback)

Транспорт — gRPC local credentials (`LOCAL_TCP`): соединение принимается только с loopback. Поверх — два секрета в `state_root()` (mode `0600`, генерирует демон при старте):

| Файл / env | Кто → кому | Назначение |
|---|---|---|
| `ui_token` / `ARLI_UI_TOKEN` | shell, probe, smoke → `service Agent` | все RPC кроме `Health` |
| `internal_token` / `ARLI_INTERNAL_TOKEN` | `arli-agent` → `AgentHub.Session` | открыть Session и получать Claim |

Метаданные `authorization: Bearer <token>`, сравнение constant-time, отказ `UNAUTHENTICATED`. Воркер получает `ARLI_INTERNAL_TOKEN` через env с allowlist (`PATH HOME LANG XDG_* ARLI_*`), не через argv и не полное окружение. `llm_api_key` по проводу только на запись (`Settings.has_api_key` / `clear_api_key`), в `GetSettings` не возвращается.

`InsecureServerCredentials` / `InsecureChannelCredentials` в коде запрещены законом дерева (`scripts/check-arli-law.sh`).

## Жизненный цикл хода

`Send` ставит задачу в очередь (`queued`), диспетчер отдаёт её воркеру по FIFO между диалогами (один активный ход на диалог, второй `Send` в тот же диалог → `FAILED_PRECONDITION`; ожидающий получает `Queued{position}`). Статусы: `queued → running → done | failed | cancelled`, переходы — CAS в SQLite, терминальный статус не перезаписывается.

Watchdog в демоне (каждые 250 мс): `waitpid` без зомби, respawn воркера с backoff 0.5 → 5 с, `Hello` не пришёл за 5 с → kill; обрыв Session → `running` задачи `failed(worker_lost)`, очередь ждёт нового воркера. Таймауты в настройках: `idle_timeout_sec` (нет событий, по умолчанию 600), `turn_timeout_sec` (2700), `cancel_grace_sec` (10): `Cancel` идемпотентен и кооперативен (воркер прерывает LLM-запрос), по истечении grace воркер убивается (`SIGTERM` → 3 с → `SIGKILL`) и перезапускается. `HardStop` — немедленно. На старте демона все `queued/running` → `failed(reconcile_startup)`. `SIGTERM/SIGINT` демону → `Shutdown` воркеру, завершение в пределах 5 с. Обрыв стрима оболочки ход не отменяет.

Тест `ctest -R lifecycle` поднимает демон + воркер + `arli-mock-llm` (тестовый OpenAI-совместимый сервер, сценарии по имени модели: `echo`, `slow:<ms>`, `hang`, `tool_read`, `loop`, `fail`) и проверяет `kill -9` воркера, FIFO, cancel, hard stop, idle timeout, reconcile, graceful shutdown.

## Trace и аудит

Каждое событие хода пишется оркестратором в точке relay: таблица `steps` (полный текст до 64 КБ) и `state_root()/audit/{task_id}/events.jsonl` (output обрезан до 4 КБ, без секретов). `task.json` рядом — мета хода, без API-ключа. Каталог `audit/` лежит вне workdir: инструменты его не видят и не меняют.

`OpenDialog.last_trace` — шаги последнего хода. `GetTurnTrace(dialog_id, turn_id)` — любой прошлый ход (`turn_id = 0` значит последний). `AttachTurn(dialog_id)` сначала отдаёт уже записанные шаги активного хода, затем живые события до `Done`. Оболочка вызывает его при открытии диалога, если `ListTasks` показывает `queued` или `running`, поэтому перезапуск окна не теряет ход. Обрыв стрима оболочки ход не отменяет.

Лог демона и воркера — одна строка `ts level component key=value` в stderr и в `state_root()/arli.log` (ротация на 5 МБ). Уровень: `ARLI_LOG=debug|info|warn|error`.

Тест `ctest -R audit` проверяет trace, AttachTurn, обрыв UI и отсутствие секрета в `audit/`.

## Состояние

`ARLI_STATE_DIR` (или `$XDG_DATA_HOME/arli`): `arli.db`, `ui_token`, `internal_token`, `workspace/` (workdir по умолчанию), `audit/{task_id}/`, `arli.log`. Файлы меняются только инструментами внутри workdir; бэкапы в `workdir/.arli-bak/`.

## Установка

Пакет Fedora и AppImage собираются вне дерева (`~/arli-dist`), в репозиторий бинарники не кладутся.

Номер берётся из `VERSION`. Артефакты — в `~/arli-dist`, не в git.

```bash
bash scripts/build-rpm.sh
sudo dnf install ~/arli-dist/arli-$(tr -d '[:space:]' < VERSION)-1.*.rpm
systemctl --user enable --now arli-daemon
arli-doctor
arli          # поднимет демон, если он не слушает, и откроет оболочку
```

```bash
bash scripts/build-appimage.sh
# ~/arli-dist/Arli-<VERSION>-x86_64.AppImage
```

Юнит `arli-daemon.service` — systemd `--user`, `Restart=on-failure`. Состояние и токены остаются в `$XDG_DATA_HOME/arli` пользователя, не в пакете.
