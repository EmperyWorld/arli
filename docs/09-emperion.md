# Встраивание в Emperion (Э9)

Кресло Арли встраивается как **страница QML** + **тот же `AgentFacade`**, что и `arli-shell`. gRPC-контракт — `src/proto/arli.proto` (пакет `arli.v1`), копируется в дерево Emperion рядом с proto Defense. Демон слушает **отдельный** loopback-порт (по умолчанию `127.0.0.1:4792`), чтобы не пересекаться с другими агентами.

## Что положить в Emperion

| Артефакт | Откуда | Зачем |
|---|---|---|
| `arli.proto` | `src/proto/arli.proto` | codegen gRPC для UI (только `service Agent`, без AgentHub в оболочке) |
| QML | `/usr/share/arli/qml` или каталог из репо | `AgentPage.qml`, `Arli/`, `ArliStyle/`, остальные компоненты |
| **`libarli_embed.a`** | сборка `src/` (`ARLI_BUILD_SHELL=ON`) | `AgentFacade`, Desk, модели, gRPC UI; заголовки `include/arli/{embed,facade,models}.hpp` |
| **`arli-embed.cmake`** | `docs/arli-embed.cmake` → `share/arli/cmake` | `add_subdirectory` + `arli_link_embed(target)` |

Минимальная страница хоста:

```qml
import QtQuick

AgentPage {
    anchors.fill: parent
    desk: arliAgent   // QObject* AgentFacade, см. ниже
}
```

### CMake (рекомендуется)

```cmake
add_subdirectory(/path/to/arli/src arli-build EXCLUDE_FROM_ALL)
target_link_libraries(emperion-shell PRIVATE arli_embed)
# см. docs/arli-embed.cmake — function arli_link_embed(...)
```

После установки SDK используйте экспортируемые targets с транзитивными зависимостями:

```cmake
find_package(Arli CONFIG REQUIRED COMPONENTS client embed)
target_link_libraries(emperion-shell PRIVATE Arli::embed)
```

Для gRPC-клиента без Qt:

```cmake
find_package(Arli CONFIG REQUIRED COMPONENTS client)
target_link_libraries(my-client PRIVATE Arli::client)
```

Пакет находится в `<prefix>/<libdir>/cmake/Arli`; prefix можно передать через
CMAKE_PREFIX_PATH. Заголовки: `<arli/auth.hpp>`, `<arli/arli.grpc.pb.h>`,
`<arli/embed.hpp>`. SDK требует C++20, OpenSSL Crypto и development-пакеты grpc++/protobuf
(обнаруживаются pkg-config); компонент embed дополнительно требует Qt6 Core,
Gui, Qml, Quick, QuickControls2. Client не требует Qt, SQLite или libcurl.
Пакет экспортирует также Arli::common и Arli::proto. До версии 1.0 version check
использует ExactVersion; заголовки и библиотеки поставляются одной сборкой.

`cmake --install <build> --component SDK --prefix <prefix>` устанавливает только
библиотеки, публичные заголовки и CMake config; QML/runtime устанавливаются полной
поставкой отдельно. Для QML в нестандартном расположении задайте ARLI_QML_DIR.
Тест `scripts/check-installed-sdk.sh` устанавливает SDK во временный каталог,
переносит prefix, копирует отдельный consumer и собирает его без add_subdirectory.
Client вариант запрещает find_package(Qt6); embed вариант линкует AgentFacade.
Оба исполняют authenticated gRPC roundtrip и проверку неверного токена.
Кроме fixture RPC, client consumer запускает отдельные production daemon/worker
и deterministic mock LLM. Через публичный SDK он проверяет Health, Send, отключение
потока, AttachTurn с монотонным cursor, idempotent retry и Cancel running задачи с
проверкой terminal event и сохранённого статуса. В тесте нет arli_core, внутренних
заголовков или прямого доступа к БД. Health probe использует wait_for_ready и deadline.
Скрипту нужна сборка с ARLI_BUILD_TESTS=ON (включая arli-mock-llm).
Эта проверка включена в scripts/ci.sh; системная установка при ней не выполняется.

### C++ хоста

```cpp
#include <arli/embed.hpp>

AgentFacade arli;
QQmlApplicationEngine engine;
engine.rootContext()->setContextProperty(QStringLiteral("arliAgent"), &arli);
arli::embed::addQmlImportPath(engine);
engine.load(/* AgentPage.qml или ваша оболочка */);
```

Стиль: `QQuickStyle::setStyle("ArliStyle")` и fallback `Basic`, как в `arli-shell`.

## Переменные окружения

| Переменная | Кто читает | Смысл |
|---|---|---|
| `ARLI_LISTEN` | shell, probe | endpoint демона (`127.0.0.1:4792`) |
| `ARLI_STATE_DIR` | daemon, agent, shell | каталог state + `ui_token` |
| `ARLI_UI_TOKEN` | shell (опционально) | bearer для RPC |
| `ARLI_QML_DIR` | оболочка / embed | override QML root (должен содержать `Main.qml` или `Arli/qmldir`); иначе runtime: `…/share/arli/qml` рядом с бинарником → bake только если путь ещё существует → `/usr/share/arli/qml` |

Emperion **не** должен читать `ARLI_INTERNAL_TOKEN` (закон в `docs/07-development.md`).

## Контракт QML ↔ `AgentFacade`

Страница ожидает у `desk` всё, что используют `ChatStage`, `SettingsPage`, `Inspector`, `CommandPalette` и панели режимов:

- модели: `dialogs`, `messages`, `trace`, `tasks` (QAbstractListModel);
- RPC-обёртки: `refresh`, `openDialog`, `send`, `cancel`, `hardStop`, `saveSettings`, `confirmDanger`, `answerAsk`, `clearFilePass`, `clearPlan`, …;
- состояние: `busy`, `daemonUp`, `filePass`, `plan`, `streamChars`, `turnEditSummary`, настройки (Q_PROPERTY с NOTIFY).

Новые поля в proto добавляются **только расширением**; Emperion перегенерирует stub без переименований.

## Демон и поставка

На машине пользователя по-прежнему нужен `arli-daemon` (оркестратор поднимает `arli-agent`; рядом нужен `arli-job-supervisor`). Emperion не заменяет systemd user-unit: `systemctl --user start arli-daemon` или запуск из своего лаунчера с тем же бинарником.

Проверка перед релизом Emperion:

```bash
bash scripts/ci.sh
bash scripts/check-emperion-embed.sh
bash scripts/arli-doctor.sh
```

## Отличие от standalone `Main.qml`

`Main.qml` — только `ApplicationWindow` + палитра + полноэкранный режим. Весь стол — **`AgentPage.qml`**, его же вставляют в Emperion без второго окна.


## GrpcToolProvider: ядро адаптера прикладных сервисов

SDK экспортирует Arli::grpc_tools и `<arli/grpc_tool_provider.hpp>`. Доверенный
C++ host регистрирует GrpcToolMethod: tool_name в namespace grpc_, фиксированный
числовой loopback endpoint, полное protobuf имя метода, serialized FileDescriptorSet
с импортами, отдельный bearer token сервиса, mutating и deadline_ms (1..60000).
Регистрация выполняется до использования; registry нельзя менять во время вызовов.

Поддерживаются allowlisted unary RPC, до 32 методов; streaming отклоняется.
Параметры инструментов выводятся из protobuf descriptors, а JsonStringToMessage
проверяет аргументы перед approval и dispatch. Unknown fields, неправильные типы,
слишком большие payload и сложные schemas отвергаются. Обычные protobuf сообщения
и google.protobuf.Empty поддерживаются; остальные well-known JSON types требуют
отдельного адаптера. Схема ограничена глубиной 6 и 2048 узлами, payload — 512 КиБ.

Credentials не входят в параметры/схемы модели. Bearer token использует token68
символы; literal echo собственного credential в результате маскируется. Ошибки
транспорта не передают необработанный server error text. Сервер приложения обязан
проверять bearer; публичный Health Arli специально остаётся открытым.
Транспорт создаётся только arli::auth с local credentials. Модель не выбирает
endpoint, method path, credentials или metadata. Read-only блокирует mutating RPC.
Mutating вызывает approval callback с именем инструмента и canonical JSON; без
callback/подтверждения dispatch запрещён. Deadline/cancel после dispatch не доказывает
отсутствие эффекта; адаптер не повторяет вызов автоматически.

В run_turn подключён optional TurnControl.grpc_tools: схемы добавляются к tools,
approval идёт через ConfirmHub/обычное ConfirmNeeded, Action/Observation — через
обычный relay/audit path. Неподтверждённый внешний вызов не превращается в успешный
Done даже при оптимистичном ответе модели.

Standalone arli-agent загружает доверенный `state/grpc-tools/registry.json` перед
каждым Claim; формат, права, лимиты и правила ротации описаны в
`docs/03-orchestrator-agent.md`. Workspace файлы не являются источником полномочий
для регистрации RPC. Реальный daemon/worker path проверен с mock и живой Gemma
на isolated authenticated application fixture; конкретные сервисы Emperion
нужно зарегистрировать по их реальным descriptor sets и классификации методов.

Сгенерированный Arli::client также экспортирует WatchJobLog (server streaming).
Храните opaque next_position после применения каждого JobLogChunk и передавайте
его в after_position при повторном подключении. gap=true требует видимого
уведомления о недоступном фрагменте после ротации. Сессия ограничена 15 секундами,
поэтому CANCELLED без локальной отмены можно возобновить; terminal завершает
наблюдение. Отмена наблюдения не равна StopJob. Контракт: раздел «Поток job log»
в docs/03-orchestrator-agent.md. Это RPC сервиса Arli, не application unary tool
из grpc-tools registry; ограничение registry на unary методы сохраняется.
