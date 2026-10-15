# Режимы (Э6)

Оркестратор хранит `file_pass` и `plans` в SQLite; воркер шлёт снимки в `TurnEvent`. Память (`memory`) глобальна, до 20 записей попадают в system prompt.

| Режим | Инструменты | Поведение |
|---|---|---|
| list-only | `list_dir`, `glob_files`, … | Блок `read_file` / запись, если пользователь просит только инвентарь |
| file_pass | `file_pass_set`, `file_pass_review`, `file_pass_clear` | Очередь файлов; можно читать зависимости, текущий файл завершается правкой или проверкой без правок; UI «×» → `ClearFilePass` |
| plan | `plan_set`, `plan_advance`, `plan_fail`, `plan_retry`, `plan_edit` | Шаги в `plans`; `plan_advance` ждёт результата текущего прочитанного файла (правка или `file_pass_review`) |
| memory | `remember`, `forget`, `list_memory` | Факты в таблице `memory`, inject в prompt |

Проверка без LLM: `bash scripts/smoke-modes.sh` (или `ctest -R 'modes-list-only|file-pass|plan-mode|memory-store|edit-stats'`).

`file_pass_review(path, note)` требует успешного чтения файла в текущем ходе и непустой причины.
Проверенные без изменений пути хранятся отдельно в `reviewed_json`, успешные правки — в `done_json`.
Оба результата продвигают очередь; UI учитывает оба, не выдавая проверку за запись.
Миграция SQLite 6 → 7 добавляет только `reviewed_json` и сохраняет существующие очереди.
Протокол расширен полями `reviewed` в `FilePassSnapshot` и `GetFilePassReply`.
