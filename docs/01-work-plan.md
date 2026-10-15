# План работ — Arli

Подробный план этапов лежит в `/data/repos/locagent/arli-cpp-plan.md`. Здесь — статус дерева `/data/repos/arli`; актуальная версия задаётся файлом `VERSION`. Старые срезы M1–M3.4 в этой таблице больше не ведутся: их поведение вошло в этапы ниже.

| Этап | В дереве | Ещё нет |
|---|---|---|
| Э0 каркас | `VERSION`, CMake, `scripts/ci.sh`, `check-arli-law.sh`, `.clang-format` | — |
| Э1 zero-trust | local credentials, `ui_token` / `internal_token`, ключ модели только на запись, allowlist env воркера | — |
| Э2 очередь | FIFO, один ход на диалог, watchdog, cancel grace, HardStop, reconcile на старте | — |
| Э3 след | таблица `steps`, `audit/{task}/events.jsonl`, GetTurnTrace, AttachTurn, `arli.log` | — |
| Э4 ход модели | SSE, ask_user, loop-guard, профили LLM, бюджет контекста (summary, head/tail tool output) | `turn_token_budget` на ход (опционально) |
| Э5 инструменты | Tier A/B, registry, bwrap/shell-лексер/rlimit, read_only + UI настроек | архивы Tier B |
| Э6 режимы | list-only; file_pass; plan_*; memory; TurnEvent Plan/Memory; Get/Clear plan | — |
| Э7 оболочка | стол; file_pass/plan; стрим; Observation.diff; qmllint strict + dev-setup/clangd; ASan/UBSan/TSan gates; Gemma/Qwen/generic настройки | — |
| Э8 поставка | RPM/AppImage, doctor, `smoke-modes.sh`, `smoke-lifecycle.sh`, `docs/07-modes.md` | live smokes в CI не гоняем |
| Э9 Emperion | `AgentPage.qml`, `docs/09-emperion.md`, proto в `share/arli/proto`; реальный Emperion собирает `arli_embed`, создаёт `AgentFacade` и грузит страницу | обновлять закреплённый source archive при релизе образа |

Проверка сделанного: `bash scripts/ci.sh`. Живой стол: `bash scripts/run-dev.sh`.

## Доработка UI — 0.4.0

Адаптивные колонки и Drawer, обычное окно с сохранением геометрии, постоянная навигация, выделение сообщений и полных результатов, разделы настроек с единым сохранением, защищённый ввод и подтверждение HardStop. Добавлен offline CTest shell-ui; lint распространяется также на ArliStyle. RPM собирается из VERSION.

## Доработка UI — 0.5.0–0.5.2

Изменяемые панели, темы, масштаб текста и компактные отступы; отдельные блоки кода и цветной diff; поиск по загруженной переписке и результатам; копирование с уведомлением; большой просмотр результатов; сворачивание длинных ответов; карточки изменённых файлов с переходом к шагу; исправленные модальные клавиши и режим разбора. UI-тесты изолируют локальные настройки и проверяют компактное окно с текстом 120%. Канон поведения — `docs/04-shell.md`.

## Надёжность агента и Qwen — 0.5.5

Профиль `qwen3` задаёт native tool calling, последовательные вызовы инструментов и
рекомендуемый thinking sampling. Неопределённое «улучшим проект» сначала требует
конкретизации. Один ход ограничен 64 вызовами, 40 чтениями и 16 мутациями.
Qwen не получает legacy `edit_file`: локальные изменения идут через строгий
`replace_text`, согласованные правки нескольких файлов — через проверяемый
`batch_replace` с preflight и откатом. GetTurnTrace и настройки gRPC ограничены по
элементам/байтам. Очередь сохраняется при перезапуске демона; начатый ход не
воспроизводится автоматически, чтобы не повторить внешние эффекты.
