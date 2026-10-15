# Поставка

Номер пакета берётся из `VERSION`. Скрипты пишут артефакты в `~/arli-dist`, не в репозиторий.

## RPM

```bash
bash scripts/build-rpm.sh
sudo dnf install ~/arli-dist/arli-$(tr -d '[:space:]' < VERSION)-1.*.rpm
```

Пакет ставит:

| Путь | Что |
|---|---|
| `/usr/bin/arli-daemon` | оркестратор, `127.0.0.1:4792` |
| `/usr/bin/arli-agent` | воркер, его поднимает демон |
| `/usr/bin/arli-shell` | окно Qt Quick |
| `/usr/bin/arli` | если Health молчит — `systemctl --user start`, затем оболочка |
| `/usr/bin/arli-doctor` | проверка окружения |
| `/usr/lib/systemd/user/arli-daemon.service` | `Restart=on-failure` |
| `/usr/share/arli/qml` | интерфейс |
| `/usr/lib64/libarli_embed.a` | статическая библиотека для Emperion (`AgentFacade`) |
| `/usr/include/arli/` | `embed.hpp`, `facade.hpp`, `models.hpp` |
| `/usr/share/arli/proto/arli.proto` | контракт UI ↔ демон |
| `/usr/share/applications/arli.desktop` | ярлык |

```bash
systemctl --user enable --now arli-daemon
arli-doctor
arli
```

После обновления пакета: `systemctl --user restart arli-daemon`, затем новый `arli`. Старый воркер подхватит сессию нового демона и будет мешать, если его не завершить рестартом.

## Qwen3.8 / llama.cpp

Для локального `Qwen3.8-27B-UD-Q4_K_M.gguf` выбирайте профиль `qwen3`, модель
`local-agent`, температуру `1.0`, окно Arli `49152`. Проверенная конфигурация
сервера использует контекст `65536`, один parallel slot, native Jinja,
`--reasoning-format deepseek`, effort `xhigh` и reasoning budget 2048.

На рабочей машине `/data/repos/llm/stack2-qwen.service` является единственным
supervisor контейнера. Установка и запуск:

```bash
install -Dm0644 /data/repos/llm/stack2-qwen.service ~/.config/systemd/user/stack2-qwen.service
systemctl --user daemon-reload
systemctl --user enable --now stack2-qwen.service
```

Compose restart policy при этом выключен, чтобы контейнер не имел двух владельцев.
Остановка передаёт один SIGTERM и ждёт до 120 секунд. Проверка реальной модели:

```bash
bash scripts/live-qwen-gate.sh
```

Gate выполняет исправление одного файла, согласованную правку двух файлов,
восстановление после ошибки инструмента и проверку точности байтов. JSON-отчёты
пишутся в `/tmp/arli-qwen-gate`.

Зависимости пакета: `grpc`, `protobuf`, `libcurl`, `sqlite`, `qt6-qtbase`, `qt6-qtdeclarative`, `ripgrep`, `bubblewrap`, `git`. `rg` нужен `rg_search`. `bwrap` пакет и `arli-doctor` требуют на PATH; `run_shell` запускает `/bin/sh -c` в workdir внутри bubblewrap с очищенным окружением и лимитами ресурсов.

Состояние и токены остаются в `$XDG_DATA_HOME/arli` пользователя.

На другой Fedora того же поколения glibc ставь RPM. AppImage — запасной путь и всё равно хочет с хоста `rg`, `git` и тот же glibc.

## AppImage

```bash
bash scripts/build-appimage.sh
~/arli-dist/Arli-<version>-x86_64.AppImage
```

Образ несёт Qt и библиотеки gRPC. `rg` и `git` берутся с хоста. Демон из образа не подменяет системный user-unit. Сборку гоняй без унаследованного `LD_LIBRARY_PATH` (`env -u LD_LIBRARY_PATH`): чужой `lib/` от прошлой сборки ломает оболочку.

Образ собирается без linuxdeploy: у бинарников Fedora RELR, и patchelf его портит.

## doctor

`arli-doctor` проверяет `rg`, `bwrap`, `git` и бинарники Арли, совпадение `--version`, права `0600` на токены и базу, Health демона. Недоступный LLM (`http://127.0.0.1:8080/v1/models`) — предупреждение, не ошибка. Успех печатает `DOCTOR_OK`.

## Smoke (регрессия)

| Скрипт | Что |
|---|---|
| `bash scripts/ci.sh` | офлайн: сборка, ctest, qmllint, лимит 300 строк |
| `bash scripts/smoke-modes.sh` | list-only, file_pass, plan, memory, edit_stats |
| `bash scripts/smoke-lifecycle.sh` | очередь, cancel, kill -9 воркера (mock LLM) |
| `bash scripts/smoke-live-llm.sh` | один ход против podman LLM |
| `bash scripts/smoke-long-tools.sh` | долгие инструменты на mock |

Режимы: [07-modes.md](07-modes.md).

## Ключ модели

Локальный llama.cpp ключа не требует. Чужой OpenAI-совместимый адрес — ключ в настройках оболочки. Подробности: [Ключи и токены](05-secrets.md).


Для фоновых команд рядом с arli-agent устанавливается arli-job-supervisor.
Отсутствие helper приводит к явному отказу запуска. Остановка фоновых заданий
требует Linux pidfd_open/pidfd_send_signal; небезопасного fallback на kill(PID) нет.

Завершённые фоновые задачи и их логи автоматически очищаются: последние 128
записей, максимум 7 дней после завершения. Активные процессы не затрагиваются.
Если в каталоге job есть посторонние файлы или путь не проходит проверку, очистка
его пропускает и пишет предупреждение с job ID; такие записи могут превышать
лимит. Каталоги рабочих проектов эта очистка не удаляет.

Архив для RPM создаётся из явного списка исходников продукта, tests/docs/templates
и метаданных VERSION/README/AGENTS. Developer state, cache и build directories
в него не копируются. Для отдельного каталога поставки задайте `ARLI_DIST`.
