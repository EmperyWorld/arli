# Разработка

## Сборка

Зависимости Fedora: `cmake ninja-build gcc-c++ grpc-devel grpc-plugins protobuf-devel openssl-devel libcurl-devel sqlite-devel qt6-qtbase-devel qt6-qtdeclarative-devel`.

```bash
bash scripts/ci.sh          # configure, build, ctest, закон дерева, qmllint, размер модулей
bash scripts/dev-setup.sh   # build-dev + compile_commands.json в корне (clangd)
bash scripts/qmllint.sh     # только QML, --max-warnings 0
bash scripts/run-dev.sh     # пересборка, свой state dir, демон и окно
```

### IDE (Cursor / VS Code)

- **C++:** расширение Microsoft C/C++ с битыми `cpptools` не нужно — в `.vscode/settings.json` отключён IntelliSense, включён **clangd** (`dnf install clang-tools-extra`). После `dev-setup.sh` в корне лежит симлинк `compile_commands.json`.
- **QML:** `scripts/qmllint.sh` в CI; делегаты с `pragma ComponentBehavior: Bound` и `required property`.
- **arli-shell:** `Main.qml` получает `shellAgent` через `QQmlApplicationEngine::setInitialProperties` (не context property).

`run-dev.sh` освобождает порт и завершает предыдущий `arli-agent`, иначе старый воркер занимает Session и новый демон крутит переподключение.

Версия — только файл `VERSION` (`MAJOR.MINOR.PATCH`). Из него генерируется `version.hpp`.

## Законы

`scripts/check-arli-law.sh` и `scripts/ci.sh`:

- в дереве нет `.so` и AppImage (каталоги `build`, `build-*`, `out`, `third_party` не сканируются на бинарники);
- нет `package.json`, Kirigami, WebEngine, bind на `0.0.0.0`, открытия системного браузера;
- нет `InsecureServerCredentials` / `InsecureChannelCredentials`;
- оболочка не читает `ARLI_INTERNAL_TOKEN`;
- исходник в `src/` и `tests/` не длиннее 300 строк. Исключения перечислены в `scripts/ci.sh` (`tools.cpp`, `turn.cpp`, `facade.cpp`) и со временем убираются.

QML не называет тип `Palette`: это имя занято `QtQuick.Palette`. Палитра команд — `CommandPalette.qml`.

## Что Cursor не индексирует

Два файла в корне, синтаксис как у gitignore. Оба коммитятся.

| Файл | Эффект |
|---|---|
| `.cursorignore` | Модель не читает совпадения: секреты, базы, журналы, `ui_token`/`internal_token`, пакеты, `.so`, черновик `.cursor/scratchpad.md`. |
| `.cursorindexingignore` | Поиск по кодовой базе пропускает `build/`, `build-*`, `out/`, `.cache/`, `compile_commands.json` и объектники. Файл из этих каталогов можно открыть явно, если он нужен для разбора сборки. |

Пакеты и AppImage в git не кладут. Сборка пишет их в `~/arli-dist`. Заголовок nlohmann в `third_party/` остаётся в индексе: это исходник, который включает код.

## Проверка надёжности агента

`docs/10-agent-reliability.md` описывает изменения цикла и инструменты проверки.
Offline CI покрывает сжатие, восстановление после ошибки аргументов, запрет правок,
очередь зависимостей и миграцию SQLite. `arli-live-eval` отдельно проверяет реальную
модель в временных рабочих папках; по умолчанию CI его не запускает.

Перед релизом изменения потоков/gRPC дополнительно проверяются санитайзерами:

```bash
bash scripts/ci-sanitizers.sh asan   # AddressSanitizer + UBSan
bash scripts/ci-sanitizers.sh tsan   # ThreadSanitizer
```

TSan выполняет 70 тестов. `shell-ui` исключён только из TSan: системный Qt/QML
создаёт внутренний поток способом, при котором runtime Clang TSan аварийно
останавливается до проверки кода Arli. Этот тест остаётся в обычном CI и ASan.
Срабатывания внутри неинструментированных системных Abseil/gRPC подавляются
точечно файлом `scripts/tsan.supp`; стеки Arli продолжают проверяться.

Сборки изолированы в `build-asan` и `build-tsan`; оба режима взаимоисключающие.
