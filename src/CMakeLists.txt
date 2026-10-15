cmake_minimum_required(VERSION 3.16)

# Single source of version: /VERSION → version.hpp (kVersion, kSlice).
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" ARLI_VERSION_RAW)
string(STRIP "${ARLI_VERSION_RAW}" ARLI_VERSION)
if(NOT ARLI_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH, got '${ARLI_VERSION}'")
endif()
set(ARLI_SLICE "M${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

project(arli VERSION ${ARLI_VERSION} LANGUAGES CXX)

include(GNUInstallDirs)

option(ARLI_BUILD_SHELL "Build the Qt Quick shell (arli-shell)" ON)
option(ARLI_BUILD_TESTS "Build smoke/unit test binaries" ON)
option(ARLI_SANITIZE "Build with ASan/UBSan" OFF)
option(ARLI_THREAD_SANITIZE "Build with ThreadSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(ARLI_SANITIZE AND ARLI_THREAD_SANITIZE)
  message(FATAL_ERROR "Choose either ARLI_SANITIZE or ARLI_THREAD_SANITIZE")
elseif(ARLI_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  add_compile_definitions(ARLI_SANITIZER_BUILD=1)
elseif(ARLI_THREAD_SANITIZE)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
  add_compile_definitions(ARLI_SANITIZER_BUILD=1)
endif()

if(NOT ARLI_PROTOC)
  find_program(ARLI_PROTOC protoc REQUIRED)
endif()
if(NOT ARLI_GRPC_PLUGIN)
  find_program(ARLI_GRPC_PLUGIN grpc_cpp_plugin REQUIRED)
endif()
if(NOT ARLI_QML_DIR)
  set(ARLI_QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../qml")
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(SQLite3 REQUIRED)
if(ARLI_BUILD_SHELL)
  find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
endif()
set(ARLI_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN})

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in ${GEN}/version.hpp @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../prompts/system.ru.txt")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../prompts/system.ru.txt" ARLI_SYSTEM_TEXT)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/daemon/system_prompt.hpp.in ${GEN}/system_prompt.hpp @ONLY)

add_custom_command(
  OUTPUT
    ${GEN}/arli.pb.cc
    ${GEN}/arli.pb.h
    ${GEN}/arli.grpc.pb.cc
    ${GEN}/arli.grpc.pb.h
  COMMAND ${ARLI_PROTOC}
  ARGS
    --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
    --cpp_out=${GEN}
    --grpc_out=${GEN}
    --plugin=protoc-gen-grpc=${ARLI_GRPC_PLUGIN}
    arli.proto
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/arli.proto
  VERBATIM
)

add_library(arli_proto STATIC
  ${GEN}/arli.pb.cc
  ${GEN}/arli.grpc.pb.cc
)
target_include_directories(arli_proto PUBLIC $<BUILD_INTERFACE:${GEN}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/arli>)
target_link_libraries(arli_proto PUBLIC PkgConfig::GRPC PkgConfig::PROTOBUF)

# Shared by daemon, agent, shell, probes: endpoint, paths, version, grpc auth helpers.
add_library(arli_common STATIC
  common/redact_store.cpp
  common/redact.cpp
  common/auth.cpp
  common/log.cpp
  common/secrets.cpp
)
target_include_directories(arli_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/daemon>
  $<BUILD_INTERFACE:${GEN}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/arli>
)
target_link_libraries(arli_common PUBLIC arli_proto Threads::Threads OpenSSL::Crypto)

add_library(arli_grpc_tools STATIC sdk/grpc_tool_provider.cpp)
target_include_directories(arli_grpc_tools PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/arli>
  PRIVATE ${ARLI_THIRD_PARTY})
target_link_libraries(arli_grpc_tools PUBLIC arli_common)
target_compile_features(arli_grpc_tools PUBLIC cxx_std_20)

add_library(arli_core STATIC
  daemon/job_retention.cpp daemon/job_orphans.cpp daemon/job_log_reader.cpp
  daemon/job_maintenance.cpp
  daemon/grpc_registry.cpp
  daemon/db.cpp
  daemon/db_settings.cpp
  daemon/db_dialogs.cpp
  daemon/db_jobs.cpp
  daemon/db_messages.cpp
  daemon/db_message_page.cpp
  daemon/db_dialog_page.cpp
  daemon/db_tasks.cpp
  daemon/db_events.cpp
  daemon/db_commands.cpp
  daemon/hub_commands.cpp
  daemon/hub_interactions.cpp
  daemon/hub_interaction_delivery.cpp
  daemon/db_steps.cpp
  daemon/db_history.cpp
  daemon/audit.cpp
  daemon/trace.cpp
  daemon/tools.cpp
  daemon/tools/atomic_publish.cpp
  daemon/workspace_identity.cpp daemon/mutation_catalog.cpp daemon/tools/backup.cpp daemon/tools/backup_store.cpp daemon/tools/mutation_journal.cpp daemon/tools/mutation_recovery.cpp daemon/tools/mutation_revert.cpp
  daemon/tools/syntax_check.cpp
  daemon/tools/tier_a.cpp
  daemon/tools/tier_a_manage.cpp
  daemon/tools/git_tools.cpp
  daemon/tools/http_tools.cpp
  daemon/tools/registry.cpp
  daemon/tools/shell_lex.cpp
  daemon/tools/shell_run.cpp
  daemon/tools/rlimit_util.cpp
  daemon/llm_internal.cpp
  daemon/request_budget.cpp
  daemon/workspace_rules.cpp
  daemon/llm_structured.cpp
  daemon/llm_models.cpp
  daemon/llm_tokenizer.cpp
  daemon/llm_sse.cpp
  daemon/llm_stream.cpp
  daemon/turn.cpp
  daemon/turn_policy.cpp
  daemon/create_policy.cpp
  daemon/modes.cpp
  daemon/file_pass.cpp
  daemon/db_file_pass.cpp
  daemon/plan.cpp
  daemon/db_plan.cpp
  daemon/memory.cpp
  daemon/db_memory.cpp
  daemon/edit_stats.cpp
  daemon/mutation_diff.cpp
  daemon/confirm.cpp
  daemon/ask.cpp
  daemon/context.cpp
  daemon/task_acceptance.cpp
  daemon/task_acceptance_procedures.cpp
  daemon/task_stage.cpp
  daemon/durable_context.cpp
  daemon/context_summary.cpp
  daemon/jobs.cpp
  daemon/job_launch.cpp
  daemon/hub.cpp
  daemon/hub_control.cpp
  daemon/hub_waiters.cpp
  daemon/spawn_agent.cpp
  daemon/watchdog.cpp
)
target_include_directories(arli_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/daemon
  ${GEN}
  ${ARLI_THIRD_PARTY}
)
target_link_libraries(arli_core PUBLIC
  arli_grpc_tools
  arli_proto
  arli_common
  Threads::Threads
  PkgConfig::CURL
  SQLite3::SQLite3
)

add_executable(arli-daemon
  daemon/main.cpp
  daemon/server.cpp
  daemon/agent_service_fill.cpp
  daemon/agent_health.cpp
  daemon/agent_stream.cpp daemon/agent_revert.cpp
  daemon/agent_service.cpp
  daemon/agent_message_page.cpp
  daemon/agent_dialog_page.cpp
  daemon/agent_service_modes.cpp
  daemon/agent_jobs.cpp daemon/agent_job_stream.cpp
)
target_link_libraries(arli-daemon PRIVATE arli_core)

add_executable(arli-agent
  daemon/agent_main.cpp
  daemon/agent_worker.cpp daemon/worker_revert.cpp
)
target_link_libraries(arli-agent PRIVATE arli_core)

add_executable(arli-probe daemon/probe.cpp)
target_link_libraries(arli-probe PRIVATE arli_common)

add_executable(arli-job-supervisor daemon/job_supervisor.cpp daemon/tools/rlimit_util.cpp)
target_include_directories(arli-job-supervisor PRIVATE ${ARLI_THIRD_PARTY})
target_link_libraries(arli-job-supervisor PRIVATE arli_common)
add_dependencies(arli-agent arli-job-supervisor)
set(ARLI_INSTALL_TARGETS arli-daemon arli-agent arli-probe arli-job-supervisor)

if(ARLI_BUILD_TESTS)
  enable_testing()

  add_executable(arli-smoke daemon/smoke_turn.cpp)
  target_link_libraries(arli-smoke PRIVATE arli_common)

  add_executable(arli-smoke-confirm daemon/smoke_confirm.cpp)
  target_link_libraries(arli-smoke-confirm PRIVATE arli_core)

  add_executable(arli-smoke-write-guards daemon/smoke_write_guards.cpp)
  target_link_libraries(arli-smoke-write-guards PRIVATE arli_core)

  add_executable(arli-smoke-mutation-gate daemon/smoke_mutation_gate.cpp)
  target_link_libraries(arli-smoke-mutation-gate PRIVATE arli_core)

  add_executable(arli-smoke-orch daemon/smoke_orch.cpp)
  target_link_libraries(arli-smoke-orch PRIVATE arli_common)

  # Test-only mock OpenAI server + lifecycle test (daemon + worker + mock, no live model).
  add_executable(arli-mock-llm ../tests/mock_llm.cpp)
  target_include_directories(arli-mock-llm PRIVATE ${ARLI_THIRD_PARTY})
  target_link_libraries(arli-mock-llm PRIVATE Threads::Threads)

  add_executable(arli-test-lifecycle ../tests/lifecycle_test.cpp)
  target_include_directories(arli-test-lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
  target_link_libraries(arli-test-lifecycle PRIVATE arli_common)

  add_executable(arli-test-audit ../tests/audit_test.cpp)
  target_include_directories(arli-test-audit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
  target_link_libraries(arli-test-audit PRIVATE arli_common)

  add_executable(arli-grpc-registry-eval ../tests/grpc_registry_daemon_eval.cpp)
  target_link_libraries(arli-grpc-registry-eval PRIVATE arli_core)
  add_test(NAME grpc-registry-daemon COMMAND arli-grpc-registry-eval $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> --mock $<TARGET_FILE:arli-mock-llm> ${CMAKE_CURRENT_BINARY_DIR}/grpc-registry-report.json)
  set_tests_properties(grpc-registry-daemon PROPERTIES TIMEOUT 210)
  add_executable(arli-test-job-stream ../tests/job_stream_test.cpp)
  target_link_libraries(arli-test-job-stream PRIVATE arli_core)
  add_test(NAME job-stream COMMAND arli-test-job-stream $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent>)
  set_tests_properties(job-stream PROPERTIES TIMEOUT 60)
  add_executable(arli-test-job-orphans ../tests/job_orphans_test.cpp)
  target_link_libraries(arli-test-job-orphans PRIVATE arli_core)
  add_dependencies(arli-test-job-orphans arli-job-supervisor)
  add_test(NAME job-orphans COMMAND arli-test-job-orphans)
  set_tests_properties(job-orphans PROPERTIES TIMEOUT 20)
  if(ARLI_SANITIZE OR ARLI_THREAD_SANITIZE)
    # The test creates and inspects more than 140 directories and several
    # supervised process trees; allocator instrumentation can exceed 20 s.
    set_tests_properties(job-orphans PROPERTIES TIMEOUT 120)
  endif()
  add_executable(arli-test-job-retention ../tests/job_retention_test.cpp)
  target_link_libraries(arli-test-job-retention PRIVATE arli_core)
  add_dependencies(arli-test-job-retention arli-job-supervisor)
  add_test(NAME job-retention COMMAND arli-test-job-retention)
  add_executable(arli-test-job-quota ../tests/job_quota_test.cpp)
  target_link_libraries(arli-test-job-quota PRIVATE arli_core)
  add_test(NAME job-quota COMMAND arli-test-job-quota)
  add_executable(arli-test-job-maintenance ../tests/job_maintenance_test.cpp)
  target_link_libraries(arli-test-job-maintenance PRIVATE arli_core)
  add_dependencies(arli-test-job-maintenance arli-job-supervisor)
  add_test(NAME job-maintenance COMMAND arli-test-job-maintenance $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent>)
  set_tests_properties(job-maintenance PROPERTIES TIMEOUT 30)
  add_executable(arli-test-job-ownership ../tests/job_ownership_test.cpp)
  target_link_libraries(arli-test-job-ownership PRIVATE arli_core)
  add_test(NAME job-ownership COMMAND arli-test-job-ownership)
  add_executable(arli-test-job-ownership-daemon ../tests/job_ownership_daemon_test.cpp)
  target_link_libraries(arli-test-job-ownership-daemon PRIVATE arli_core)
  add_test(NAME job-ownership-daemon COMMAND arli-test-job-ownership-daemon $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(job-ownership-daemon PROPERTIES TIMEOUT 60)
  add_executable(arli-test-jobs-redaction ../tests/jobs_redaction_test.cpp)
  target_link_libraries(arli-test-jobs-redaction PRIVATE arli_core)
  add_dependencies(arli-test-jobs-redaction arli-job-supervisor)
  add_test(NAME jobs-redaction COMMAND arli-test-jobs-redaction)
  add_executable(arli-test-redaction-store ../tests/redaction_store_test.cpp)
  target_link_libraries(arli-test-redaction-store PRIVATE arli_common)
  add_test(NAME redaction-store COMMAND arli-test-redaction-store)
  add_executable(arli-test-redaction-daemon ../tests/redaction_daemon_test.cpp)
  target_link_libraries(arli-test-redaction-daemon PRIVATE arli_core)
  add_test(NAME redaction-daemon COMMAND arli-test-redaction-daemon $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(redaction-daemon PROPERTIES TIMEOUT 60)
  add_executable(arli-test-redaction ../tests/redaction_test.cpp)
  target_link_libraries(arli-test-redaction PRIVATE arli_core)
  add_test(NAME redaction COMMAND arli-test-redaction)
  add_executable(arli-test-grpc-registry ../tests/grpc_registry_test.cpp)
  target_link_libraries(arli-test-grpc-registry PRIVATE arli_core)
  add_test(NAME grpc-registry COMMAND arli-test-grpc-registry)
  add_executable(arli-test-private-state ../tests/private_state_test.cpp)
  target_link_libraries(arli-test-private-state PRIVATE arli_core)
  add_test(NAME private-state COMMAND arli-test-private-state)
  add_executable(arli-test-tools ../tests/tools_tier_test.cpp)
  target_link_libraries(arli-test-tools PRIVATE arli_core)
  add_executable(arli-test-file-integrity ../tests/file_edit_integrity_test.cpp)
  target_link_libraries(arli-test-file-integrity PRIVATE arli_core)
  add_test(NAME file-edit-integrity COMMAND arli-test-file-integrity)
  add_executable(arli-test-shell-runtime ../tests/shell_runtime_test.cpp)
  target_link_libraries(arli-test-shell-runtime PRIVATE arli_core)
  add_test(NAME shell-runtime COMMAND arli-test-shell-runtime)
  set_tests_properties(shell-runtime PROPERTIES TIMEOUT 25)
  add_executable(arli-test-syntax ../tests/syntax_check_test.cpp)
  add_executable(arli-test-verification-rejected ../tests/verification_rejected_edit_test.cpp)
  target_link_libraries(arli-test-verification-rejected PRIVATE arli_core)
  add_test(NAME verification-rejected-edit COMMAND arli-test-verification-rejected)
  add_executable(arli-test-task-stage ../tests/task_stage_test.cpp)
  target_link_libraries(arli-test-task-stage PRIVATE arli_core)
  add_test(NAME task-stage COMMAND arli-test-task-stage)
  add_executable(arli-test-strict-replace ../tests/strict_replace_test.cpp)
  target_link_libraries(arli-test-strict-replace PRIVATE arli_core)
  add_test(NAME strict-replace COMMAND arli-test-strict-replace)
  set_tests_properties(strict-replace task-stage verification-rejected-edit PROPERTIES TIMEOUT 30)
  add_executable(arli-test-history-retention ../tests/history_retention_test.cpp)
  target_link_libraries(arli-test-history-retention PRIVATE arli_core)
  add_test(NAME history-retention COMMAND arli-test-history-retention)
  add_executable(arli-test-verification-candidate ../tests/verification_candidate_replay_test.cpp)
  target_link_libraries(arli-test-verification-candidate PRIVATE arli_core)
  add_test(NAME verification-candidate COMMAND arli-test-verification-candidate)
  add_executable(arli-test-turn-recovery ../tests/turn_recovery_test.cpp)
  target_link_libraries(arli-test-turn-recovery PRIVATE arli_core)
  add_test(NAME turn-recovery COMMAND arli-test-turn-recovery $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(turn-recovery PROPERTIES TIMEOUT 90)
  add_executable(arli-live-webos-recovery-eval ../tests/live_webos_recovery_eval.cpp)
  target_link_libraries(arli-live-webos-recovery-eval PRIVATE arli_core)
  target_link_libraries(arli-test-syntax PRIVATE arli_core)
  add_test(NAME syntax-check COMMAND arli-test-syntax)

  add_executable(arli-live-eval ../tests/live_agent_eval.cpp)
  target_link_libraries(arli-live-eval PRIVATE arli_core)
  add_executable(arli-live-procedure-eval ../tests/live_procedure_eval.cpp)
  target_link_libraries(arli-live-procedure-eval PRIVATE arli_core)

  add_executable(arli-test-llm-protocol ../tests/llm_protocol_test.cpp)
  target_link_libraries(arli-test-llm-protocol PRIVATE arli_core)
  add_test(NAME llm-protocol COMMAND arli-test-llm-protocol)

  add_executable(arli-test-context ../tests/context_test.cpp)
  target_link_libraries(arli-test-context PRIVATE arli_core)

  add_executable(arli-test-modes ../tests/modes_test.cpp)
  target_link_libraries(arli-test-modes PRIVATE arli_core)

  add_executable(arli-test-db-file-pass ../tests/db_file_pass_test.cpp)
  target_link_libraries(arli-test-db-file-pass PRIVATE arli_core)
  add_test(NAME db-file-pass-migration COMMAND arli-test-db-file-pass)

  add_executable(arli-test-file-pass ../tests/file_pass_test.cpp)
  target_link_libraries(arli-test-file-pass PRIVATE arli_core)

  # Offline tests (no LLM). Everything else is a live smoke run by scripts/.
  add_test(NAME smoke-confirm COMMAND arli-smoke-confirm)
  add_test(NAME smoke-write-guards COMMAND arli-smoke-write-guards)
  add_test(NAME smoke-mutation-gate COMMAND arli-smoke-mutation-gate)
  add_executable(arli-live-web-eval ../tests/live_web_eval.cpp)
  target_link_libraries(arli-live-web-eval PRIVATE arli_core)
  add_executable(arli-test-task-acceptance ../tests/task_acceptance_test.cpp)
  target_link_libraries(arli-test-task-acceptance PRIVATE arli_core)
  add_test(NAME task-acceptance COMMAND arli-test-task-acceptance)
  add_executable(arli-test-task-quota ../tests/task_quota_test.cpp)
  add_executable(arli-test-message-page ../tests/message_page_test.cpp)
  add_executable(arli-test-dialog-page ../tests/dialog_page_test.cpp)
  target_link_libraries(arli-test-dialog-page PRIVATE arli_core)
  add_test(NAME dialog-page COMMAND arli-test-dialog-page $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent>)
  set_tests_properties(dialog-page PROPERTIES TIMEOUT 35)
  target_link_libraries(arli-test-message-page PRIVATE arli_core)
  add_test(NAME message-page COMMAND arli-test-message-page $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent>)
  set_tests_properties(message-page PROPERTIES TIMEOUT 35)
  target_link_libraries(arli-test-task-quota PRIVATE arli_core)
  add_test(NAME task-quota COMMAND arli-test-task-quota)
  add_executable(arli-test-task-admission ../tests/task_admission_daemon_test.cpp)
  target_link_libraries(arli-test-task-admission PRIVATE arli_core)
  add_test(NAME task-admission COMMAND arli-test-task-admission $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(task-admission PROPERTIES TIMEOUT 60)
  add_executable(arli-test-procedure-turn ../tests/procedure_turn_test.cpp)
  target_include_directories(arli-test-procedure-turn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
  target_link_libraries(arli-test-procedure-turn PRIVATE arli_core)
  add_test(NAME procedure-turn COMMAND arli-test-procedure-turn $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(procedure-turn PROPERTIES TIMEOUT 45)
  add_executable(arli-test-workspace-identity ../tests/workspace_identity_test.cpp)
  target_link_libraries(arli-test-workspace-identity PRIVATE arli_core)
  add_test(NAME workspace-identity COMMAND arli-test-workspace-identity)
  add_executable(arli-test-workspace-queue ../tests/workspace_queue_daemon_test.cpp)
  target_link_libraries(arli-test-workspace-queue PRIVATE arli_core)
  add_test(NAME workspace-queue COMMAND arli-test-workspace-queue $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(workspace-queue PROPERTIES TIMEOUT 60)
  add_executable(arli-test-mutation-catalog ../tests/mutation_catalog_test.cpp)
  target_link_libraries(arli-test-mutation-catalog PRIVATE arli_core)
  add_test(NAME mutation-catalog COMMAND arli-test-mutation-catalog)
  add_executable(arli-test-crash-revert-daemon ../tests/crash_revert_daemon_test.cpp)
  target_link_libraries(arli-test-crash-revert-daemon PRIVATE arli_core)
  add_test(NAME crash-revert-daemon COMMAND arli-test-crash-revert-daemon $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent>)
  set_tests_properties(crash-revert-daemon PROPERTIES TIMEOUT 60)
  add_executable(arli-test-revert-daemon ../tests/revert_daemon_test.cpp)
  target_link_libraries(arli-test-revert-daemon PRIVATE arli_core)
  add_test(NAME revert-daemon COMMAND arli-test-revert-daemon $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent>)
  set_tests_properties(revert-daemon PROPERTIES TIMEOUT 90)
  add_executable(arli-test-mutation-journal ../tests/mutation_journal_test.cpp)
  target_link_libraries(arli-test-mutation-journal PRIVATE arli_core)
  target_link_options(arli-test-mutation-journal PRIVATE -Wl,--wrap=renameat -Wl,--wrap=write)
  add_test(NAME mutation-journal COMMAND arli-test-mutation-journal)
  add_executable(arli-test-backup-store ../tests/backup_store_test.cpp)
  target_link_libraries(arli-test-backup-store PRIVATE arli_core)
  add_test(NAME backup-store COMMAND arli-test-backup-store)
  add_executable(arli-test-file-publication ../tests/file_publication_test.cpp)
  target_link_libraries(arli-test-file-publication PRIVATE arli_core)
  target_link_options(arli-test-file-publication PRIVATE -Wl,--wrap=write -Wl,--wrap=fsync -Wl,--wrap=openat)
  add_test(NAME file-publication COMMAND arli-test-file-publication)
  add_executable(arli-test-durable-context ../tests/durable_context_test.cpp)
  target_link_libraries(arli-test-durable-context PRIVATE arli_core)
  add_test(NAME durable-context COMMAND arli-test-durable-context)
  add_executable(arli-test-workspace-rules ../tests/workspace_rules_test.cpp)
  target_link_libraries(arli-test-workspace-rules PRIVATE arli_core)
  add_test(NAME workspace-rules COMMAND arli-test-workspace-rules)
  add_executable(arli-test-request-budget ../tests/request_budget_test.cpp)
  target_link_libraries(arli-test-request-budget PRIVATE arli_core)
  add_test(NAME request-budget COMMAND arli-test-request-budget)
  add_executable(arli-test-interaction-delivery ../tests/interaction_delivery_test.cpp)
  target_link_libraries(arli-test-interaction-delivery PRIVATE arli_core)
  add_test(NAME interaction-delivery COMMAND arli-test-interaction-delivery)
  set_tests_properties(interaction-delivery PROPERTIES TIMEOUT 15)
  add_executable(arli-test-claim-delivery ../tests/claim_delivery_test.cpp)
  target_link_libraries(arli-test-claim-delivery PRIVATE arli_core)
  add_test(NAME claim-delivery COMMAND arli-test-claim-delivery)
  add_executable(arli-test-grpc-provider ../tests/grpc_tool_provider_test.cpp)
  target_link_libraries(arli-test-grpc-provider PRIVATE arli_core)
  add_test(NAME grpc-provider COMMAND arli-test-grpc-provider $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(grpc-provider PROPERTIES TIMEOUT 30)
  add_executable(arli-test-job-channel ../tests/job_channel_test.cpp)
  target_include_directories(arli-test-job-channel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/daemon)
  target_link_libraries(arli-test-job-channel PRIVATE Threads::Threads)
  add_test(NAME job-channel COMMAND arli-test-job-channel)
  add_executable(arli-test-jobs ../tests/jobs_test.cpp)
  target_link_libraries(arli-test-jobs PRIVATE arli_core)
  add_dependencies(arli-test-jobs arli-job-supervisor)
  add_test(NAME jobs COMMAND arli-test-jobs)
  add_executable(arli-test-worker-identity ../tests/worker_identity_test.cpp)
  target_link_libraries(arli-test-worker-identity PRIVATE arli_core)
  add_test(NAME worker-identity COMMAND arli-test-worker-identity)
  add_executable(arli-test-session-generation ../tests/session_generation_test.cpp)
  target_link_libraries(arli-test-session-generation PRIVATE arli_core)
  add_test(NAME session-generation COMMAND arli-test-session-generation)
  add_executable(arli-test-create-policy ../tests/create_policy_test.cpp)
  target_link_libraries(arli-test-create-policy PRIVATE arli_core)
  add_test(NAME create-policy COMMAND arli-test-create-policy)
  add_executable(arli-test-turn-execution ../tests/turn_execution_test.cpp)
  target_include_directories(arli-test-turn-execution PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tests)
  target_link_libraries(arli-test-turn-execution PRIVATE arli_core)
  add_test(NAME turn-execution COMMAND arli-test-turn-execution $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(turn-execution PROPERTIES TIMEOUT 30)
  add_test(NAME lifecycle
           COMMAND arli-test-lifecycle $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(lifecycle PROPERTIES TIMEOUT 180)
  add_test(NAME audit
           COMMAND arli-test-audit $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(audit PROPERTIES TIMEOUT 120)
  add_test(NAME tools-tier COMMAND arli-test-tools)
  add_test(NAME context-budget COMMAND arli-test-context)
  add_test(NAME modes-list-only COMMAND arli-test-modes)
  add_test(NAME file-pass COMMAND arli-test-file-pass)
  add_executable(arli-test-plan ../tests/plan_test.cpp)
  target_link_libraries(arli-test-plan PRIVATE arli_core)
  add_test(NAME plan-mode COMMAND arli-test-plan)
  add_executable(arli-live-grpc-tools-eval ../tests/live_grpc_tools_eval.cpp)
  target_link_libraries(arli-live-grpc-tools-eval PRIVATE arli_core)
  add_executable(arli-live-jobs-eval ../tests/live_jobs_eval.cpp)
  target_link_libraries(arli-live-jobs-eval PRIVATE arli_core)
  add_executable(arli-live-stream-eval ../tests/live_stream_eval.cpp)
  target_link_libraries(arli-live-stream-eval PRIVATE arli_core)
  add_executable(arli-test-stream-resume ../tests/stream_resume_test.cpp)
  target_link_libraries(arli-test-stream-resume PRIVATE arli_core)
  add_test(NAME stream-resume COMMAND arli-test-stream-resume $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(stream-resume PROPERTIES TIMEOUT 90)
  add_executable(arli-test-audit-regression ../tests/audit_regression_test.cpp)
  target_link_libraries(arli-test-audit-regression PRIVATE arli_core)
  add_test(NAME audit-regression COMMAND arli-test-audit-regression $<TARGET_FILE:arli-daemon> $<TARGET_FILE:arli-agent> $<TARGET_FILE:arli-mock-llm>)
  set_tests_properties(audit-regression PROPERTIES TIMEOUT 60)
  add_executable(arli-test-memory ../tests/memory_test.cpp)
  target_link_libraries(arli-test-memory PRIVATE arli_core)
  add_test(NAME memory-store COMMAND arli-test-memory)
  add_executable(arli-test-edit-stats ../tests/edit_stats_test.cpp)
  target_link_libraries(arli-test-edit-stats PRIVATE arli_core)
  add_test(NAME edit-stats COMMAND arli-test-edit-stats)
  add_executable(arli-test-mutation-diff ../tests/mutation_diff_test.cpp)
  target_link_libraries(arli-test-mutation-diff PRIVATE arli_core)
  add_test(NAME mutation-diff COMMAND arli-test-mutation-diff)
endif()

if(ARLI_BUILD_SHELL)
  set(ARLI_SHELL_SOURCES
    shell/facade_instructions.cpp shell/facade_revert.cpp
    shell/facade.cpp
    shell/facade.hpp
    shell/models.cpp
    shell/message_paging.cpp
    shell/dialog_paging.cpp
    shell/desk_dialog_page.cpp
    shell/desk_message_page.cpp
    shell/models.hpp
    shell/job_log_watch.cpp
    shell/desk_job_log.cpp
    shell/desk.cpp
    shell/desk.hpp
    shell/desk_jobs.cpp
    shell/desk_jobs_bg.cpp
    shell/desk_jobs_save.cpp
    shell/desk_stream.cpp
    shell/desk_turn.cpp
    shell/desk_modes.cpp
    shell/embed.cpp
    shell/embed.hpp
  )
  add_library(arli_embed STATIC ${ARLI_SHELL_SOURCES})
  target_compile_definitions(arli_embed PRIVATE ARLI_QML_DIR="${ARLI_QML_DIR}")
  target_include_directories(arli_embed PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/shell>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/daemon>
    $<BUILD_INTERFACE:${GEN}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/arli>
  )
  target_link_libraries(arli_embed PUBLIC
    arli_common
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
  )

  add_executable(arli-shell shell/main.cpp)
  target_link_libraries(arli-shell PRIVATE arli_embed)
  list(APPEND ARLI_INSTALL_TARGETS arli-shell)

  if(ARLI_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(arli-live-revert-ui-eval ../tests/live_revert_ui_eval.cpp)
    target_link_libraries(arli-live-revert-ui-eval PRIVATE arli_embed arli_core Qt6::Test)
    add_executable(arli-test-revert-ui ../tests/revert_ui_test.cpp)
    target_link_libraries(arli-test-revert-ui PRIVATE arli_embed Qt6::Test)
    add_test(NAME revert-ui COMMAND arli-test-revert-ui)
    set_tests_properties(revert-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    add_executable(arli-test-job-log-ui ../tests/job_log_ui_test.cpp)
    add_executable(arli-test-message-page-ui ../tests/message_page_ui_test.cpp)
    add_executable(arli-test-dialog-page-ui ../tests/dialog_page_ui_test.cpp)
    target_link_libraries(arli-test-dialog-page-ui PRIVATE arli_embed Qt6::Test)
    add_test(NAME dialog-page-ui COMMAND arli-test-dialog-page-ui)
    set_tests_properties(dialog-page-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    target_link_libraries(arli-test-message-page-ui PRIVATE arli_embed Qt6::Test)
    add_test(NAME message-page-ui COMMAND arli-test-message-page-ui)
    set_tests_properties(message-page-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    target_link_libraries(arli-test-job-log-ui PRIVATE arli_embed Qt6::Test)
    add_test(NAME job-log-ui COMMAND arli-test-job-log-ui)
    set_tests_properties(job-log-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
    add_executable(arli-test-ui ../tests/ui_test.cpp)
    target_link_libraries(arli-test-ui PRIVATE arli_embed Qt6::Test)
    target_compile_definitions(arli-test-ui PRIVATE ARLI_UI_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME shell-ui COMMAND arli-test-ui)
    set_tests_properties(shell-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/ui-config" TIMEOUT 30)
  endif()

  install(FILES
    shell/embed.hpp
    shell/facade.hpp
    shell/models.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/arli COMPONENT SDK
  )
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../qml/ DESTINATION share/arli/qml)
endif()

install(TARGETS ${ARLI_INSTALL_TARGETS} RUNTIME DESTINATION bin)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/arli.sh DESTINATION bin RENAME arli)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/arli-doctor.sh DESTINATION bin RENAME arli-doctor)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/arli.desktop DESTINATION share/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/arli.svg DESTINATION share/icons/hicolor/scalable/apps)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/arli-daemon.service DESTINATION lib/systemd/user)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/proto/arli.proto DESTINATION share/arli/proto)
if(ARLI_BUILD_SHELL)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../docs/arli-embed.cmake DESTINATION share/arli/cmake)
endif()

include(cmake/install-sdk.cmake)

# File tools now use private state for backups; no regression test may touch user state.
if(ARLI_BUILD_TESTS)
  get_property(ARLI_TEST_NAMES DIRECTORY PROPERTY TESTS)
  set(ARLI_TEST_STATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-state" CACHE PATH "Isolated state root for CTest processes")
  foreach(ARLI_TEST_NAME IN LISTS ARLI_TEST_NAMES)
    set_tests_properties(${ARLI_TEST_NAME} PROPERTIES ENVIRONMENT_MODIFICATION
      "ARLI_STATE_DIR=set:${ARLI_TEST_STATE_DIR}/${ARLI_TEST_NAME}")
  endforeach()
endif()

# Preserve copyright and permission notices in installed distributions.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/../NOTICE
  DESTINATION share/licenses/arli)
