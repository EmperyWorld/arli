# Аудит реализации Arli и gRPC

Дата: 2026-10-10. Версия: 0.5.2, текущее рабочее дерево, включая незакоммиченные изменения и предыдущие исправления файловых инструментов.

Arli уже является рабочим локальным агентом с разделением на shell, daemon и worker. Однако считать её надёжно законченной для длительной автономной работы и интеграции нескольких gRPC-клиентов пока нельзя. Приоритет — границы исполнения, целостность состояния и корректный lifecycle, затем устойчивый RPC-контракт и расширение инструментов.

## Что проверено

Прочитаны основные пути исполнения агента, transport/auth, protobuf, Session/Claim, очередь/watchdog, SQLite, tools, shell/background jobs, контекст/память, verification, Qt RPC-клиент, упаковка и тесты. Это аудит кода и выборочные воспроизведения, а не доказательство отсутствия остальных дефектов.

`bash scripts/ci.sh`: 19/19, arli-law, qmllint и module-size прошли. Отдельный [probe](audit-2026-10-10/probe.cpp) использовал временные каталоги, синтетический маркер и реальный gRPC daemon/worker с детерминированным mock LLM. [Результаты](audit-2026-10-10/probe-results.json). Рабочий экземпляр пользователя не менялся.

Ранее в этом чате реальная Gemma 4 26B прошла 5/5 файловых сценариев: [отчёт](file-integrity-eval-results.json). В этом полном аудите модель заново не оценивалась; тестировались инфраструктурные отказы. Slow-consumer нагрузка, TSAN, сетевой fault-injection, crash-durability и запуск внутри самого Emperion не выполнены. В каждом замечании различены воспроизведение и вывод по коду.

## Что уже реализовано

- Три процесса; QML работает через QObject, daemon передаёт задания worker через двусторонний gRPC Session.
- Loopback local credentials, bearer с разделением UI/internal, write-only поле ключа модели, запрет второго Session.
- FIFO, один активный ход на диалог, watchdog, cooperative Cancel, HardStop, перезапуск worker, graceful shutdown.
- Send и AttachTurn, запись steps/audit, восстановление части следа, планы, очередь файлов, память.
- LLM SSE и обработка вызовов tools, ограничение шагов, восстановление аргументов, read-before-edit и syntax gate.
- Qt Quick shell, proto для внешних клиентов, библиотека embed, RPM/AppImage scripts, 19 тестов и live-eval harness.

## Приоритеты и замечания

P1 — исправить до объявления надёжной автономной работы/интеграционного релиза. P2 — следующие обязательные улучшения устойчивости и эксплуатации. Это приоритет реализации, не CVSS-оценка.

### A01 · P1 · Shell наследует internal_token

**Основание:** Воспроизведено с синтетическим маркером; реальные токены не выводились.

ARLI_INTERNAL_TOKEN передаётся воркеру, а execv/execl наследуют его окружение. Даже sandboxed printenv получает маркер. Результат команды может затем попасть в модель, UI, steps и лог. Общего удаления секретов из этих потоков нет.

**Код:** [src/daemon/spawn_agent.cpp:45](/data/repos/arli/src/daemon/spawn_agent.cpp:45), [src/daemon/tools/shell_run.cpp:58](/data/repos/arli/src/daemon/tools/shell_run.cpp:58), [src/daemon/jobs.cpp:64](/data/repos/arli/src/daemon/jobs.cpp:64).

**Доработка:** Отдельный allowlist env для всех инструментальных процессов; execve с чистым env, закрытие лишних FD, недоступность state. Удаление секретов из результатов до UI/LLM/хранилища как дополнительная защита.

**Приёмка:** Маркер отсутствует в env foreground/background, stdout, observation, steps, audit и log; проверять также пути ошибок.

### A02 · P1 · Фоновые команды обходят sandbox

**Основание:** Воспроизведена запись в тестовый файл вне workdir при настройках по умолчанию.

start_background запускает /bin/sh напрямую. ShellRunOptions с shell_net, allow_unsandboxed и timeout не применяются, rlimit обычного runner также отсутствует.

**Код:** [src/daemon/jobs.cpp:21](/data/repos/arli/src/daemon/jobs.cpp:21), [src/daemon/jobs.cpp:108](/data/repos/arli/src/daemon/jobs.cpp:108).

**Доработка:** Единый ProcessRunner для foreground/background: sandbox, env, сеть, лимиты, группы процессов и отмена.

**Приёмка:** Одинаковая политика доступа и сети для обоих режимов, включая отсутствие bwrap; выход за workdir запрещён.

### A03 · P1 · read_only не обеспечивает запрет записи

**Основание:** Воспроизведено: лексер разрешает Python-команду, и обычный runner создаёт файл.

Python, sh-обёртки и проектные скрипты могут писать без распознаваемых редиректов. Workdir монтируется writable. HTTP POST/DELETE также не классифицируются как изменение.

**Код:** [src/daemon/turn.cpp:598](/data/repos/arli/src/daemon/turn.cpp:598), [src/daemon/tools/shell_run.cpp:117](/data/repos/arli/src/daemon/tools/shell_run.cpp:117), [src/daemon/tools/registry.cpp:123](/data/repos/arli/src/daemon/tools/registry.cpp:123).

**Доработка:** Явные capabilities инструментов и режимов; read-only mount для запрещённых записей; политика сетевых изменений; лексер оставить подсказкой, а не границей безопасности.

**Приёмка:** В read_only попытки записи через интерпретатор, build hook, shell wrapper и HTTP mutation отклоняются; чтение работает.

### A04 · P1 · Ошибка хода превращается в done

**Основание:** Воспроизведено через настоящий Agent.Send + mock syntax_stuck: Error «Проверка не прошла», tasks.status=done.

Done обозначает конец потока и одновременно трактуется как успех задачи. Аналогично лимиты и некоторые ошибки инструмента могут выглядеть как успешное завершение в ListTasks.

**Код:** [src/daemon/turn.cpp:765](/data/repos/arli/src/daemon/turn.cpp:765), [src/daemon/turn.cpp:834](/data/repos/arli/src/daemon/turn.cpp:834), [src/daemon/hub_control.cpp:79](/data/repos/arli/src/daemon/hub_control.cpp:79).

**Доработка:** Добавить outcome/error_code/retryable к терминальному результату, сохранив существующие поля proto. Явно различать success, failed, cancelled, incomplete, blocked; обработчик завершения один.

**Приёмка:** Ошибка LLM, исчерпание шагов, syntax_stuck, отказ пользователя и успешный ход дают согласованные UI/DB/audit/RPC результаты.

### A05 · P1 · Второй демон портит состояние первого

**Основание:** Воспроизведено: второй процесс вышел с кодом 1, но running задача первого стала failed(reconcile_startup).

Reconcile выполняется до успешного bind и до получения эксклюзивного владения каталогом состояния. Один state может также использоваться демонами на разных портах.

**Код:** [src/daemon/server.cpp:93](/data/repos/arli/src/daemon/server.cpp:93), [src/daemon/server.cpp:119](/data/repos/arli/src/daemon/server.cpp:119).

**Доработка:** Эксклюзивный lock state directory до миграций и reconcile; только владелец изменяет lifecycle. Отказ второго экземпляра должен быть без побочных эффектов.

**Приёмка:** Два запуска на одном порту и на разных портах с одним state: второй отклонён, задачи первого не изменены.

### A06 · P1 · Длинные диалоги теряют свежую историю

**Основание:** Воспроизведено: из 405 сообщений возвращены 1–400, а не 6–405.

ORDER BY id ASC LIMIT загружает начало диалога. UI ограничен первыми 200, агент первыми 400. Текущий запрос закреплён в system, но последние ответы и уточнения пропадают.

**Код:** [src/daemon/db_messages.cpp:9](/data/repos/arli/src/daemon/db_messages.cpp:9), [src/daemon/turn.cpp:64](/data/repos/arli/src/daemon/turn.cpp:64), [src/daemon/turn.cpp:88](/data/repos/arli/src/daemon/turn.cpp:88), [src/daemon/db.hpp:144](/data/repos/arli/src/daemon/db.hpp:144).

**Доработка:** Запрос последних N с последующим восстановлением хронологического порядка; курсорная пагинация UI; стабильный summary между ходами.

**Приёмка:** После 500 сообщений UI показывает последние и умеет загрузить старые; агент видит последнее уточнение и правильный предыдущий ответ.

### A07 · P1 · Память теряет записи и стабильные ID

**Основание:** Воспроизведено: 30 фактов → remember → 21 факт.

В MemoryState загружаются только 20 фактов для prompt, а replace_memory удаляет всю таблицу и создаёт её заново. Остальные факты теряются; ID также меняются после сохранения.

**Код:** [src/daemon/memory.cpp:106](/data/repos/arli/src/daemon/memory.cpp:106), [src/daemon/db_memory.cpp:26](/data/repos/arli/src/daemon/db_memory.cpp:26), [src/daemon/turn.cpp:82](/data/repos/arli/src/daemon/turn.cpp:82).

**Доработка:** Разделить полное хранилище и выборку для prompt; delta-операции add/update/delete по стабильному ID в транзакции; scope workspace/user, подтверждение сохранения.

**Приёмка:** 200 фактов переживают несколько ходов; remember/forget меняют только нужные записи, ID остальных не меняются.

### A08 · P1 · Медленный UI блокирует обработку событий

**Основание:** Подтверждено путём исполнения; нагрузочный сценарий slow-reader отдельно не запускался.

Deferred callbacks вызываются синхронно; sink делает blocking ServerWriter::Write. Это происходит на пути обработки worker frame, а иногда watchdog/dispatch. Медленный подписчик способен остановить обработку worker events и задержать других подписчиков.

**Код:** [src/daemon/hub_waiters.cpp:10](/data/repos/arli/src/daemon/hub_waiters.cpp:10), [src/daemon/hub.cpp:18](/data/repos/arli/src/daemon/hub.cpp:18), [src/daemon/agent_service.cpp:189](/data/repos/arli/src/daemon/agent_service.cpp:189).

**Доработка:** Ограниченные очереди на подписчика и отдельная доставка, coalescing Token, отключение slow consumer с возможностью replay. Async/callback gRPC можно внедрять постепенно.

**Приёмка:** Один клиент не читает поток: агент, watchdog и второй клиент продолжают работу; RAM ограничена и reconnect восстанавливает события.

### A09 · P1 · RPC может зависнуть в UI; lifetime контекста не защищён

**Основание:** Подтверждено кодом; TSAN и искусственное зависание UI не запускались.

Cancel/HardStop/Confirm/Answer выполняют синхронные RPC на UI-потоке без deadline и игнорируют статус. runSend игнорирует Finish. Atomic<ClientContext*> защищает адрес, но не lifetime: cancelActive может использовать уже уничтоженный стековый context.

**Код:** [src/shell/facade.cpp:379](/data/repos/arli/src/shell/facade.cpp:379), [src/shell/facade.cpp:427](/data/repos/arli/src/shell/facade.cpp:427), [src/shell/desk.cpp:73](/data/repos/arli/src/shell/desk.cpp:73), [src/shell/desk_turn.cpp:208](/data/repos/arli/src/shell/desk_turn.cpp:208).

**Доработка:** Отдельный control executor с deadlines, владение контекстом под mutex/shared owner, единый status mapping. Pending карточки закрывать после подтверждённого результата, с retry при безопасной ошибке.

**Приёмка:** Потеря сети/демона, UNAUTHENTICATED, INVALID_ARGUMENT и быстрые переключения диалогов не замораживают окно; ASan/TSAN чисты.

### A10 · P1 · Queued task не фиксирует workdir и политику

**Основание:** Подтверждено кодом, отдельный queue/settings race не запускался.

В задаче сохранён текст, но workdir/model/permissions читаются из глобальных настроек при Claim. После смены настроек ожидающая задача может выполниться в другом проекте. Диалог не закреплён за workspace.

**Код:** [src/daemon/agent_service.cpp:223](/data/repos/arli/src/daemon/agent_service.cpp:223), [src/daemon/hub.cpp:147](/data/repos/arli/src/daemon/hub.cpp:147), [src/proto/arli.proto:414](/data/repos/arli/src/proto/arli.proto:414).

**Доработка:** Workspace identity и неизменяемый task config snapshot при enqueue; секреты передавать ссылкой/актуальным секретом по явной политике, не дублировать в audit.

**Приёмка:** Поставить две задачи, сменить глобальный workdir до второй: она остаётся в исходном проекте и с согласованными разрешениями.

### A11 · P1 · Подтверждения отвечают успехом без принятия решения

**Основание:** Воспроизведено: неверный confirm_token и ответ без вопроса вернули ok=true.

Ответ означает только постановку сообщения в outbound при живой Session. Воркер может его отвергнуть. AskUser привязан только к dialog_id: старый ответ может попасть в новый вопрос того же диалога.

**Код:** [src/daemon/hub_control.cpp:157](/data/repos/arli/src/daemon/hub_control.cpp:157), [src/daemon/hub_control.cpp:173](/data/repos/arli/src/daemon/hub_control.cpp:173), [src/daemon/ask.cpp:38](/data/repos/arli/src/daemon/ask.cpp:38).

**Доработка:** Стабильные approval_id/question_id, task_id/turn_id, состояние pending/resolved, worker ack и идемпотентное решение. Статусы NOT_FOUND/FAILED_PRECONDITION вместо ложного accepted.

**Приёмка:** Неверные/устаревшие/повторные ответы различимы; reconnect показывает только актуальные вопросы; дубли не изменяют уже принятое решение.

### A12 · P2 · Сессии и claims недостаточно коррелированы

**Основание:** Подтверждено кодом; hostile worker/fault injection не выполнялись.

Worker frames не сверяются с тройкой активной задачи и epoch сессии; task_id=0 подменяется busy_task. Нет capability/version negotiation и ClaimAccepted. Heartbeat обновляет last_event, поэтому при обычном idle_timeout=600 поддерживает живость даже без прогресса.

**Код:** [src/daemon/hub_control.cpp:49](/data/repos/arli/src/daemon/hub_control.cpp:49), [src/daemon/agent_worker.cpp:95](/data/repos/arli/src/daemon/agent_worker.cpp:95), [src/daemon/server.cpp:40](/data/repos/arli/src/daemon/server.cpp:40).

**Доработка:** Session epoch/worker instance ID, строгая корреляция и валидация frame, ClaimAccepted, отдельные liveness/progress timers, причины завершения stream.

**Приёмка:** Устаревшие Done/Observation/Heartbeat не меняют новую задачу; несовместимый воркер отклонён; живой, но зависший инструмент обнаруживается отдельно.

### A13 · P2 · Replay не является полным возобновляемым журналом

**Основание:** Подтверждено proto и преобразованием trace.

Нет event_seq/after_seq и явного task_id в UI-событии. Tokens не сохраняются; часть snapshots и EditStats не восстанавливается из steps. Старые confirm/ask replay-ятся без событий решения, UI может показать уже закрытую карточку.

**Код:** [src/proto/arli.proto:166](/data/repos/arli/src/proto/arli.proto:166), [src/daemon/trace.cpp:74](/data/repos/arli/src/daemon/trace.cpp:74), [src/daemon/hub_waiters.cpp:57](/data/repos/arli/src/daemon/hub_waiters.cpp:57), [src/shell/desk_turn.cpp:79](/data/repos/arli/src/shell/desk_turn.cpp:79).

**Доработка:** Курсор событий и snapshot+delta контракт, gap detection, task identity, pending interaction snapshot. Разделить replay истории и активные интерактивные карточки.

**Приёмка:** Reconnect до/после approval и во время генерации не дублирует/не теряет состояние; cursor resume работает после завершения задачи.

### A14 · P2 · Не заданы прикладные квоты и пагинация

**Основание:** Подтверждено контрактом и обработчиками; пороги transport отдельно не измерялись.

ListDialogs/GetTurnTrace/OpenDialog собирают большие ответы; request text/settings/число подписчиков и очереди не имеют общего бюджета. Args сохраняются без общего лимита. Часть больших ответов упрётся в ограничения gRPC, память и SQLite.

**Код:** [src/daemon/agent_service.cpp:33](/data/repos/arli/src/daemon/agent_service.cpp:33), [src/daemon/agent_service.cpp:62](/data/repos/arli/src/daemon/agent_service.cpp:62), [src/daemon/db_steps.cpp:66](/data/repos/arli/src/daemon/db_steps.cpp:66), [src/daemon/server.cpp:112](/data/repos/arli/src/daemon/server.cpp:112).

**Доработка:** Курсорная пагинация, лимиты размера входа/выхода/очередей, RESOURCE_EXHAUSTED с деталями, UTF-8-safe truncation, retention. Не повышать message limit без квот.

**Приёмка:** Большой trace и тысячи диалогов доступны страницами; oversized запрос отклонён до побочных эффектов; память bounded.

### A15 · P2 · Health синхронно зависит от LLM

**Основание:** Подтверждено кодом.

Health делает сетевой /models с таймаутом до 8 секунд, UI ждёт 700 мс. Недоступная/медленная модель может выглядеть как недоступный демон и занимать RPC threads.

**Код:** [src/daemon/agent_service.cpp:21](/data/repos/arli/src/daemon/agent_service.cpp:21), [src/daemon/llm_models.cpp:81](/data/repos/arli/src/daemon/llm_models.cpp:81), [src/shell/desk_jobs.cpp:18](/data/repos/arli/src/shell/desk_jobs.cpp:18).

**Доработка:** Дешёвый liveness; асинхронно кэшируемая LLM readiness; отдельные статусы и timestamp. Стандартный grpc.health.v1 для внешних клиентов — полезное дополнение.

**Приёмка:** При зависшем /models Health демона отвечает быстро, UI различает daemon/worker/model readiness.

### A16 · P1 · Удаление активного диалога не согласовано с задачей

**Основание:** Подтверждено кодом, разрушительный сценарий не запускался.

DeleteDialog удаляет диалог и каскадно tasks/messages, но не отменяет и не ожидает занятого воркера. Он может продолжать менять файлы; запись финального сообщения упадёт по FK.

**Код:** [src/daemon/agent_service.cpp:57](/data/repos/arli/src/daemon/agent_service.cpp:57), [src/daemon/db_messages.cpp:65](/data/repos/arli/src/daemon/db_messages.cpp:65).

**Доработка:** Запретить удаление активного диалога либо реализовать cancel-and-wait под общим lifecycle lock, затем транзакционное удаление и cleanup trace/audit.

**Приёмка:** Удаление queued/running/waiting-confirm диалога имеет определённую семантику и не оставляет бесхозную работу.

### A17 · P2 · Нет надёжной транзакции набора правок и отката

**Основание:** Подтверждено кодом; сбои диска и гонки symlink не инъецировались.

Backup перезаписывает единственную .bak и глотает ошибки; copy/restore не атомарны. Проверка expected и rename разделены, путь разрешается до операции. Нет multi-file rollback, directory fsync, сохранения ACL/xattr.

**Код:** [src/daemon/tools/backup.cpp:5](/data/repos/arli/src/daemon/tools/backup.cpp:5), [src/daemon/tools/atomic_publish.hpp:31](/data/repos/arli/src/daemon/tools/atomic_publish.hpp:31), [src/daemon/tools/tier_a_manage.cpp:118](/data/repos/arli/src/daemon/tools/tier_a_manage.cpp:118).

**Доработка:** Mutation service: snapshot/hash/version, защищённые dirfd-операции, проверяемая резервная копия вне доступного модели дерева, журнал changeset и revert. Учитывать конфликты внешнего редактора.

**Приёмка:** ENOSPC/permission/symlink swap/сбой второй правки не дают тихой потери исходника; можно откатить один ход, сохранив чужие изменения.

### A18 · P1 · HTTP guard не закрывает IPv6 и повторный DNS

**Основание:** Подтверждено кодом; запросы к внутренним сервисам не выполнялись.

Проверяется только AF_INET, адреса IPv6 private/mapped пропускаются. После проверки getaddrinfo curl резолвит заново. Ручной URL parser и proxy env создают расхождение проверяемой и фактической цели.

**Код:** [src/daemon/tools/http_tools.cpp:103](/data/repos/arli/src/daemon/tools/http_tools.cpp:103), [src/daemon/tools/http_tools.cpp:113](/data/repos/arli/src/daemon/tools/http_tools.cpp:113), [src/daemon/tools/http_tools.cpp:131](/data/repos/arli/src/daemon/tools/http_tools.cpp:131).

**Доработка:** Стандартный URL parser, полная проверка IPv4/IPv6, pin проверенного IP при соединении, явная proxy policy; сетевые mutations отдельной capability.

**Приёмка:** Локальный тестовый IPv6/DNS-rebinding/proxy стенд подтверждает запрет, обычный публичный HTTPS работает.

### A19 · P2 · Проверка результата основана на эвристике команд

**Основание:** Подтверждено кодом; прежние live eval проверяют конкретные успешные сценарии.

Нет обязательного критерия завершения по задаче. Распознаётся ограниченный список тестовых команд; произвольный ./test.sh или составная команда могут не попасть в verification. Встроенный синтаксис ограничен JS/Python/JSON.

**Код:** [src/daemon/verification.hpp:16](/data/repos/arli/src/daemon/verification.hpp:16), [src/daemon/verification.hpp:38](/data/repos/arli/src/daemon/verification.hpp:38), [src/daemon/turn.cpp:782](/data/repos/arli/src/daemon/turn.cpp:782).

**Доработка:** Task acceptance + VerificationPlan: что изменено, каким check подтверждено, по какому revision; типизированный результат проверок, честное unverified. Приоритет — проверки реального проекта, включая C++/QML.

**Приёмка:** Непрошедший пользовательский test script блокирует успешный outcome; устаревшая проверка инвалидируется; отсутствие тестов видно явно.

### A20 · P2 · Контекст и правила проекта не оформлены как подсистема

**Основание:** Подтверждено кодом; качество разных моделей этим аудитом не измерялось.

Оценка bytes/4 не учитывает schemas/tools и выходной бюджет 4096/8192. Summary ограничивает старые указания четырьмя фрагментами по 600 байт. Автоматической загрузки и области действия AGENTS.md нет; модель может прочесть файл сама, но гарантий нет.

**Код:** [src/daemon/context.cpp:55](/data/repos/arli/src/daemon/context.cpp:55), [src/daemon/context_summary.cpp:27](/data/repos/arli/src/daemon/context_summary.cpp:27), [src/daemon/llm_internal.cpp:53](/data/repos/arli/src/daemon/llm_internal.cpp:53), [src/daemon/turn.cpp:82](/data/repos/arli/src/daemon/turn.cpp:82).

**Доработка:** ContextBuilder с резервом ответа и schemas, usage от провайдера, durable summary и неизменяемые ограничения задачи; scoped repository instructions и отделение недоверенных данных.

**Приёмка:** Граничное окно не переполняется; длинное ограничение и правила вложенного каталога сохраняются после compaction и нового хода.

### A21 · P2 · Фоновые jobs не имеют устойчивого владения процессом

**Основание:** Подтверждено кодом.

Результат jobs определяется kill(pid,0), exit code отсутствует. StopJob сигналит process group по сохранённому PID без защиты от reuse; логи не ограничены. Обычная отмена shell не передаёт cancel token в runner.

**Код:** [src/daemon/jobs.cpp:118](/data/repos/arli/src/daemon/jobs.cpp:118), [src/daemon/agent_jobs.cpp:10](/data/repos/arli/src/daemon/agent_jobs.cpp:10).

**Доработка:** Supervisor с pidfd либо PID+starttime, task/job ownership, exit code, log rotation, cancel всей группы и streaming bounded output.

**Приёмка:** После перезапуска и reuse PID чужой процесс не затронут; завершившаяся job имеет exit code; Cancel останавливает дочерние процессы.

### A22 · P2 · Поставка gRPC-клиента и tool-адаптеров не закончена

**Основание:** Подтверждено CMake, registry и скриптом интеграции.

Proto и embed есть. Нет готового export CMake package с версиями/транзитивными зависимостями и end-to-end внешнего клиента. gRPC сейчас управляет Arli; инструментов вызова прикладных сервисов Emperion в registry нет.

**Код:** [src/CMakeLists.txt:319](/data/repos/arli/src/CMakeLists.txt:319), [scripts/check-emperion-embed.sh:1](/data/repos/arli/scripts/check-emperion-embed.sh:1), [src/daemon/tools/registry.cpp:128](/data/repos/arli/src/daemon/tools/registry.cpp:128).

**Доработка:** ArliClient SDK/примеры и install-tree consumer test. Отдельно GrpcToolProvider с allowlisted methods, schemas, credentials per service, deadlines/cancellation, audit и approval для mutations.

**Приёмка:** Независимое приложение из установленного пакета выполняет health/send/resume/cancel. Агент вызывает тестовый разрешённый сервис и не может вызвать запрещённый метод.

### A23 · P2 · Текущая приёмка не покрывает критические отказы

**Основание:** Подтверждено: CI 19/19, но отдельный probe воспроизвёл дефекты.

Нет покрытия slow readers, больших историй, memory rollover, duplicate daemon, stale approvals, env isolation/background sandbox. ASan/UBSan option есть, регулярного sanitizer gate нет; TSAN не настроен. Документация ошибочно утверждает, что обычный shell не использует bwrap; версия требований устарела.

**Код:** [scripts/ci.sh:1](/data/repos/arli/scripts/ci.sh:1), [tests/lifecycle_test.cpp:1](/data/repos/arli/tests/lifecycle_test.cpp:1), [tests/audit_test.cpp:2](/data/repos/arli/tests/audit_test.cpp:2), [docs/08-ops.md:36](/data/repos/arli/docs/08-ops.md:36).

**Доработка:** Добавить эти contract/fault-injection тесты, sanitizer job, proto compatibility gate, install/RPM/AppImage и Emperion consumer smoke. Обновить документацию по фактическому коду.

**Приёмка:** Новые регрессии падают на текущих дефектах и проходят после исправлений; релиз проверяется из установленного пакета.

## Что создать для полноценной агентской работы

1. **ProcessRunner + CapabilityPolicy.** Единые правила для файлов, shell, background, HTTP и будущих RPC-tools. Рабочая папка, env, сеть, approval, cancellation и лимиты заданы явно.
2. **TaskStateMachine.** Task config snapshot, workspace identity, устойчивые outcome, pause/waiting-user, cancellation и recovery без ложного success. Возобновление после сбоя только от проверенного checkpoint, без слепого повтора mutations.
3. **ContextStore и MemoryStore.** Правильный хвост диалога, долговечная сводка, стабильные факты, правила репозитория, учёт реального контекстного бюджета.
4. **MutationService.** Changeset на ход, обнаружение конфликтов, сохранение оригиналов, локальный diff, откат одного/нескольких файлов. Затем patch/batch-edit для согласованных изменений; не начинать с ослабления read-before-edit.
5. **VerificationPlan.** Команды приёмки проекта, результаты по версии файлов, различение синтаксиса/поведения/непроверенного результата.
6. **JobSupervisor.** Наблюдаемые фоновые задачи, владение процессами, exit codes, bounded logs, безопасный stop и восстановление состояния.
7. **Typed ToolRegistry.** Одна декларация schema+validation+capabilities+executor, стабильные error codes и recovery hints вместо зависимостей от русских строк. Выдавать модели только доступные в режиме tools.

Архивы, дополнительные языковые проверки, внешние connectors, browser automation и multi-agent orchestration — расширения по потребности. Они не являются предварительным условием качественного локального C++/Qt агента. Архивы должны использовать тот же approval relay и защиту от traversal/zip-bomb.

## Как довести использование gRPC

Сейчас gRPC — управляющий транспорт **клиент → daemon → worker**. Наличие этого транспорта само по себе не даёт модели инструментов для обращения к другим сервисам Emperion.

Для устойчивого управления Arli расширять `arli.v1` без переименования существующих полей:

- SubmitTask/GetTask с client_request_id и идемпотентным принятием. Send сохранить для совместимости; запрос после неопределённого сетевого исхода не должен создавать дубль.
- Outcome/error_code/retryable в терминальном результате, task_id и event_seq в событиях; Resume/Watch с after_seq и полным snapshot для пропусков.
- Approval/question ID, task correlation, явное подтверждение применения решения и pending-interaction snapshot.
- Session epoch, capabilities/version, ClaimAccepted; независимые liveness и progress; корректное закрытие обеих сторон потока.
- Bounded delivery per subscriber, deadlines для unary/control calls, reconnect state machine; никаких тяжёлых RPC на UI-потоке.
- Пагинация и квоты, rich errors, schema validation, revision/field mask для безопасного обновления Settings несколькими клиентами.
- Cached readiness и стандартный health service для интеграционных клиентов, если нужен; метрики длительности/ошибок/очередей, correlation ID и безопасный audit.
- Клиентская библиотека и примеры, CMake export/config, install-tree consumer, совместимость старого/нового клиента в CI. Reflection удобен для разработки и grpcurl, но не заменяет versioned contract и не обязателен для работы.

Для обращения агента **к другим gRPC-сервисам** нужен отдельный GrpcToolProvider: заранее разрешённые service/method, преобразование schema, типизированные аргументы/результаты, ограничение размеров, credentials на сервис, deadlines, cancellation, read-only/mutation классификация и approval. Не отдавать модели internal_token и произвольный вызов любого метода.

LocalCredentials + bearer подходят текущему loopback-контракту; заменять их на Insecure credentials нельзя. Удалённые воркеры/сеть — отдельная архитектурная задача: сейчас worker зависит от общей SQLite и локальной файловой системы. Для неё нужны mTLS/identity/authorization, синхронизация workspace/artifacts и RPC-владение состоянием. Просто открыть порт на 0.0.0.0 недостаточно и противоречит действующему канону.

## Порядок реализации

**Этап 1 — устранить воспроизведённые дефекты.** A01–A07, A10–A11, A16 и A18. Добавить воспроизводящие тесты до исправлений. Критерий: никакой утечки окружения, записи вопреки политике, потери памяти, ложного done или изменения задач вторым демоном.

**Этап 2 — надёжный локальный gRPC.** A08–A09, A12–A15. Критерий: slow reader, disconnect, daemon restart, stale approval, большие истории и два UI-клиента не приводят к зависанию, дублям или потере состояния.

**Этап 3 — агентская приёмка и эксплуатация.** A17, A19–A21, A23. Критерий: реальная задача C++/QML с несколькими файлами, чужими изменениями, тестами, отменой и восстановлением проходит через полный daemon/worker/UI стек.

**Этап 4 — внешняя интеграция.** A22 и GrpcToolProvider. Критерий: независимый клиент из установленного пакета и тестовый сервис Emperion работают с auth, approvals, deadlines и восстановлением потока. Реальную интеграцию проверять в дереве Emperion.

## Основания рекомендаций по gRPC

Blocking write при flow control допустим по модели gRPC; следовательно, его нельзя выполнять на общем пути dispatch/watchdog: [Flow control](https://grpc.io/docs/guides/flow-control/). По умолчанию deadline отсутствует: [Deadlines](https://grpc.io/docs/guides/deadlines/). Отмена RPC требует кооперации выполняемого кода: [Cancellation](https://grpc.io/docs/guides/cancellation/). Стандартная проверка readiness описана в [Health checking](https://grpc.io/docs/guides/health-checking/).

## Повтор воспроизведений

Из корня репозитория после сборки `build-ci`:

```bash
c++ -std=c++20 -Itests -Isrc/daemon -Isrc/common -Ibuild-ci/generated -Ithird_party \
  docs/audit-2026-10-10/probe.cpp build-ci/libarli_core.a build-ci/libarli_common.a \
  build-ci/libarli_proto.a $(pkg-config --libs grpc++ protobuf libcurl sqlite3) \
  -pthread -o /tmp/arli-full-audit-probe
/tmp/arli-full-audit-probe
```

Probe использует абсолютный `/data/repos/arli/build-ci/` для бинарников; при переносе изменить эту строку. Он проверяет текущие симптомы, а не является зелёным регрессионным тестом исправлений: отдельные boolean=true в отчёте означают обнаруженный дефект. Локальные sockets и bubblewrap требуют подходящего окружения. Результат записывается в `/tmp/arli-full-audit-probe.json`.

## Реализация после аудита

Первый этап и этап восстановления потоков внесены в рабочее дерево. Исходные находки выше сохранены как история аудита; текущий статус каждого пункта — в [implementation-status.json](audit-2026-10-10/implementation-status.json). Полный roadmap ещё не завершён.

Исправлены наследование окружения shell и обход sandbox фоновыми командами, окно истории, сохранение памяти и её ID, ложный terminal success, повреждение состояния при запуске второго daemon и удаление активного диалога. Очередь хранит снимок настроек без API key (schema 8); смена endpoint до dispatch завершает queued task ошибкой с просьбой отправить заново. При неизменном endpoint используется текущий ключ, что допускает ротацию credentials.

В gRPC callback оркестратора больше нет сетевого Write: RPC-поток обслуживает ограниченную очередь событий. Qt использует отдельную очередь control RPC с deadlines и защищённым временем жизни ClientContext. Worker events проверяются по task/dialog/turn; heartbeat не скрывает отсутствие прогресса. Health возвращает кэш фонового probe, который может отставать до очередной проверки. HTTP-инструмент проверяет IPv4/IPv6 и закрепляет разрешённые DNS-адреса; proxy из окружения отключён.

Итоговый `bash scripts/ci.sh`: 20/20, arli-law, qmllint и module size прошли.

Проверки: новый `tests/audit_regression_test.cpp` использует изолированные настоящие daemon/agent и authenticated local gRPC, но детерминированную mock LLM. Это не заменяет проверку модели. Реальная Gemma `/models/gemma-4-26B.gguf` проверяется отдельно через `arli-live-eval`, который вызывает production `run_turn` непосредственно, без Qt и transport-слоя.

Первый живой прогон: [4/5](audit-2026-10-10/implementation-live-initial.json). В file_integrity модель добавила LF после `ready`; исходный prompt не указывал отсутствие LF, хотя byte-exact oracle требовал его. Этот результат сохранён. Условие уточнено до «ровно 5 байтов, без перевода строки», а read_file теперь сообщает bytes/final_newline. Повторный прогон: [5/5](audit-2026-10-10/implementation-live-final.json). Все исходные проверки сохранности CRLF/NBSP/EOF и дальнего диапазона сохранены.

После первого этапа оставались durable replay/cursors/idempotency/question_id (реализованы ниже). Всё ещё остаются quotas/pagination/retention, общий secret redaction, устойчивый supervisor фоновых задач, транзакции нескольких файлов, полноценные verification targets/context budget, устанавливаемый client SDK и GrpcToolProvider для Emperion, нагрузочные и sanitizer проверки. Установленный экземпляр приложения этим этапом не обновлялся.


### Этап восстановления gRPC-потоков

Реализованы persistent events (schema 9), `task_id/event_id/after_event_id`, повтор Send по request_id без повторного enqueue, AttachTurn завершённой задачи, атомарность последнего события и terminal status. Qt повторяет соединение ограниченное число раз и только при подтверждённой поддержке daemon. Сквозной question_id защищает новые клиенты от запоздалых ответов; replay не открывает уже разрешённые вопросы/подтверждения. Контракт и совместимость описаны в [каноне процессов](03-orchestrator-agent.md#восстановление-потока-schema-9).

Итоговый CI: **21/21**, arli-law, qmllint, module size passed. `stream-resume` проверяет реальные процессы/локальный gRPC с mock LLM: разрыв, повтор Send, restart, rollback при ошибке INSERT события, страницы, исторические вопросы, медленный UI и следующую задачу.

Отдельно проверена реальная локальная Gemma через daemon/agent: [успешный отчёт](audit-2026-10-10/stream-live-final.json). После первого события соединение прервано; повторный Send вернул события 2–16 той же задачи, без повторов. Результат — ровно один файл с пятью байтами `ready`. Это проверка без Qt, но через production transport и worker, в отдельном временном workspace.

Первый live-прогон выявил **A24**, отдельный незакрытый дефект классификатора: «других файлов не создавай» запрещает и явно запрошенный result.txt. Модель затем запросила shell; runner не выдал разрешение и остановил только тестовый daemon. [Первоначальный отчёт](audit-2026-10-10/stream-live-initial.json) сохранён. Повторный транспортный тест убрал эту фразу, сохранил проверку единственного файла и прошёл; A24 этим не исправлен.

Следующие незакрытые части: общие лимиты/retention, session generation и durable acknowledgement worker, полный lifecycle фоновых процессов, file transactions/verification/context, внешний SDK/Emperion и A24. Установленный экземпляр не обновлялся.

### Исправление A24: создание явно указанного файла

Фраза «Других файлов не создавай» теперь ограничивает набор разрешённых новых путей,
а не отменяет явную команду «Создай result.txt». Полный запрет («Не создавай новые
файлы», «Без новых файлов») сохраняет приоритет, включая сочетание с частным запретом.
Имена сохраняют регистр; абсолютные пути, переходы через `..` и symlink не получают исключений.

Проверка выполняется перед dispatch и охватывает create/write, назначения copy/move,
restore отсутствующего файла и создание каталогов. Разрешены родительские каталоги
явно названного файла. Каталог нельзя перенести под видом одного разрешённого файла.
Работа с существующими файлами сохраняется в рамках остальных проверок; служебные
backup/temp-файлы инструментов не считаются новыми пользовательскими результатами.
Произвольные shell/background команды при запрете создания блокируются, поскольку
их побочные записи нельзя ограничить этим списком путей.

Разбор консервативный: исключения извлекаются из прямых команд «Создай [файл] путь»
или «Create [file] path» в начале предложения; поддержаны кавычки и пробелы внутри
кавычек. Упоминание файла при чтении или обсуждении не даёт разрешения на создание.
Неоднозначные перечисления и нераспознанные формулировки не расширяют разрешения.
Это ограниченный распознаватель явных указаний, не универсальный анализ естественного языка.

Регрессии: `tests/create_policy_test.cpp` проверяет границы разрешений,
`tests/turn_execution_test.cpp` проводит реальный worker turn с mock LLM и проверяет
создание разрешённого файла и отказ двум попыткам создать соседние.
В live Stream Eval восстановлен исходный запрос целиком, включая запрещавшую фразу;
успех дополнительно требует успешного read_file после создания.

Итоговый CI после исправления: **22/22**, arli-law, qmllint и module size прошли.
[Живой прогон исходного запроса](audit-2026-10-10/a24-live-final.json) успешен:
реальная Gemma через daemon/agent, create_file и read_file без ошибок, один файл
с ровно пятью байтами ready; reconnect вернул события 2–16 без повторов.
A24 закрыт для описанного контракта. Установленный экземпляр не обновлялся.

### A12: изоляция поколений worker Session

Reader, outbound writer, завершение RPC и watchdog получили generation текущего
соединения. Запоздалые операции старого поколения не изменяют новую сессию.
Добавлена проверка порядка и формата Hello, отсутствующего payload и принадлежности
heartbeat текущей задаче; heartbeat после Done игнорируется без разрыва соединения.

Детерминированный тест `session-generation` воспроизводит stale reader/writer/teardown:
старый writer не забирает новый Claim, старый Done с совпадающими идентификаторами
не завершает новую задачу, старый teardown не сбрасывает нового worker.
Проверяются ошибочные Hello и отсутствие обновления timestamp чужим heartbeat.
Обычные transport/lifecycle тесты используют реальные процессы и mock LLM.

A12 остаётся частично выполненным: durable acknowledgement/retry Claim и привязка
Hello к ожидаемому процессу остаются отдельной работой. PID проверяется на
положительность, аутентификация по-прежнему local credentials + bearer.

Итоговый CI: **23/23**, arli-law, qmllint, module size прошли.
[Живой прогон](audit-2026-10-10/session-live-final.json): реальная Gemma через
обновлённые daemon/agent, исходный запрос A24, создание и чтение файла с пятью
байтами ready, восстановление gRPC без повторов. Установленное приложение не обновлялось.

### A12: подтверждение принятия Claim

Добавлены согласование поддержки ACK, номер доставки, ограниченный повтор внутри
сессии и защита worker от повторного исполнения Claim. SQLite schema 10 хранит
результат доставки без содержимого команды/секретов. Принятие не приравнивается
к завершению задачи; потеря Session не вызывает повторного запуска инструментов.

Новый детерминированный тест `claim-delivery` проверяет потерянный ACK, неизменность
повторного Claim, единственный запуск, чужое поколение, изменённое содержимое
под тем же номером, запоздалый старый номер, прекращение повторов после ACK,
таймаут и сохранение accepted/unconfirmed при повторном открытии SQLite.
Live-проверка дополнительно требует persisted receipt accepted от настоящего worker.

Для предотвращения ложного отказа следующему Claim worker освобождает флаг
занятости перед отправкой Done; reader присоединяет завершавшийся поток до
запуска следующего. Heartbeat не закрывает Session при shutdown до завершения
этой финализации.

A11 остаётся частично выполненным: ACK ответов/подтверждений и их отображение в UI
ещё не реализованы. A12 также не включает восстановление исполнения после аварии
и проверку PID Hello против ожидаемого процесса.

Итоговый CI: **24/24**, arli-law, qmllint, module size прошли.
[Живой прогон Claim ACK](audit-2026-10-10/claim-live-final.json) успешен:
реальная Gemma через daemon/agent/gRPC, квитанция accepted в SQLite, один файл
с пятью байтами ready и последующим чтением. Установленное приложение не обновлялось.

### Волна 1 общей цели: доставка ответов и разрешений

Введены отдельная capability для interaction ACK, номера доставки в RPC replies,
SQLite-квитанции accepted/rejected/unconfirmed и ограниченные повторы внутри
Session. Worker сериализует применение ответа/решения и ACK перед событиями
разблокированного хода. Cache 64 доставок не применяет старый ответ к новому вопросу.
Уже принятое разрешение нельзя заменить противоположным повторным решением.
Cancellation имеет приоритет над таймаутом ACK.

Новый interaction-delivery проверяет потерю ACK после потребления ответа, следующий
вопрос, повторную противоположную decision, таймаут, durable receipt и cancellation.
stream-resume дополнительно использует реальные daemon/agent: два вопроса, replay
двух статусов принятия и persisted receipts; отдельно подтверждение и отказ для
безопасной команды echo, с проверкой реального результата run_shell.

Живой прогон выявил A25: модель приняла служебный OK в результате ask_user за часть
ответа и записала OK\\nready. [Первый отчёт](audit-2026-10-10/interaction-live-initial.json)
сохранён. Успешный ask_user теперь передаёт буквальный ответ без обрезки/префикса.
[Тот же живой запрос после исправления](audit-2026-10-10/interaction-live-final.json)
прошёл: один вопрос, обе квитанции accepted, один файл ровно ready (5 байт) и чтение
для проверки. Mock также проверяет буквальность ответов.

Полная цель остаётся активной. Следующая волна — A20, бюджет запроса и правила
workspace; остальные незакрытые пункты сохранены в implementation-status.json.

Итог первой волны: CI **25/25**, arli-law, qmllint и module size passed.
Установленный экземпляр не обновлялся.

### Волна 2: бюджет полного запроса и фактический usage

Добавлен отдельный request budget по окончательному wire body: сообщения, tool
schemas, structured response schema и template guide учитываются вместе с резервом
ответа. История сжимается в оставшийся объём. CONTEXT_LIMIT возникает до HTTP и не
повторяется сетевыми retries; слишком большие закреплённые инструкции сохраняются
целиком и приводят к явному отказу, вместо незаметного удаления.

Новый стандартный размер окна — 16384 токена, без миграции сохранённых настроек.
Резерв равен четверти окна с верхними пределами профиля. Малые окна старых mock
тестов не учитывали schemas и ответ: тесты работы файлов теперь используют 16384,
а отдельный request-budget проверяет маленькое окно и отказ до сети. Тест длинного
хода по-прежнему требует фактического сжатия и сохранения закреплённого ограничения.

Добавлены SSE/nonstream usage и обработка usage-only чанка с пустым choices.
Actual prompt_tokens повышает коэффициент следующих оценок при недооценке.
ContextUse расширен provider_reported/reserved_output_tokens/completion_tokens;
Qt обозначает локальную оценку знаком ≈. Это консервативная эвристика, а не
model tokenizer: нельзя объявлять фактическую token boundary доказанной для любого
провайдера исключительно на основании оценки.

[Живой прогон](audit-2026-10-10/context-budget-live-final.json) прошёл на реальной
Gemma через daemon/agent: вопрос, подтверждённый ответ, создание/чтение ровно ready,
обе квитанции accepted, provider usage получен. Последний prompt — 4051 токен,
резерв ответа — 4096. Scoped repository rules, durable summary и сохранение
ограничений между ходами остаются следующими частями A20.

Итог второй волны: CI **26/26**, arli-law, qmllint и module size passed.
Общая цель активна; установленное приложение не обновлялось.

### Волна 3: scoped workspace rules

Добавлена загрузка root/nested AGENTS.md по аргументам файловых инструментов.
Новые правила закрепляются в system context и откладывают текущий batch до
следующего ответа модели; изменения/удаления перечитываются, ошибки явно
останавливают ход. Ограничены размер/число файлов; symlink/traversal/необычные
типы файлов не читаются как инструкции. Правила переживают compaction и заново
загружаются на новом ходе. Обычный вывод инструментов не повышается до инструкций.

CI **27/27**, arli-law, qmllint и module size прошли. Новые тесты охватывают область
действия, изменённые/удалённые правила, новый loader/ход, симлинки, размер,
compaction и настоящий run_turn с попыткой записи до ознакомления с правилами.

[Живой прогон](audit-2026-10-10/workspace-rules-live-final.json): Gemma создала
nested/result.txt с ровно ready, узнав содержимое из вложенного AGENTS.md, а не
из user prompt, и прочитала файл. Один вызов отложен RULES_UPDATED; дополнительная
попытка shell отклонена runner без выдачи разрешения, после чего модель завершила
задачу файловыми инструментами. Это успешное восстановление, не прогон без ошибок
инструментов. Установленное приложение не обновлялось.

A20 остаётся partial: нужны durable summary и пользовательские ограничения между
ходами; оценка бюджета не является точным токенизатором произвольного провайдера.
Общая цель активна.

### Волна 4: сохраняемая история инструментов

Добавлен worker-owned снимок `context_state` внутри `messages`, исключённый из
видимой переписки до LIMIT. Каждый завершившийся вызов записывает только имя,
результат, номер хода и пути. Вывод инструмента, текст файлов, команды и ответы
пользователя в снимок не попадают. При новом ходе снимок того же канонического
workdir включается в закреплённый контекст как исторические данные, с явным
запретом трактовать их как инструкции или текущую проверку файлов.

Лимит — 32 действия / 16 KiB, вытеснение отмечается. Повреждённый снимок
невалидной структуры игнорируется. Регрессии проверяют открытие базы заново,
изоляцию каталогов, отсутствие содержимого/команд, скрытие из переписки,
ограничение объёма, сохранность при compaction и удаление вместе с диалогом.
Живая Gemma через daemon/agent создала и прочитала ready; после остановки
процессов обе записи подтверждены отдельным чтением SQLite:
`durable-context-live-final.json`. Поведение модели на следующем ходе с этим
снимком пока не проверено живым сценарием.

A20 остаётся partial: история действий не заменяет durable пользовательские
ограничения и полный смысловой summary. Также это не файловый WAL: авария между
эффектом инструмента и записью может оставить историю неполной. Точный tokenizer
и граничные сценарии разных моделей остаются открытыми.

Проверка волны 4: `bash scripts/ci.sh` — 28/28, arli-law, qmllint,
ограничения размеров модулей и `git diff --check` прошли.

### Волна 5: закреплённые правила диалога

Добавлены `dialogs.instructions` (схема 11), authenticated
`SetDialogInstructions`, поля Dialog/ClaimTask и редактор «Правила диалога» в Qt.
Пользователь задаёт полный текст, пустой текст снимает правила; лимит 16 КиБ,
NUL отвергается. Изменение сериализовано с enqueue, очередь сохраняет прежнюю
версию. Worker получает снимок и не может менять его. Правила не сворачиваются;
известные явные запреты участвуют также в gates. Произвольные смысловые условия
остаются инструкциями модели, а не доказанными формальными ограничениями.
Hello объявляет поддержку: старый worker не исполняет задачу, требующую правил.
Правила относятся к диалогу, включая смену workdir; AGENTS.md остаётся механизмом
правил проекта. Это явное закрепление, а не автоматическое извлечение из чата.

При проверках найдены и исправлены два дефекта интеграции: добавление пустой строки
перед заданием ломало распознавание разрешённого пути create; длинная история с
закреплёнными правилами могла пройти message-only compaction и всё ещё превысить
полный запрос. Теперь исходное намерение определяется по текущему запросу, а
правила добавляют ограничения; compaction адаптивно уточняет бюджет по полному
сериализованному запросу, не обрезая pins.

Живая проверка `--instructions`: длинные правила содержат требуемые пять байтов
в конце текста (за пределами прежних 600 байт). Gemma создаёт/читает result.txt,
после полного перезапуска daemon/agent — second.txt. Оба содержат ровно ready;
в запросах этих ходов слово ready отсутствует. `pinned-instructions-live-final.json`.
A20 остаётся partial: явные pins не равны автоматическому сохранению всех
ограничений обычной переписки; точные tokenizer boundaries и смысловой durable
summary ещё открыты.

Итоговая проверка волны 5: `bash scripts/ci.sh` — 28/28, arli-law, qmllint,
лимиты модулей и `git diff --check` прошли. RPC-регрессии проверяют auth, размер,
NUL, чтение и очистку правил; Claim — неизменность снимка и отказ старому агенту;
полный worker turn — длинные правила после сворачивания большой истории.

### Волна 6: проверяемые копии и публикация файлов

Устранены два пути тихой потери: ошибки backup больше не игнорируются, copy/restore
не перезаписывают inode назначения по частям. Общая публикация использует
openat/O_NOFOLLOW, случайный O_EXCL temp, ожидаемые байты, повторную проверку inode/
ctime/содержимого и родительского каталога, fsync файла и directory. Сохраняются
владелец, permission bits и xattr/ACL; executable privileges setuid/setgid/capabilities
не копируются. При сбое directory fsync после rename возвращается отдельное
PUBLISHED_UNSYNCED: возможный эффект обозначен явно.

Backup создаёт независимые `.history-*` версии и атомарный latest `.bak`, сверяет
прочитанные байты и исходник. Restore сохраняет текущую версию в history, не
перетирая точку восстановления latest. Удалённый файл восстанавливается с обычными
правами/атрибутами backup. Разбор backup/publish ограничен 64 МиБ.

Детерминированные инъекции через linker wrap проверяют ENOSPC на write, EIO на
fsync, EACCES на openat, внешнюю запись во время публикации и замену родительского
каталога ссылкой. Отдельно проверены symlink backup/target, устаревший expected,
сохранение xattr, сбой copy/move/delete/edit из-за backup и восстановление файла.
A17 остаётся partial: backup ещё внутри workspace, не защищён от самой модели;
нет changeset-журнала/отката хода и retention; move/delete не переведены полностью
на dirfd; остаточная check→rename гонка с внешним несогласованным writer не устранена.

Дополнительно удалён ошибочный fallback `resolve_under`: строковый prefix разрешал
соседний `work-neighbor` для корня `work`, а lexical path возвращался даже при
canonical symlink-escape. Теперь canonical containment обязателен; регрессии
проверяют read/copy/create через `..` и ссылку наружу.

Живой полный прогон на Gemma: 4/5. bugfix, multi_file, recovery и file_integrity
прошли; long_turn сделал 15 из 16 правок, оставил `16 KEEP` и объявил завершение.
Это подтверждённый незакрытый A19, не отказ файловой публикации. Неудачный отчёт
сохранён как `file-publication-live-initial.json`; повтор модели не заменяет
исправление критериев завершения. Следующая приоритетная часть — acceptance/verification.

Итоговая проверка волны 6: `bash scripts/ci.sh` — 29/29, arli-law, qmllint,
лимиты модулей и `git diff --check` прошли. Отдельный live file_integrity на
финальной сборке прошёл (`file-publication-live-integrity-final.json`), не отменяя
ранее обнаруженный long_turn failure A19.


## Волна 7 — явные файловые критерии завершения (A19, частично)

Перед изменяющим ходом отдельный вызов LLM составляет неизменяемый план:
равенство байтов, наличие/отсутствие фрагмента, отсутствие файла или диапазон
строк с шаблоном номера. Перед финальным ответом worker проверяет реальные файлы.
Нарушение вызывает до двух попыток исправления; оставшийся дефект даёт failed.
Done передаёт план, проверенную ревизию и checks_passed/partial/unverified/failed.
Неполная проверка видна в ответе и статусе оболочки.

Детерминированные регрессии проверяют последнюю строку диапазона, инвалидирование
результата после мутации, внешнюю запись, некорректный план, исправляющуюся и
упорно завершающуюся mock-модель. Первоначально упали старые проверки точного
ответа `done`: контракт теперь требует также предупреждение об отсутствии
проверяемых критериев. Они обновлены с проверкой текста и verification_state.

Первый live прогон этой волны не прошёл: Gemma выдала пустой план, ошибочно
отнесла проверку конечного диапазона к непроверяемым требованиям и закончила
после 13 правок. Отчёт acceptance-live-initial.json сохранён. Контракт планировщика
исправлен: проверяемое конечное содержимое выделяется отдельно от непроверяемого
порядка операций; добавлен пример диапазона, не привязанный к live задаче.

Ограничения: план составляет модель и может пропустить требование; успешные
предикаты не доказывают семантическую корректность кода или полноту задания.
Нет декларативных команд проверки, проверки порядка операций и предварительной
долговечной записи плана до первой мутации. A19 не считается полностью закрытым.

После исправления контракта исходный live long_turn прошёл: 16 правок, 33 tools,
6 compactions; line_range охватывает 1..16, disk_correct=true. Статус partial:
порядок операций предикатами не проверяется. В этом живом запуске модель сама
выполнила все строки; принудительное исправление пропуска доказано mock-регрессией,
а не приписывается live запуску. Артефакт: acceptance-live-final.json.

Финальный CI волны 7: 30/30, arli-law, qmllint, module size law и git diff --check прошли.


## Волна 8 — идентичность запуска worker (A12, частично)

Помимо local credentials + internal bearer Session теперь требует случайный
секрет конкретного запуска. Он передаётся в env дочернего worker, предъявляется
через arli::auth metadata, проверяется до занятия Session, сверяется с ожидаемым
PID в Hello и отзывается при reap/kill. После respawn старый секрет не принимается.
Окно fork/регистрация закрыто mutex на стороне parent; child не берёт этот mutex.

Регрессии запускают реальные дочерние процессы: проверяют credential rotation,
отказ старому/пустому/неверному значению и отзыв после kill/reap. Отдельный gRPC тест
с правильным постоянным internal_token и отсутствующим/поддельным launch token
получает UNAUTHENTICATED и сохраняет штатный worker idle. Lifecycle и поколения
Session также прошли. Это не аттестация PID транспортом LOCAL_TCP и не изоляция
от процессов с правом чтения окружения того же пользователя (см. docs/05-secrets.md).

Восстановление исполнения после аварии остаётся открытым A12: незавершённые
побочные эффекты не воспроизводятся автоматически. Существующая политика честно
завершает running задачу ошибкой worker_lost; queued задачи сохраняются.

Live Gemma через реальные daemon/agent прошёл с новым launch credential: reconnect событий без дубликатов, полный restart и следующий ход; worker-identity-live-final.json.

CI волны 8: 31/31, arli-law, qmllint, module size law и git diff --check прошли.


## Волна 9 — supervisor фоновых команд (A21, частично)

Прежний double-fork запускал shell без владельца lifecycle и stop сигналил по
сохранённому PGID. Новый отдельный helper владеет дочерним процессом, держит leader
не reaped до очистки группы, сохраняет exit code, ограничивает лог двумя файлами
по 1 МиБ и переживает worker. Bubblewrap die-with-parent связывает команду с
supervisor. Worker записывает jobs до разрешения начать команду.

Schema 12 хранит boot_id/starttime supervisor и exit_code. Оркестратор открывает
pidfd, затем сверяет идентичность и сигналит через pidfd. При несовпадении, zombie
или старой записи без identity ставится lost; небезопасного kill(PID) fallback нет.
Файл результата атомарно публикуется и синхронизируется до выхода supervisor.
Повторное чтение после обнаружения исчезновения процесса закрывает гонку между
публикацией результата и завершением. После waitid дочитывается последний вывод.
ListJobs(include_finished=true) предоставляет завершённые задания и код выхода.

Открыты: ownership по dialog/task и групповые квоты, retention завершённых каталогов,
постоянная фоновая сверка БД (сейчас ListJobs/StopJob), UI истории завершений и
исчерпывающая матрица аварий supervisor/daemon. Поэтому A21 остаётся partial.


Регрессии jobs прошли на реальных OS-процессах и bubblewrap: exit 7, 4 МБ вывода
с последним маркером, два ограниченных лога, stop после DB reopen, остановка
дочерних писателей, SIGKILL supervisor → lost без выдуманного exit code и без
продолжающейся команды. Чужая/legacy identity не получает сигнал. Дополнительно
StopJob в Qt получил deadline 5 секунд и явное сообщение неподтверждённой остановки.
Это процессные тесты, не проверка принятия решений живой моделью.

CI волны 9: 32/32, arli-law, qmllint, module size law и git diff --check прошли. Authenticated ListJobs(include_finished) подтвердил exit7 и полный лог реальной команды.


## Волна 10 — завершение background, IPC deadline и история

Исправлен статус `команда &`: после успешного async launch теперь учитывается
код выхода последнего асинхронного child, а ошибка завершающего foreground
выражения сохраняется. Это не агрегатор результатов всех shell jobs.
Запуск supervisor получил общий JobChannel: неблокирующие send/recv и единый
IPC deadline. Тест с заполненным send buffer доказывает отсутствие блокирующего
send; медленно поступающие байты не продлевают deadline между вызовами.

Qt запрашивает завершённые задания, показывает код выхода/lost/stopped и скрывает
Stop для terminal rows. Запрос сортирует активные строки перед историей, чтобы
свежие завершения не вытесняли работающие задания из первой страницы.

Live Gemma прошёл: ровно один start_background, approval по gRPC разрешает только printf ready; exit 7, ожидаемые ready/exit7 читаются после полного restart демона. background-live-final.json.

CI волны 10: 33/33, arli-law, qmllint, module size law и git diff --check прошли.


## Волна 11 — устанавливаемый SDK (A22, частично)

Ранее устанавливался arli_embed без зависимых архивов и package config. Теперь SDK
component включает arli_proto, arli_common, optional arli_embed, generated protobuf/
gRPC headers, auth headers и relocatable CMake targets Arli::client/Arli::embed.
Client не требует Qt/SQLite/curl; embed разрешает Qt-зависимости через find_dependency.
До 1.0 version config использует ExactVersion. Source-tree include paths и публичный
ARLI_QML_DIR build path не попадают в exported usage requirements.

check-installed-sdk.sh устанавливает только SDK во временный prefix, переносит его,
копирует внешний CMake consumer и собирает client с отключённым find_package(Qt6),
затем embed. Выполняются разрешённый и запрещённый authenticated fixture RPC.
Первая проверка Qt ошибочно ожидала версию демона до соединения; исправлен тест
создания AgentFacade/metaobject. Повтор прошёл (INSTALLED_SDK_OK).

Это подтверждает поставку/линковку/базовый gRPC, но не закрывает весь A22:
production daemon health/send/resume/cancel из установленного SDK и GrpcToolProvider
для прикладных сервисов ещё нужны. Пакет на систему пользователя не устанавливался.

Финальный CI волны 11: 33/33 и INSTALLED_SDK_OK, arli-law, qmllint, module size law и git diff --check прошли.


## Волна 12 — внешний SDK consumer против production daemon

В install-tree gate добавлен отдельный consumer, собранный только с Arli::client.
Он поднимает изолированные production daemon/worker и deterministic mock LLM,
проверяет Health/ready, Send, разрыв клиентского потока, AttachTurn по task/cursor,
монотонность событий и отсутствие дублей. Повтор Send с тем же request_id не
создаёт вторую задачу. Cancel выполняется после подтверждения running второго хода;
проверяются terminal cancelled и сохранённый статус через ListTasks.

Первый прогон без wait_for_ready в readiness probe не дождался соединения.
Добавлено явное wait_for_ready с deadline; повтор дал SDK_DAEMON_ROUNDTRIP_OK
и INSTALLED_SDK_OK. Consumer не линкует arli_core, не включает внутренние заголовки
и не читает SQLite. Модель детерминированная; gRPC, процессы и аутентификация реальные.
Клиентская acceptance-часть A22 покрыта; GrpcToolProvider остаётся незавершённым.

CI волны 12: 33/33 плюс SDK_DAEMON_ROUNDTRIP_OK и INSTALLED_SDK_OK; arli-law, qmllint, module size law и git diff --check прошли.


## Волна 13 — ядро GrpcToolProvider и агентский цикл (A22, частично)

Добавлен SDK target Arli::grpc_tools: allowlisted unary методы, protobuf-derived
schemas и validation, owned descriptor pools, numeric loopback endpoints,
credentials на метод/сервис, bounded transport, deadline/cancel и mandatory approval
mutations. Payload/config/schema ограничены. Service token не входит в schemas,
его literal echo скрывается, необработанные transport error messages не выдаются.

run_turn принимает optional доверенный provider, публикует его schemas, вызывает
ConfirmHub перед мутацией и пишет обычные Action/Observation. Отказ/ошибка RPC
остаются в verification и не могут завершиться ложным done. Read-only применяется
до approval. Это ещё не конфигурационный путь отдельного worker.

Регрессии на реальном локальном gRPC сервисе: allowlist, args method override,
неверный token, protobuf type mismatch, read-only, deny/accept, deadline, cancel,
invalid registry. Mock agent пытается объявить успех после deny/forbidden — итог
failed. Первая credential регрессия ошибочно использовала публичный Health;
заменена защищённым GetSettings. Первые terminal assertions запустились со старым
object file до последней правки; после пересборки оба набора прошли.

Live Gemma через production run_turn: одно точное approval, один grpc_write,
одна мутация ready в isolated application service; grpc-provider-live-final.json.
Это не daemon/worker config test: through_daemon=false записано в артефакте.
Standalone registry loading/защита конфигурации/delivery к worker и реальные
прикладные контракты Emperion остаются незавершёнными.

CI волны 13: 34/34, SDK_DAEMON_ROUNDTRIP_OK и INSTALLED_SDK_OK (включая вызов через установленный Arli::grpc_tools), arli-law, qmllint, module size law и git diff --check прошли.

### Волна 14 — служебные файлы и изоляция поиска/Git

Native file tools теперь закрывают служебный state, включая canonical symlink
aliases; default state/workspace остаётся доступным. Move/delete каталога-предка
state запрещены, рекурсивные списки скрывают служебное дерево. rg перешёл с host
popen на фиксированный argv в общем read-only sandbox, с `--` перед шаблоном.
Git использует тот же sandbox и очищенное окружение, отключает external diff,
textconv, fsmonitor и hooks. Ограничены время, вывод и сеть.

Новая регрессия проверяет реальные bubblewrap/rg/Git, чтение/мутации секретных
путей, aliases и предка, обычную работу default workspace, option-like шаблон rg,
отсутствие inherited secret в env и запрет записи read-only subprocess.
Первая подготовка Git fixture ошибочно использовала запрещённый shell redirect;
исправлен стенд, политика продукта не ослаблена. CI: 35/35, оба SDK gate и законы.

Первый live file_integrity заблокирован до записи: прежний стенд создавал проект
непосредственно внутри служебного state. Артефакт private-state-live-initial.json
сохранён. Live agent/web fixtures переведены в разрешённый state/workspace.
Глобальная redaction и hardlink-копии вне state остаются за рамками этой волны.
Повторный live Gemma прошёл: private-state-live-final.json, file_integrity=true,
6 tools, 2 записи, 0 ошибок; точные байты и отсутствие лишних файлов проверены.

### Волна 15 — standalone gRPC registry

Worker перед Claim загружает private state/grpc-tools/registry.json; provider
фиксируется на весь turn. Модель не выбирает пути/endpoint/method/token. Registry
имеет строгие поля/version, обязательную mutating классификацию, limits, закрытые
owner-only dir/file permissions, dirfd + no-follow basename reads, запрет hardlinks,
regular-file validation и проверку изменений во время чтения. Отсутствие каталога
отключает provider; неверный существующий реестр завершает task до LLM. Сообщение
ошибки не включает конфигурацию/секреты. Daemon не исполняет RPC приложения.

Регрессии: missing/valid/bad registry, types/path traversal/permissions/symlink/
hardlink, snapshot/reload. Отдельный integration test запускает настоящие daemon,
worker, Session relay и authenticated application service: accept вызывает RPC
один раз, deny не вызывает, corrupt registry останавливает следующее задание.
Конфигурация публикуется после старта worker, что проверяет per-Claim loading.

Live Gemma через тот же путь прошла: grpc-registry-live-final.json,
through_daemon=true, одно exact approval, одна мутация, одно observation, сервисный
токен отсутствует в UI событиях. Реальные прикладные контракты Emperion ещё не
подменены fixture-контрактом и остаются незавершёнными.
CI волны 15: 37/37, SDK_DAEMON_ROUNDTRIP_OK, INSTALLED_SDK_OK, arli-law,
qmllint, module size и git diff --check прошли; /tmp/arli-wave15-ci.log.

### Волна 16 — общая фильтрация известных credentials

Добавлен общий process-local registry с immutable snapshots и ограничением
памяти. Credentials регистрируются при чтении/создании, auth, загрузке/смене ключа
модели и provider registration. Покрыты буквальные и JSON-escaped значения,
ротации внутри процесса. Log sink, audit до truncation, worker TurnEvent/messages,
daemon relay/journal/replay и UI replies используют общую фильтрацию.
Token stream удерживает возможный secret prefix; проверены все split boundaries,
выдача по одному байту и незавершённый prefix на границе. Обычный совпадающий
короткий suffix может быть скрыт; переставлять данные инструмента/аргументы
исполнения фильтр не должен.

Regression daemon+worker+mock проверяет echo ключа и обоих токенов, UI историю,
trace, task text, audit/log файлы и смену ключа без restart. Unit test проверяет
JSON escapes, audit truncation, concurrent registrations и durable events.
Начальная попытка тестировать AgentService через arli_core не линковалась:
сервис находится в daemon; проверка вынесена в реальный process integration.
В unit fixture сначала отсутствовал task для FK events; создана настоящая task.

A01 остаётся partial: старые unsanitized записи после rotation+restart требуют
отдельной миграции/санитизированных представлений. Неизвестные и преобразованные
секреты не распознаются. Текущая волна не утверждает защиту от произвольного
кодирования секретов или другого процесса того же UID.
CI волны 16: 39/39, оба installed SDK gates и все законы прошли;
/tmp/arli-wave16-ci.log. Live Gemma normal gRPC workflow: redaction-live-final.json,
through_daemon=true, одно exact approval/мутация, passed. Проверки раскрытия
синтетических credentials выполнены deterministic тестами, не live adversarial
оценкой модели.

### Волна 17 — отпечатки после rotation/restart

Daemon/worker включают private state/redaction.fingerprints до загрузки credentials.
Ledger содержит length + rolling index + SHA-256, без исходного ключа. No-follow,
owner/private permissions/link-count/regular-file validation, межпроцессный flock,
fsync, bounded records/bytes; неполный append/небезопасный ledger закрывают выдачу,
а проверка перед event прекращает turn. Удалённый активный ledger не пересоздаётся
при фильтрации. OpenSSL Crypto добавлен в build/RPM/installed SDK dependencies.

Независимый exec процесса без remember(old_key) проверяет full и побайтовый stream.
Реальные daemon/worker меняют key, перезапускаются, возвращают старый title/history
и replay исходного request_id без раскрытия и без изменения request semantics.
Результат read_file теперь фильтруется до LLM/read guard/observation; mock проверяет,
что старый ключ из файла после restart не появился в tool message модели.

Первый полный CI 40/40 прошёл, но live Gemma выявила разрезание UTF-8 новым
буфером: protobuf отверг Token, Session оборвалась после одной успешной мутации.
durable-redaction-live-initial.json сохранён как failed. Выпуск текста исправлен
на целые UTF-8 символы; добавлена проверка кириллицы/emoji с каждым размером chunk,
проверкой валидности каждого emitted chunk и точного результата.

Неизвестные прежние ключи, удалённые до внедрения ledger, нельзя восстановить.
Отдельная остающаяся поверхность A01 — raw log files фонового supervisor; общий
UI фильтр не заменяет фильтрацию самого файла лога до записи.
Повтор исходного live-сценария после UTF-8 исправления прошёл:
durable-redaction-live-final.json, through_daemon=true, одно approval, одна
мутация, одна observation, done. Credential assertions по restart/tool-result
проверяются deterministic mock, а live подтверждает обычный рабочий путь.
Финальный CI волны 17: 40/40, оба installed SDK gates с OpenSSL Crypto,
arli-law, qmllint, module size и git diff --check прошли;
/tmp/arli-wave17-ci-final.log.

### Волна 18 — фоновые логи до диска

Supervisor подключён к общему durable redaction по доверенному пути launcher,
без передачи plaintext credentials или наследования env worker. JobOutput
фильтрует stdout/stderr до записи, ограничивает две ротации 1 MiB каждая и не режет
UTF-8 символы между файлами. При normal exit финальный безопасный хвост сбрасывается;
при forced stop withheld tail отбрасывается. Короткий последний вывод может ждать
следующего chunk/exit. Это осознанная задержка для fingerprint-only streaming.

OwnedChild guard убивает группу/leader и reap-ит child на исключениях filtering/I/O;
результат не выдумывается, job становится lost. Проверка открытия и rotation
fail-closed вместо продолжения без лога. Пути ledger нормализованы до absolute,
чтобы допустимый относительный ARLI_STATE_DIR не ломал повторное подключение.

Реальные процессы проверяют отсутствие env credential, split stdout/stderr,
незаписанный secret prefix, точную кириллицу/emoji, обе ротации без marker,
normal final tail, stop посередине секрета. Torn ledger во время живой команды
останавливает heartbeat child, даёт lost и не раскрывает pending output.
Targeted jobs/jobs-redaction/audit-regression прошли.
Live Gemma background-redaction-live-final.json прошла: одно exact approval/start,
ready/exit7 сохраняются после daemon restart. CI волны 18: 41/41, оба SDK gates и
все законы прошли; /tmp/arli-wave18-ci.log. A01 переведён в implemented в границе
известных credentials и инструментальных процессов. Неизвестные/преобразованные
секреты, другой процесс того же UID и старые raw logs до исправления не входят
в заявленную гарантию.

### Волна 19 — принадлежность фоновых jobs

Schema 13: dialog_id/task_id/turn_id в jobs, default 0 для legacy записей и индекс
owner. Значения приходят из Claim/контекста run_turn, не из tool arguments. DB
проверяет dialog/task/turn/running и повторяет условия в самом INSERT SELECT,
чтобы не опираться на устаревшую предварительную проверку перед supervisor GO.
Standalone вызовы с task_id=0 сохраняются. Historical owner IDs не имеют cascade
на удаление диалога: фоновые процессы продолжают управляться из global list.

Proto только расширен: ListJobs filters до LIMIT, JobInfo owner IDs, optional
expected owner для StopJob. По умолчанию сохраняется global/legacy поведение.
Mismatch возвращает ok=false без сигнала; это consistency check, не multi-user
access control. Qt jobs строка показывает исходные dialog/task.

Регрессии: реконструированная реальная схема jobs v12 → v13 без потери старых
строк, owner после reopen, фильтр до limit среди 60 чужих записей, неверный/terminal
task не может создать job. Настоящие daemon/worker/mock проверяют owner из Claim,
wrong-owner StopJob без сигнала, correct-owner stop и сохранение после restart.
Targeted job-ownership/job-ownership-daemon/claim-delivery/jobs прошли.
Live Gemma job-ownership-live-final.json прошла: одно exact approval/start,
owner_verified=true (dialog/task/turn из Send), owner и ready/exit7 сохранены после
daemon restart. Quotas/retention/background reconciliation остаются A21 partial.
CI волны 19: 43/43, оба installed SDK gates, arli-law, qmllint,
module size и git diff --check прошли; /tmp/arli-wave19-ci.log.

### Волна 20 — job quotas и независимая сверка

Добавлены global/dialog/task running quotas 16/8/4. Preflight выполняется до fork
и создания job directory, атомарный COUNT — в INSERT SELECT перед supervisor GO.
Unowned rows не обходят global quota. Завершение освобождает capacity.
JobMaintenance в daemon обходит по 64 running jobs раз в секунду с cursor; не
зависит от ListJobs, окна UI или медленного LLM health. Result/identity обновляют
exit/lost без сигналов старому PID. Добавлен partial running index без смены схемы.

Quota test: per-task/per-dialog/global, unowned bypass rejection, release capacity,
32 независимых SQLite connections принимают ровно 16 rows, rejected launch не
создаёт directory и не выполняет command. Maintenance test использует настоящий
daemon/worker/supervisor, не вызывает ListJobs/refresh_job и читает БД напрямую:
exit7 фиксируется без UI, чужой identity получает lost без сигнала, offline exit3
подхватывается после restart. Ownership fixture создаёт чужую историю terminal,
чтобы проверять filter-before-limit и не нарушать новую running quota.
Targeted 5/5 прошли. Общая retention terminal rows/logs остаётся открытой.
Live Gemma job-maintenance-live-final.json прошла с включёнными квотами/maintenance:
одно approval/start, owner verified, ready/exit7 после restart. Финальный CI:
45/45, оба SDK gates и законы прошли; /tmp/arli-wave20-ci.log.

### Волна 21 — ограниченная история terminal jobs

Schema14 добавляет finished_at с однократным переходом running → terminal;
миграция ставит старым terminal rows текущее время. Additive JobInfo.finished_at
и Qt пояснение политики. Daemon maintenance при старте / каждые 30 секунд
обрабатывает до 32 кандидатов: последние 128 terminal rows, максимум 7 дней.
Активные rows и живая process identity сохраняются. Безопасная очистка только
state/jobs/job-XXXXXX через directory FD/O_NOFOLLOW: четыре известных файла,
посторонние entries сохраняют каталог, symlink каталога отвергается, symlink
файла удаляется без обращения к target. Row удаляется после файлов; повтор после
частичной очистки допустим. Небезопасные/недоступные entries могут превышать cap.
Ошибка launch до регистрации row очищает приватный каталог без исполнения GO.
Принудительный kill в этом окне ещё может оставить orphan directory: A21 partial.

Targeted 6/6: возраст/count, повторное завершение без продления, active/live
identity, external path, directory/file symlinks, unknown entries, частичная
очистка и pre-GO отказ без side effects. Actual daemon restart удаляет старый
log/row без ListJobs/UI. Live job-retention-live-final.json: настоящая Gemma,
одно точное approval/start, ready/exit7/owner/finished_at после restart прошли.
Возрастная очистка проверяется детерминированно, не решением LLM.
CI волны 21: 46/46, оба installed SDK gates, arli-law, qmllint,
module size и git diff --check прошли; /tmp/arli-wave21-ci.log.

### Волна 22 — orphan jobs и аварийные границы GO

Launcher держит flock jobs directory до регистрации/GO, supervisor держит flock
job directory до выхода. Cleanup использует неблокирующую блокировку, не удаляя
логи ещё живого supervisor. Обычная ошибка до регистрации закрывает канал и
ожидает освобождения каталога до одной секунды; SIGKILL оставляет очистку daemon.
OrphanJobs сохраняет directory cursor между проходами: до 64 entries каждые
30 секунд, grace 60 секунд, только незарегистрированный приватный job directory
с известными файлами. Индексы log_path / suffix защищают и старые relative/alias
rows; совпадение suffix консервативно сохраняет каталог. Нет recursive remove
или обхода directory symlink. Registered terminal rows остаются normal retention.

Targeted 5/5 прошли. Новая OS regression использует SIGKILL launcher до helper,
настоящий supervisor R→GO без row и с row, отсутствие command side effect,
освобождение lease ядром, honest lost/-1 для зарегистрированной незапущенной
команды, сохранение recent/unknown/relative-path rows, 140 orphan dirs через
несколько bounded страниц. Дополнительно production start_background после GO:
worker-like launcher убит, heartbeat продолжается, sweep сохраняет job, pidfd stop
останавливает дерево. Actual daemon startup/restart подбирает old orphan без UI.
Это crash injection на реальных процессах; LLM решения не подменяются этим тестом.
Live job-orphans-live-final.json прошёл на настоящей Gemma через daemon/worker:
одно exact approval/start, ready/exit7/owner/finished_at после restart. A21 остаётся
partial: исходный аудит требует streaming bounded output, а текущий UI получает
bounded tail через ListJobs polling без позиции/возобновления отдельного job log.
Crash/retention границы этой волны не подменяют это оставшееся требование.
CI волны 22: 47/47, оба SDK gates, arli-law, qmllint, module size
и git diff --check прошли; /tmp/arli-wave22-ci.log.

### Волна 23 — возобновляемый gRPC job log

Additive WatchJobLog/JobLogChunk: signed opaque position привязана к job и
statx device/inode/birth time + byte offset; HMAC-SHA256 внутренним токеном.
Cursor переживает daemon restart, отвергает подделку/чужую job. При утрате
ротации приходит gap=true с самой старой доступной позицией. Reader открывает
NOFOLLOW два bounded поколения, проверяет owner/nlink/regular/permissions/size,
проверяет стабильность snapshot при rename и не удерживает бесконечную историю.
FS без birth time не поддерживает этот RPC (явный FAILED_PRECONDITION), старый
ListJobs сохраняется. Смена internal token инвалидирует старые позиции.

Redaction Stream сообщает размер удерживаемых raw bytes: next_position
подтверждает только уже обработанные байты, без потери хвоста при reconnect.
Фильтр переносит secret/UTF-8 между chunks и generations. Raw read 8 KiB,
максимальный ответ 1 MiB с учётом расширения масок и final flush. До 8 подписок,
15-секундная lease отменяет и blocked Write даже без deadline клиента. Нет
producer→client неограниченной очереди; Cancel наблюдения не останавливает job.

Первый targeted job-stream прошёл: actual daemon restart/resume без повторов,
UTF-8/secret boundary, authenticated owner check, tamper/cross-job/unauth reject,
one-rotation continuation, expired generation gap, secret across generations,
eight slow readers + responsive Health, deadline и server lease expiration.
Финальный тест усилен: клиент без deadline 16 секунд вообще не читает сообщения.
Qt подключение нового RPC пока не реализовано; A21 остаётся partial до него.
Live job-stream-live-final.json: настоящая Gemma один раз запустила exact approved
command; после restart WatchJobLog вернул streamed=true, ровно ready, terminal
exit7 без gap, owner проверен. Этот live подтверждает нормальный полный процесс;
rotation/tamper/slow-reader assertions выполняются реальными deterministic RPC.
CI волны 23: 48/48, оба SDK gates, arli-law, qmllint, module size
и git diff --check прошли; /tmp/arli-wave23-ci.log.

### Волна 24 — Qt job log inspector

JobLogWatch — отдельный поток чтения, не очередь основного хода/control. Qt
хранит применённую позицию, reconnect/backoff, bounded 65 536 UTF-16 tail без
разрыва surrogate pairs. Desk объединяет updates в один queued snapshot;
selected generation защищает инспектор от запоздавших сообщений после switch.
GUI stop/switch не join-ят сеть. Shutdown отменяет ClientContext и завершает thread.
QML открывает результат при выборе job, защищает его от automatic trace follow,
даёт кнопку detach и отключает подписку при скрытии инспектора. Завершение/gap/
обрезка/legacy fallback явно видны. Invalid cursor сбрасывает предыдущий текст
и перечитывает с предупреждением, не склеивая дубли. Transient rotation snapshot
теперь UNAVAILABLE, чтобы клиент переподключался, а не считал FS неподдерживаемой.

Первый targeted job-log-ui passed: настоящий AgentFacade/QGuiApplication и
authenticated gRPC fixture проверяют позицию после reconnect, stale update fencing,
немедленный switch/detach, responsive StopJob, cancellation на сервере, legacy
fallback, bounded coalesced Unicode tail и destruction с активным stream.
Финальная версия теста также проверяет invalid cursor reset; QML render test
проверяет выбор result tab и кнопку detach. Это реальные Qt/RPC, ответы сервиса
детерминированные; не выдаётся за модельный UI eval.
Первый полный CI: 48/49; shell-ui поймал onVisibleChanged раньше required desk
(TypeError stopJobLog of undefined). Добавлена проверка desk перед cancel;
финальный targeted job-log-ui + shell-ui 2/2 прошёл. Исходный failure сохранён в
/tmp/arli-wave24-ci.log. Live job-log-ui-live-final.json: actual Gemma и daemon/
worker/gRPC/supervisor, exact approved command, restart, streamed ready/exit7;
Qt не участвует в этом live executable, его поведение проверено отдельными
настоящими Qt/RPC и QML render tests.
Финальный CI волны 24: 49/49, оба SDK gates, arli-law, qmllint,
module size и git diff --check прошли; /tmp/arli-wave24-ci-final.log.
A21 закрыт по исходной приёмке: persisted ownership/exit codes, bounded logs и
поток, безопасные identity/pidfd stop и cancel дерева, restart/crash/reuse-PID,
retention и Qt consumption покрыты приведёнными регрессиями. Ограничение statx
birth time и legacy fallback явно документировано; полный аудит остаётся открыт.

### Волна 25 — резервные копии вне workspace

Backup/restore переведены в state/backups с SHA-256 namespaces canonical workdir
и relative path. latest/history публикация остаётся atomic/verified с metadata;
хранилище открывается без directory symlinks, backup ancestors owner/private,
state root owner/no foreign write. Одинаковые имена в разных проектах не смешаны,
canonical alias использует ту же историю, 255-byte filename поддерживается.
Модель не может прочитать/изменить/удалить копию через file tools, symlink alias
или shell sandbox. Legacy .arli-bak не удаляется и не считается доверенной:
restore без private snapshot отказывает; автоматического импорта нет.
CMake задаёт отдельный ARLI_STATE_DIR для каждого CTest, чтобы regressions новых
backup tools не записывали пользовательское состояние. Existing explicit test
state overrides продолжают работать. UI delete_file обещает protected backup,
delete_dir честно сообщает отсутствие backup дерева (A17 всё ещё partial).

Targeted 6/6: backup-store, file-publication, syntax-check, tools-tier и mutation/
write guards. Проверены independent projects/alias, private permissions, model
read/write/delete denial, настоящий bwrap, legacy refusal, latest symlink,
максимальное имя, ENOSPC/EIO/EACCES, parent swap, verified history/latest и
restore mode/xattr. Ошибка сборки нового теста (не хватало unistd.h) исправлена.
Changeset journal, rollback хода с конфликтами, retention, полные dirfd move/delete
и оставшаяся check→rename гонка не закрываются этим переносом.
Live protected-backup-live-final.json: настоящая Gemma через production run_turn,
file_integrity passed (6 tools/2 writes/no errors). Отдельная disk verification
подтвердила исходные large.txt/empty.txt bytes в private latest/history, права
каталогов и отсутствие новой .arli-bak в workspace. Это не UI/daemon model eval.
CI волны 25: 50/50, оба SDK gates, arli-law, qmllint, module size и
 git diff --check прошли; /tmp/arli-wave25-ci.log. A17 остаётся partial.

### Волна 26 — durable intent/outcome для публикаций файлов

run_turn задаёт thread-local MutationScope с trusted dialog/turn/task IDs.
Atomic publisher (edit/append/create/copy/restore) перед записью сохраняет
private before snapshot и fsynced prepared manifest с before/after SHA-256,
relative path, sequence/ownership. После фактического rename/link сохраняется
applied/failed/uncertain. Journal writes и snapshot publication не попадают в
собственный журнал; read-only scope не создаёт каталог. Новый attempt с теми же
IDs не перезаписывает старый. Лимиты 4096 entries / 4 MiB manifest.

Prepare failure не меняет workspace. Commit failure возвращает
MUTATION_UNCERTAIN, сохраняет durable intent и блокирует дальнейшие записи
этого scope. Publisher проверяет bytes, снятые при prepare, перед заменой.
Targeted 5/5 прошли. Новая regression проверяет hash/snapshot/create semantics,
private access, reused IDs, ENOSPC prepare, commit failure after actual rename,
реальный process exit до/после rename с discoverable prepared record. Финальный
CI также включает foreign write во время prepare и отказ второй правки после
успешной первой: исходники сохранены и outcomes различимы.

Это ещё не готовый revert: RPC/UI отката, replay/recovery сверка prepared,
move/delete, прямые shell mutations и changeset-wide rollback не подключены.
Residual check→rename race и retention остаются A17 partial.

Первый CI прошёл 51/51, но живой запуск Gemma выявил два дополнительных дефекта:
file_stat считал строки форматированного read_file (header/truncation), а
rg_search по обычному файлу принимался rules loader за недоступный каталог.
Первый неудачный отчёт сохранён в mutation-journal-live-initial.json: диск уже
был корректен, но ход завершился с RULES_ERROR. file_stat теперь считает raw
bytes (до 64 MiB; выше явно unknown), сообщает final_newline; regular-file
scope пропускается без отключения проверки родительских правил и symlinks.
Добавлены регрессии empty/no EOF/CRLF/2000 lines, nested file search и symlink.

Финальный CI: 51/51, оба SDK gates, arli-law, qmllint и module size прошли
(/tmp/arli-wave26-ci-final.log). Повтор того же live prompt на настоящей Gemma:
mutation-journal-live-final.json, 6 tools, 2 writes, no errors, exact disk bytes.
Отдельно проверены два applied entry, before/after SHA-256, исходные snapshot
bytes и private directories. Это production run_turn напрямую, не Qt/daemon.

### Волна 27 — сверка журнала после аварии

Worker library inspect_mutations проверяет private storage/manifest, ID,
workspace/owner fields, bounded entries, canonical relative paths, snapshot
namespace и SHA-256 оригинала. Active MutationScope держит directory flock;
инспекция активного attempt отклоняется, crash освобождает lease через ОС.
Никакие файлы workspace и исходные prepared/applied phases не меняются.
Последняя не-failed запись по пути сверяется с текущими bytes: before, after,
unchanged (одинаковые before/after), conflict или unavailable. Ранние записи
superseded, failed — not_published. Это evidence для будущего recovery/revert,
а не утверждение, что совпадение bytes доказывает исполнение операции.

Регрессии: реальный child exit до/после rename и последующая сверка, active
lease rejection, испорченная копия, чужие изменения, отсутствующий create,
symlink, traversal в manifest, повторные правки одного файла, no-op и
неизменность исходного журнала. Targeted mutation-journal прошёл.
Полноценный откат и RPC/UI ещё впереди; A17 остаётся partial.

Финальный CI волны 27: 51/51, SDK_DAEMON_ROUNDTRIP_OK, INSTALLED_SDK_OK,
arli-law, qmllint, module size и git diff --check прошли;
/tmp/arli-wave27-ci.log. Первоначальная ошибка компиляции сравнения std::string
с JSON исправлена явным get<string>(). Live mutation-recovery-live-final.json:
настоящая Gemma, direct run_turn, 5 tools/2 writes/no errors; disk hashes и
snapshots отдельно проверены. Аварийная сверка проверяется настоящими child
process regressions, не выдаётся за выполненный моделью/UI откат.

### Волна 28 — журналируемый откат файлов

Worker revert_mutations проверяет dialog owner, держит exclusive source lease,
сворачивает повторные правки по пути и отказывает при разрыве цепочки. Current
bytes, не совпадающие с ожидаемым результатом (или before неопределённой записи),
не трогает. Восстанавливает ранний snapshot с metadata, убирает созданные файлы
через checked removal с private before snapshot и fsync. Delete journal entries
расширяют внутренний формат after_exists=false. Сам откат получает отдельный
журнал с reverts ID; исходные phases остаются неизменны. Per-file результат
честно сообщает частичный успех. Повторный откат и undo-of-undo поддержаны.

Регрессии покрывают повторные правки, created file deletion/recreation, чужой
файл рядом с откатываемым, другой dialog, повторный запуск, process exit после
реальной публикации, ENOSPC при подготовке отката и EACCES второй правки.
Остаточная check→mutation гонка, same-file merge, UI/RPC и approval удаления,
полное move/delete tree tracking/retention остаются открыты; A17 partial.

Финальный CI волны 28: 51/51, SDK_DAEMON_ROUNDTRIP_OK, INSTALLED_SDK_OK,
arli-law, qmllint, module size и git diff --check прошли;
/tmp/arli-wave28-ci.log. Live mutation-revert-live-final.json: настоящая Gemma
выполнила 6 tools/2 writes без ошибок, после чего production worker-library
revert вернул complete=true. revert_verified=true подтверждает точные исходные
bytes обоих файлов (CRLF/NBSP/no EOF и пустой файл). IDs исходного и undo журнала
сохранены в отчёте. Этот live тест не заявляет пока не реализованный UI/RPC откат.

### Волна 29 — очередь и gRPC для отката

Additive RevertChangeset RPC + Claim field + Hello capability. Отдельная
операция хранится в immutable config snapshot задачи, исполняется worker без
LLM/registry. Старые workers fail closed. Обязательный request_id дедуплицируется
через общую durable tasks/events систему; Send и Revert не могут присвоить
операцию друг друга. Endpoint модели для revert не используется. Existing
read_only остаётся ограничением. Worker проверяет owner, запрашивает обязательное
подтверждение, записывает сообщения и Observation/Done; отмена проверяется между
файлами. Оркестратор по-прежнему не исполняет file tools.

Новый actual daemon/worker test использует недоступный LLM endpoint и отдельный
journal fixture: auth/owner, deny/cancel, approve replace+delete, dedup/restart и
cross-operation request collision. Qt-кнопка/discovery остаются следующим этапом.

Финальный CI волны 29: 52/52, SDK_DAEMON_ROUNDTRIP_OK, INSTALLED_SDK_OK,
arli-law, qmllint, module size и git diff --check прошли;
/tmp/arli-wave29-ci.log. Targeted 2/2; финальный test дополнен read_only и
invalid ID, unit — отменой между файлами и последующим retry. Live
revert-queue-live-final.json: actual Gemma direct run_turn, 6 tools/2 writes,
no errors, revert_verified=true. RPC проверен отдельно настоящими daemon/worker
с недоступной моделью. Qt initiation/discovery ещё не реализованы.

### Волна 30 — выбор и запуск отката из Qt

Additive Done.changeset_id связывает завершённый ход с private mutation journal;
обычный worker и deterministic revert публикуют его. Existing trace JSON/events
сохраняют поле без смены семантики. Desk получает ID из Done в GetTurnTrace,
TurnDrawer показывает кнопку, AgentFacade проверяет selected ID/busy/read_only,
не стирает draft и запускает RevertChangeset. Stream reconnect сохраняет request
ID/event cursor. Confirmation имеет отдельную метку и bounded preview путей с
восстановлением/удалением. Late trace response проверяется по dialog/turn;
исправлено повторное открытие скрытых шагов. Facade header остаётся 300 строк.

Новый настоящий Qt facade/authenticated RPC fixture проверяет historical ID,
legacy отсутствие кнопки, selection/read_only guard, неблокирующий вызов,
разрыв стрима/reconnect с тем же request ID и cursor, control approval и draft.
QML render test добавляет кнопку и disabled state. Recovery discovery для хода
без Done пока не подключён; полная A17 приёмка ещё не закрыта.

Финальный CI волны 30: 53/53, SDK_DAEMON_ROUNDTRIP_OK, INSTALLED_SDK_OK,
arli-law, qmllint, module size и git diff --check прошли;
/tmp/arli-wave30-ci.log. Targeted 3/3. MutationScope больше не выдаёт ID
несуществующего manifest после отказа initial prepare.

Live revert-qt-live-final.json: настоящая Gemma через production daemon/worker
и offscreen Qt AgentFacade исправила calc.py, после daemon restart Qt получил
changeset из исторического Done, показал обязательное подтверждение с calc.py,
запустил RevertChangeset и проверил точные исходные bytes. Undo journal виден в
истории второго хода. Независимая SQLite проверка подтвердила status=done обеих
задач. Live обращается к методам facade; сам QML button click отдельно проверен
render-тестом, не выдаётся за ручное управление окном.

### Волна 31 — аварийный journal discovery и исторический workspace

Worker публикует bounded private locator до первой правки. Daemon читает только
metadata catalog и добавляет отдельный changeset TraceItem для terminal task
без соответствующего Done ID. Индекс включает task/dialog/turn, закрыт от
symlinks/foreign ownership, ограничен 64 attempts, активные leases не выдаёт.
Ошибки каталога не разрешают workspace mutation; повреждение отражается в trace.
Предыдущие неиндексированные журналы автоматически не мигрируются.

Исправлена привязка исторического отката: source_turn_id выбирает сохранённый
workdir исходной задачи вместо текущих настроек. Текущая policy/read_only не
ослабляется. Source IDs доставляются в Claim и проверяются worker до approval,
поддержка согласуется Hello capability. TaskCard вынесен из Inspector и открывает
шаги/откат без необходимости наличия assistant message после crash.

Targeted 6/6: catalog symlink/owner/active/quota/fail-before-write, реальный child
exit без Done/destructors, actual daemon startup reconciliation, discovery,
смена settings.workdir и откат старого workspace, existing revoke/retry/Qt/QML.
Финальный тест дополнен отказом подменить source task. Ошибка компиляции UI
fixture (unique_ptr вместо .get()) исправлена до запуска тестов.

Повторная сверка исходной приёмки A10: существующий settings snapshot сохраняет
строку workdir, без canonical/device/inode identity. Он покрывает смену настроек,
но не доказывает защиту от подмены alias/root каталога между enqueue и Claim.
A10 возвращён в partial; воспроизведение и фиксация filesystem identity —
следующая волна. Это отдельный вопрос от исправленного исторического отката.

Финальный CI волны 31: 55/55, SDK_DAEMON_ROUNDTRIP_OK, INSTALLED_SDK_OK,
arli-law, qmllint, module size и git diff --check прошли;
/tmp/arli-wave31-ci.log. Live crash-catalog-qt-live-final.json: actual Gemma,
daemon/worker/offscreen Qt facade, restart и смена текущей рабочей папки;
подтверждённый откат восстановил только исходный проект. SQLite отдельно
подтверждает обе задачи done и source task/turn/workdir в snapshot отката.
Сам crash без Done проверяется детерминированным actual child/daemon тестом,
а не выдаётся за аварию во время этого живого модельного прогона.

### Волна 32 — queued workspace identity

Snapshot теперь закрепляет canonical path, existing anchor и statx device/
inode/birth time. Claim переносит versioned metadata, Hello согласует поддержку.
Worker до model/registry/tools удерживает и проверяет root descriptor; отсутствующие
каталоги создаёт относительно pinned ancestor без новых symlink переходов.
Повторные проверки защищают границы rounds/tools/acceptance и файлов отката.
Manifest сохраняет actual root identity, включая проект, созданный после enqueue;
исторический revert проверяет и его. Legacy metadata не восстанавливаются задним
числом, unsupported statx FS дают явный отказ. Residual syscall races — A17.

Добавлены unit cases alias retarget, existing root replacement, normal child
changes, absent workspace, replaced ancestor и late symlink; persistence и
старый worker без capability. Actual daemon/worker/mock queue test занимает
worker первой задачей, меняет alias/root после enqueue второй и проверяет
каноническую привязку либо отказ WORKSPACE_CHANGED.

Проверено: targeted 7/7 и полный `scripts/ci.sh` 57/57, external installed
SDK roundtrip, arli-law, qmllint, module-size law, `git diff --check`.
Первичная сборка нового теста потребовала missing JSON include; предупреждение
GCC16 на временном argv vector устранено именованным vector, финальная сборка
без этого предупреждения. `/tmp/arli-wave32-ci.log` завершён с CI_OK.
`workspace-identity-qt-live-final.json`: настоящая запущенная Gemma через
daemon/worker/offscreen Qt facade — правка, restart, смена текущего проекта,
подтверждение и точный откат. Отдельно SQLite и manifest подтверждают обе задачи
done, source task/turn binding и сохранённую идентичность исходного проекта.
Подмена root проверяется отдельным deterministic actual-process тестом.
A10 закрыт в границах queue/admission/boundary identity; A17 остаётся partial.

### Волна 33 — durable acceptance before dispatch

Worker записывает неизменяемый acceptance_plan с SHA-256 в private manifest
и fsynced task catalog до инструментов явно изменяющего задания. Для остальных
ходов с разрешённой записью первый вызов инструмента также проходит этот gate,
чтобы shell, HTTP и gRPC-provider не зависели от эвристики file-tool classification.
Повторный gate идемпотентен; изменение критериев или сбой записи отравляет scope
и запрещает последующие вызовы/правки. Неполный/пустой план сохраняет явное
unverifiable, а не получает статус успешно проверенного результата.

Catalog reader проверяет ownership, bounded private manifest и хеш критериев.
GetTurnTrace показывает исходные критерии после terminal task/crash без Done.
Критерии без файловых изменений не создают кнопку отката; после первой файловой
мутации тот же manifest связывает критерии с snapshots/outcomes. Legacy journals
без acceptance остаются читаемыми, отсутствующие критерии не реконструируются.
SQLite plans по-прежнему пишет только оркестратор; worker пишет private artifact.

A19 остаётся partial: полнота модельного плана и декларативная проверка команд/
порядка действий не закрыты сохранением критериев. Хеш служит контролем целостности
метаданных, а не подписью против другого процесса того же UID.

Первый полный CI выявил regression: GetTurnTrace уже дополнял историю criteria,
а OpenDialog.last_trace — нет. OpenDialog теперь использует тот же путь выдачи
trace, существующий parity test проходит. Отдельный job-stream test обнаружил
зависимость fixture от случайного токена: хвост из `b` совпал с началом
internal_token и штатно маскировался как незавершённый prefix. Fixture завершён
newline; redaction unit отдельно фиксирует conservative one-byte prefix masking
и сохранение обычной завершённой строки. Production redaction не ослаблялся.
Исходный неуспешный `/tmp/arli-wave33-ci.log` сохранён.

Финальная проверка: targeted 5/5, затем regressions 2/2 и полный CI 57/57;
external SDK, arli-law, qmllint, module-size law, git diff --check прошли.
`/tmp/arli-wave33-ci-final.log` содержит CI_OK.
`durable-acceptance-qt-live-final.json`: настоящая Gemma, daemon/worker/offscreen
Qt facade, правка/restart/смена проекта/подтверждённый точный откат. Независимо
проверены SQLite done для обеих задач, SHA-256 acceptance и равенство plan
в manifest и terminal Done. Один критерий плюс unverifiable дали честный partial.
Порядок pre-dispatch и crash survival проверяются отдельными deterministic tests.

### Волна 34 — procedural acceptance

TaskAcceptance добавляет command_succeeds (точная явно заданная команда,
успешный foreground run_shell на текущей revision) и read_after_each_write
(полный read_file указанного файла после каждой успешной файловой правки,
до следующей мутации). Команды не запускаются самим verifier: все обычные
policy/approval/sandbox ограничения остаются на dispatch path. Background launch
не считается завершённой проверкой. Файловая/gRPC мутация инвалидирует прежний
command check; отказ точной команды запоминается на ход, повторного prompt нет.

ReadCoverage передаёт внутренние typed first/last/total и SHA-256 bytes из
read_file. Страницы разных версий не суммируются; пустой файл поддержан.
Успех/текст модели не заменяет coverage. До следующей файловой/shell/gRPC mutation
pending reread выдаёт ACCEPTANCE_ORDER. Gate не исполняет инструмент за модель.
План готовится также для tool/command tasks без правок; final evaluation больше
не зависит от mutation_ok, поэтому пропущенная команда блокирует done.

Это расширение ограниченных декларативных критериев, а не доказательство
полноты модельного плана или смысловой корректности программы. Произвольные
процессы/внешние изменения и общий порядок действий пока не выражены этими двумя
предикатами; они требуют honest unverifiable. Crash resume исполнения не добавлен.

Живой initial выявил конфликт creation policy с явно запрошенным test script:
«Других файлов не создавай» блокировал весь shell. Исправлено без ослабления
ограничения workspace: creation gate допускает shell лишь при гарантированном
read-only dispatch, worker принудительно включает ro-bind и отключает unsandboxed
fallback. Approval остаётся обязательным по обычной policy; карточка объясняет
read-only режим. Actual shell regression подтверждает успешный harmless script
и EROFS/отсутствие файла при попытке touch из script.

Первый CI выявил ту же случайную prefix-redaction коллизию в оставшихся rotation
fixtures; все сравниваемые завершённые текстовые записи теперь имеют newline,
production filter не изменён. Initial CI/live reports сохранены. Повторный live
выполнил все три действия без tool errors, но отдельный eval первоначально
слишком строго требовал checks_passed: модель честно указала сохранность прочих
файлов как unverifiable. Eval теперь независимо проверяет точные bytes скрипта,
результат и полный состав workspace, сохраняя partial в отчёте; три обязательных
предиката в model plan по-прежнему требуются, как и фактический shell output.

Финальное доказательство: `/tmp/arli-wave34-ci-final.log` — CI_OK, 58/58 tests,
external installed SDK roundtrip, QML/law/module checks; git diff --check clean.
`procedural-acceptance-live-final.json` — настоящая Gemma через daemon/worker/
authenticated gRPC: 4 tools, 1 edit, 1 exact approval, no tool errors. SQLite
подтверждает done и порядок read/edit/read/run_shell, manifest — исходные три
критерия и корректный SHA-256. Проверочный script реально вывел success marker;
отдельный disk verifier подтвердил сохранность script и ровно два исходных файла.
Model verification_state остаётся partial для preservation restrictions.

### Волна 35 — task and turn-stream admission

SQLite enqueue использует один INSERT…SELECT: общий максимум 64 queued/running
задачи и 8 MiB суммарных UTF-8 bytes text+config_json. Text и immutable snapshot
по отдельности ограничены 256 KiB. Условие внутри записи предотвращает гонку между
независимыми connections; terminal rows освобождают active budget. История этим
не удаляется. Send и RevertChangeset возвращают RESOURCE_EXHAUSTED с именем лимита
до insertion; повтор существующего request_id не проходит новый enqueue gate.

Общий пул 16 turn streams резервируется до enqueue для Send/RevertChangeset,
также применяется к AttachTurn. Slot освобождается при завершении серверного RPC,
не обязательно одновременно с локальным client cancellation. Watchdog ограничивает
именно заблокированный writer->Write десятью секундами; ожидание модели/оператора
не ограничивается новым таймером. Отмена такого потока не отменяет задачу и не
меняет durable event cursor. Health/Cancel и worker Session не входят в этот пул.

Targeted проверяет 128 конкурирующих enqueue через 16 connections, byte/snapshot
limits, reopen/revert, release. Actual daemon/worker/mock проверяет saturating
queue/streams, отказ без task side effect, retry при заполненной очереди, control
availability, освобождение мест/restart и bounded stalled Write без client deadline.
Первый стенд допускал claim replay-fixture во время её наполнения; fixture теперь
готовится под занятым worker. Для воспроизводимого backpressure у тестового канала
BDP probe отключён, lookahead — 5 bytes, transport/auth и 4 MiB message limit сохранены.
Production channel flow-control параметры не изменялись. Тест учитывает серверное,
а не мгновенное клиентское освобождение слотов.

A14 остаётся partial: quotas на active tasks/streams не заменяют пагинацию больших
unary replies, ограничения settings/worker payloads и retention всей истории.

Финальное доказательство: `/tmp/arli-wave35-ci.log` — CI_OK, 60/60, external SDK
roundtrip, arli-law, qmllint, module-size law; git diff --check чистый.
Actual daemon regression дополнительно держит здоровый model wait 11 секунд,
дольше write budget, и успешно завершает его после перегрузки/restart.
`task-admission-live-final.json`: настоящая Gemma, daemon/worker/authenticated
gRPC, 4 tools/1 edit/1 exact approval, no tool errors; script и состав workspace
сохранены. SQLite done/active count zero/порядок read-edit-read-shell и manifest
plan/hash проверены отдельно. Model partial сохранён без завышения гарантии;
нагрузка/нечитающий клиент проверяются deterministic real-process тестами.

### Волна 36 — страницы переписки и Qt selection fencing

Исправлено отсутствие доступа к сообщениям старше последних 200: добавлены
ListMessages и opt-in message_page_size в OpenDialog, next_before_id в ответах.
Старое значение 0 сохраняет прежний контракт. Новая страница ограничена числом
записей и bytes до копирования SQLite text в application memory; один слишком
большой элемент даёт явный RESOURCE_EXHAUSTED. Range query и partial index избегают
OFFSET/сканирования уже загруженного префикса; скрытая context_state исключена.

Qt показывает кнопку ранних сообщений, сохраняет уже загруженное при ошибках,
позволяет повторить ту же страницу, блокирует дублирующие запросы. Generation fence
применён и к OpenDialog, и к поздним страницам: старый ответ не переключает диалог
обратно и не подмешивает чужую переписку. Переполнение control queue не оставляет
вечный loading. Поиск и copy обозначены как действия над загруженной частью.

Targeted 4/4: actual daemon/worker + authenticated gRPC проверяют 425 сообщений,
перезапуск между страницами, concurrent append, точный набор ID без дублей/пропусков,
legacy 200, auth, validation, byte budget и oversized item. Qt fixture проверяет
late response, retry, single-flight, legacy reply; QAbstractItemModelTester проверяет
insert contract. QML test нажимает настоящую кнопку и проверяет disabled/exhausted.

A14 остаётся partial: ListDialogs/GetTurnTrace, крупные отдельные сообщения,
settings/worker payloads и общая retention ещё требуют работы. Новый ListMessages
не превращает весь OpenDialog (с trace) в гарантированно небольшой ответ.

Финальный `/tmp/arli-wave36-ci.log`: 62/62, SDK_DAEMON_ROUNDTRIP_OK,
INSTALLED_SDK_OK, arli-law, qmllint, module-size law; git diff --check чистый.
`message-pages-qt-live-final.json`: actual running Gemma через daemon/worker и Qt
с paged OpenDialog, точная правка, restart/workspace switch, historical discovery,
обязательное подтверждение и точный откат прошли. SQLite: обе задачи done,
четыре видимых сообщения и индекс истории. Глубокая пагинация доказана отдельно
425-message real-process тестом, а не объявлена историей, созданной моделью.

### Волна 37 — recency pages диалогов и Unicode-поиск

Добавлен authenticated ListDialogPage с прежним порядком updated_at DESC/id DESC,
не более 100 summaries без instructions и soft byte budget 512 KiB. Schema 15:
revision в dialog_list_state, insert/update/delete triggers и recency index.
Revision и строки считываются одним SQL statement; сохранённое продолжение работает
после restart и явно отказывает ABORTED при любой записи dialogs из другого
process, включая touch после нового сообщения. Точное совпадение секунд updated_at
не скрывает изменение. Никаких долгоживущих серверных списков или silent skip.

Qt последовательно запрашивает страницы, сохраняет прежнее при ошибке, добавляет
строки через insert, не дублирует запросы. Поиск без учёта регистра остаётся на Qt
и охватывает ещё не загруженные страницы; сохраняются только совпадения до batch
50 результатов. Это линейный обход по сети, не индексированный поиск. Debounce и
generation защищают смену запроса. ABORTED требует явного обновления; временная
ошибка позволяет повторить ту же страницу. Только UNIMPLEMENTED допускает старый
ListDialogs. Переполнение очереди снимает loading с ошибкой.

Legacy ListDialogs для больших списков теперь явно отказывает после 1000 строк
или 3 MiB, не обрезает успешный ответ. Новые CreateDialog/RenameDialog ограничены
4 KiB UTF-8 без NUL; ошибка переименования видна в Qt. Старые большие titles
поддерживаются до 256 KiB на элемент, сверх этого выдаётся явная ошибка.

Targeted 7/7: actual daemon/worker/gRPC обходит 1005 диалогов через restart без
дубликатов/потерь и проверяет межпроцессные rename/touch/insert/delete, auth,
неверные параметры и byte caps; миграции и предыдущая пагинация сообщений проходят.
Qt tests проверяют case-insensitive кириллицу за первой страницей, смену запроса
при позднем ответе, retry/refresh, legacy-only fallback и QAbstractItemModelTester.
QML test нажимает load-more и проверяет предложение обновить изменившийся список.

Финальный код прошёл full CI 64/64 сначала на 0.5.2
(`/tmp/arli-wave37-ci.log`), затем на release 0.5.3
(`/tmp/arli-wave37-release-ci.log`): SDK_DAEMON_ROUNDTRIP_OK,
INSTALLED_SDK_OK, arli-law, qmllint, module-size law, git diff --check.
`dialog-pages-qt-live-final.json`: actual Gemma/daemon/worker/Qt, точная правка,
restart/workspace switch, discovery/approval/exact revert прошли. SQLite обе задачи
done, schema 15, revision и все три triggers проверены отдельно.
По запросу пользователя дальнейшие волны откладываются после сборки RPM 0.5.3.

RPM 0.5.3-1.fc44.x86_64 собран штатным scripts/build-rpm.sh в отдельном
`/home/owned/arli-dist/wave37`. Source allowlist исключает developer state,
credentials directories, caches и build trees. `%check` внутри rpmbuild прошёл
64/64. Digest, 113 payload entries, SDK libraries и версии daemon/agent/shell/probe
проверены; shell из распакованного RPM запускается offscreen с packaged QML после
удаления build tree. `rpm-0.5.3-wave37.json` содержит SHA-256 RPM/source archive,
путь к логу и категории оставшихся предупреждений Fedora compiler flags; сборка
не объявляется свободной от предупреждений.

`rpm-0.5.3-live-final.json`: именно извлечённые daemon/agent, actual Gemma и Qt facade
успешно выполняют правку, restart/workspace switch и подтверждённый точный откат.
SQLite обе задачи done/schema 15. Установленный пакет 0.5.2 не изменён. Дальнейшая
работа по общему аудиту приостанавливается по просьбе пользователя; A14/A23 и
прочие открытые требования не объявляются завершёнными.
