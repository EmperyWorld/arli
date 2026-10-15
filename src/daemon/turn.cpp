#include "secrets.hpp"
#include "redact_events.hpp"
#include "turn.hpp"
#include "grpc_tool_provider.hpp"

#include "ask.hpp"
#include "confirm.hpp"
#include "context.hpp"
#include "durable_context.hpp"
#include "llm_tool_format.hpp"
#include "verification.hpp"
#include "task_acceptance.hpp"
#include "task_stage.hpp"
#include "read_edit_guard.hpp"
#include "jobs.hpp"
#include "llm.hpp"
#include "llm_internal.hpp"
#include "llm_tokenizer.hpp"
#include "llm_models.hpp"
#include "workspace_rules.hpp"
#include "shell_policy.hpp"
#include "tools.hpp"
#include "tools/tier_a.hpp"
#include "tools/mutation_journal.hpp"
#include "file_pass.hpp"
#include "memory.hpp"
#include "plan.hpp"
#include "edit_stats.hpp"
#include "modes.hpp"
#include "turn_policy.hpp"
#include "create_policy.hpp"
#include "task_limits.hpp"
#include "tool_schema_profile.hpp"

#include "system_prompt.hpp"

#include "log.hpp"
#include "paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace arli {
namespace {

bool emit(const EventSink& sink, v1::TurnEvent ev) {
  if (!sink) {
    return false;
  }
  return sink(ev);
}

bool cancelled(const TurnControl& ctl) {
  return ctl.cancel && ctl.cancel->load();
}

void log_turn(std::int64_t dialog_id, std::int64_t turn_id, const std::string& line) {
  log::info("turn", log::kv("dialog", static_cast<long long>(dialog_id)) + " " +
                        log::kv("turn", static_cast<long long>(turn_id)) + " " + line);
}

void fill_done(v1::TurnEvent& ev, const arli::MessageRow& saved, const TurnEditStats& stats, const std::filesystem::path& journal) {
  auto* d = ev.mutable_done();
  if (!journal.empty()) d->set_changeset_id(journal.parent_path().filename().string());
  d->set_outcome("failed");
  d->set_error_code("turn_failed");
  d->set_message_id(saved.id);
  d->set_assistant_text(saved.text);
  fill_done_edit_stats(stats, d);
}

}  // namespace

std::int64_t run_turn(Database& db, const TurnConfig& input_cfg, std::int64_t dialog_id,
                      const std::string& user_text, std::int64_t turn_id, EventSink sink, TurnControl ctl) {
  secrets::load(secrets::ui_token_path());
  secrets::load(secrets::internal_token_path());
  redact::remember(input_cfg.llm_api_key);
  sink = redact_events(std::move(sink));
  TurnConfig turn_cfg = input_cfg;
  const auto history = db.list_messages(dialog_id, 400);
  const std::string previous_answer =
      !history.empty() && history.back().role == "assistant" ? history.back().text : std::string();
  std::string effective_user_text = user_text;
  TurnIntent intent;
  auto refresh_intent = [&] {
    intent = classify_turn_intent(effective_user_text, previous_answer);
    if (turn_cfg.dialog_instructions.empty()) return;
    const auto policy = classify_turn_intent(effective_user_text + "\n " + turn_cfg.dialog_instructions);
    intent.forbid_mutation = intent.forbid_mutation || policy.forbid_mutation;
    intent.forbid_create = policy.forbid_create;
    intent.allowed_create_paths = policy.allowed_create_paths;
    if (intent.forbid_mutation) intent.expects_mutation = false;
  };
  refresh_intent();
  if(ctl.check_workspace)ctl.check_workspace();
  std::filesystem::path workdir = turn_cfg.workdir;
  std::filesystem::create_directories(workdir);
  tools_internal::MutationScope mutations(workdir,dialog_id,turn_id,ctl.task_id);
  log_turn(dialog_id, turn_id, "user: " + redact::text(user_text).substr(0, 200));
  log_turn(dialog_id, turn_id, "workdir=" + workdir.string());

  db.add_message(dialog_id, "user", user_text, turn_id);

  if (intent.needs_clarification && !cancelled(ctl)) {
    const std::string question =
        "Что именно нужно улучшить? Укажите конкретную проблему, функцию/экран, файлы или ожидаемый результат.";
    const std::string question_id = std::to_string(dialog_id) + ":" + std::to_string(turn_id) + ":clarify";
    AskHub::instance().begin(dialog_id, question, question_id);
    v1::TurnEvent ev; ev.set_dialog_id(dialog_id); ev.set_turn_id(turn_id);
    auto* ask = ev.mutable_ask_user(); ask->set_question(question); ask->set_question_id(question_id);
    ask->add_options("Исправить конкретный дефект");
    ask->add_options("Выбрать один небольшой подэтап после анализа");
    emit(sink, ev);
    if (const auto answer = AskHub::instance().wait_answer(dialog_id, ctl.cancel)) {
      effective_user_text += "\nУточнение пользователя: " + *answer;
      log_turn(dialog_id, turn_id, "clarification: " + redact::text(*answer).substr(0, 200));
      refresh_intent();
      if (*answer == "Выбрать один небольшой подэтап после анализа") intent.needs_clarification = false;
    }
  }
  turn_cfg.read_only = input_cfg.read_only || intent.forbid_mutation || intent.needs_clarification;

  const bool need_mutation = intent.expects_mutation;
  const bool list_only = modes::turn_is_list_only(user_text);
  if (list_only) {
    log_turn(dialog_id, turn_id, "mode=list_only");
  }

  MemoryState memory = MemoryState::load(db.list_memory(kMemoryStoreLimit), db.next_memory_id());
  DurableContext durable(db, dialog_id, workdir);
  WorkspaceRules rules(workdir);
  const auto root_rules = rules.refresh();
  std::string rules_error = root_rules.error;
  const std::string system_text = std::string(kSystemPrompt) + memory.inject_block() + durable.render() +
      "\nЗакреплённые пользователем правила диалога (действуют до изменения пользователем):\n" +
      turn_cfg.dialog_instructions +
      "\nТекущее задание пользователя (сохраняется при сжатии):\n" + effective_user_text;

  nlohmann::json messages = nlohmann::json::array();
  messages.push_back({{"role", "system"}, {"content", system_text + rules.render()}});
  for (const auto& m : db.list_messages(dialog_id, 400)) {
    if (m.role != "user" && m.role != "assistant" && m.role != "system") {
      continue;
    }
    // Drop poison that breaks Gemma4 chat template / grammar sampler (HTTP 400).
    if (m.role == "assistant") {
      if (m.text.empty() || m.text == "(пусто)" || unparsed_tool_tail(m.text) ||
          m.text.find("Failed to initialize samplers") != std::string::npos ||
          m.text.find("ошибка LLM: HTTP 400") != std::string::npos ||
          m.text.find("llama.cpp/Gemma не смог") != std::string::npos) {
        continue;
      }
    }
    messages.push_back({{"role", m.role}, {"content", m.text}});
  }

  FilePassState pass = [&]() {
    if (const auto row = db.get_file_pass(dialog_id)) {
      return FilePassState::from_row(*row);
    }
    return FilePassState{};
  }();
  auto emit_file_pass = [&](const FilePassState& ps) {
    v1::TurnEvent ev;
    ev.set_dialog_id(dialog_id);
    ev.set_turn_id(turn_id);
    fill_turn_event_file_pass(ps, ev);
    emit(sink, ev);
  };

  PlanState plan = [&]() {
    if (const auto row = db.get_plan(dialog_id)) {
      return PlanState::from_row(*row);
    }
    return PlanState{};
  }();
  auto emit_plan = [&](const PlanState& ps) {
    v1::TurnEvent ev;
    ev.set_dialog_id(dialog_id);
    ev.set_turn_id(turn_id);
    fill_turn_event_plan(ps, ev);
    emit(sink, ev);
  };

  auto emit_memory = [&](const MemoryState& mem) {
    v1::TurnEvent ev;
    ev.set_dialog_id(dialog_id);
    ev.set_turn_id(turn_id);
    fill_turn_event_memory(mem, ev);
    emit(sink, ev);
  };

  nlohmann::json tools = nlohmann::json::parse(tools_openai_schema_json());
  filter_tool_schemas(tools, turn_cfg.llm_profile);
  for (const auto& schema : TaskStage::schemas()) tools.push_back(schema);
  if (ctl.grpc_tools) for (const auto& schema : nlohmann::json::parse(ctl.grpc_tools->schemas_json())) tools.push_back(schema);
  LlmConfig cfg;
  cfg.base_url = turn_cfg.llm_base_url;
  cfg.model = turn_cfg.llm_model;
  cfg.api_key = turn_cfg.llm_api_key;
  cfg.profile = turn_cfg.llm_profile;
  cfg.context_tokens = turn_cfg.context_tokens;
  cfg.temperature = turn_cfg.llm_temperature;

  std::string assistant_text;
  std::int64_t step_id = 0;
  bool mutation_ok = false;
  bool used_tools = false;
  bool exhausted = true;
  int fail_streak = 0;
  int tool_steps = 0;
  int read_steps = 0;
  int mutation_steps = 0;
  int write_ok = 0;
  int tool_fail = 0;
  int resumes = 0;
  int verification_resumes = 0;
  std::string budget_error;
  VerificationState verification;
  const auto workflow = durable.workflow();
  TaskStage stage(need_mutation ? workflow.value("stage", nlohmann::json::object()) : nlohmann::json::object());
  if (need_mutation && workflow.contains("verification") && workflow["verification"].is_string())
    verification.restore(workflow["verification"].get<std::string>());
  auto save_workflow = [&] {
    if (need_mutation || stage.code_changed())
      durable.save_workflow({{"stage", stage.state()}, {"verification", verification.serialized()}});
  };
  TaskAcceptance acceptance;
  std::string acceptance_failure;
  if ((need_mutation || intent.requires_tools) && !list_only && rules_error.empty()) {
    v1::TurnEvent status; status.set_dialog_id(dialog_id); status.set_turn_id(turn_id);
    status.mutable_status()->set_text("составляю критерии проверки результата"); emit(sink, status);
    acceptance = TaskAcceptance::prepare(cfg, nlohmann::json{{"request", effective_user_text},
        {"dialog_rules", turn_cfg.dialog_instructions}, {"workspace_rules", rules.render()},
        {"continuation_context", workflow}}.dump(), ctl.cancel);
    if(!cancelled(ctl))mutations.record_acceptance(nlohmann::json::parse(acceptance.serialized()));
  }
  ReadEditGuard read_edits;
  int same_key_hits = 0;
  std::string last_tool_key;
  TurnEditStats edit_stats;
  const int max_steps = std::max(1, std::min(turn_cfg.max_steps > 0 ? turn_cfg.max_steps : 64, 256));
  // Gemma often omits path on edit_file — recover from read_file buffers in this turn.
  std::vector<std::pair<std::string, std::string>> recent_reads;

  auto publish_context = [&]() {
    bool compressed = false;
    try {
      const int before = context::estimate_tokens(messages);
      llm_internal::fit_chat_budget(messages, cfg, tools);
      if(const auto exact=llm_internal::exact_prompt_tokens(cfg,messages,tools)){
        const int estimated=llm_internal::request_prompt_tokens(
            llm_internal::build_chat_body(cfg,messages,tools,"auto",true,cfg.profile));
        cfg.prompt_token_scale=std::max(cfg.prompt_token_scale,1.05*(*exact)/std::max(1,estimated));
        llm_internal::fit_chat_budget(messages,cfg,tools);
      }
      compressed = context::estimate_tokens(messages) < before;
    } catch (const std::exception& e) {
      log_turn(dialog_id, turn_id, std::string("compact_skip ") + e.what());
    }
    const int used = llm_internal::request_prompt_tokens(
        llm_internal::build_chat_body(cfg, messages, tools, "auto", true, cfg.profile), cfg.prompt_token_scale);
    v1::TurnEvent ev;
    ev.set_dialog_id(dialog_id);
    ev.set_turn_id(turn_id);
    auto* meter = ev.mutable_context_use();
    meter->set_used_tokens(used);
    meter->set_reserved_output_tokens(llm_internal::response_reserve(cfg));
    meter->set_window_tokens(turn_cfg.context_tokens);
    meter->set_compressed(compressed);
    emit(sink, ev);
    if (compressed) {
      v1::TurnEvent st;
      st.set_dialog_id(dialog_id);
      st.set_turn_id(turn_id);
      st.mutable_status()->set_text("сжимаю контекст");
      emit(sink, st);
    }
  };

  for (int round = 0; round < max_steps; ++round) {
    if(!cancelled(ctl)&&ctl.check_workspace)ctl.check_workspace();
    if (cancelled(ctl)) {
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      ev.mutable_error()->set_message("отменено");
      emit(sink, ev);
      exhausted = false;
      break;
    }

    {
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      ev.mutable_status()->set_text("думаю…");
      emit(sink, ev);
    }

    // Always "auto" with llama.cpp+Gemma4. "required" builds a grammar that fails sampler init → HTTP 400.
    publish_context();
    bool streamed_content = false;
    LlmStreamSink llm_stream;
    llm_stream.on_token = [&](const std::string& delta) {
      if (delta.empty()) {
        return true;
      }
      // Mutation reports are published only after verification. A streamed model
      // claim must not bypass the authoritative final result.
      if (need_mutation || stage.active() || stage.has_deferred() || stage.code_changed()) return true;
      streamed_content = true;
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      ev.mutable_token()->set_text(delta);
      return emit(sink, ev);
    };
    llm_stream.on_thought = [&](const std::string& delta) {
      // SSE reasoning is token-sized. Do not emit Thought here — that flooded
      // steps/UI (~thousands/turn). Full reasoning is emitted once after reply.
      (void)delta;
      return true;
    };
    LlmReply reply;
    const auto recheck = verification.due();
    if (!rules_error.empty()) reply.error = rules_error;
    else if (!recheck.empty()) {
      reply.ok = true;
      reply.message = {{"role", "assistant"}, {"content", nullptr}, {"tool_calls", nlohmann::json::array({
          nlohmann::json{{"id", "verification_" + std::to_string(round)}, {"type", "function"},
            {"function", {{"name", "run_shell"}, {"arguments", nlohmann::json{{"command", recheck}}.dump()}}}}})}};
    } else reply = chat_completions(cfg, messages, tools, "auto", ctl.cancel, &llm_stream);
    if (reply.prompt_tokens > 0) {
      const int estimated = llm_internal::request_prompt_tokens(
          llm_internal::build_chat_body(cfg, messages, tools, "auto", true, cfg.profile));
      cfg.prompt_token_scale = std::max(cfg.prompt_token_scale, 1.1 * reply.prompt_tokens / std::max(1, estimated));
      v1::TurnEvent usage;
      usage.set_dialog_id(dialog_id); usage.set_turn_id(turn_id);
      usage.mutable_context_use()->set_used_tokens(reply.prompt_tokens);
      usage.mutable_context_use()->set_provider_reported(true);
      usage.mutable_context_use()->set_completion_tokens(reply.completion_tokens);
      usage.mutable_context_use()->set_reserved_output_tokens(llm_internal::response_reserve(cfg));
      usage.mutable_context_use()->set_window_tokens(turn_cfg.context_tokens);
      emit(sink, usage);
    }
    if (reply.cancelled || (!reply.ok && cancelled(ctl))) {
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      ev.mutable_error()->set_message("отменено");
      emit(sink, ev);
      exhausted = false;
      break;
    }
    if (!reply.ok) {
      log_turn(dialog_id, turn_id, "llm_error " + redact::text(reply.error).substr(0, 200));
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      ev.mutable_error()->set_message(reply.error);
      emit(sink, ev);
      // Keep error short in DB so it does not re-poison the next turn's prompt.
      const std::string saved_text =
          reply.error.find("Failed to initialize samplers") != std::string::npos ||
                  reply.error.find("не смог собрать grammar") != std::string::npos
              ? "ошибка LLM: grammar/sampler (llama.cpp+Gemma). Создай новый диалог или повтори короткой фразой."
              : ("ошибка LLM: " + reply.error.substr(0, 240));
      const auto saved = db.add_message(dialog_id, "assistant", saved_text, turn_id);
      v1::TurnEvent done;
      done.set_dialog_id(dialog_id);
      done.set_turn_id(turn_id);
      fill_done(done, saved, edit_stats, mutations.path());
      emit(sink, done);
      return saved.id;
    }

    messages.push_back(reply.message);

    // Gemma4 thinking lands in reasoning_content (not content). Stream it as Thought
    // through orch → UI; keep full message (incl. reasoning) for the next LLM round.
    auto json_text = [](const nlohmann::json& v, auto&& self) -> std::string {
      if (v.is_string()) {
        return v.get<std::string>();
      }
      if (v.is_array()) {
        std::string out;
        for (const auto& part : v) {
          const std::string piece = self(part, self);
          if (piece.empty()) {
            continue;
          }
          if (!out.empty()) {
            out.push_back('\n');
          }
          out += piece;
        }
        return out;
      }
      if (v.is_object()) {
        if (v.contains("text")) {
          return self(v["text"], self);
        }
        if (v.contains("content")) {
          return self(v["content"], self);
        }
      }
      return {};
    };
    auto emit_thought = [&](const std::string& text) {
      if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return;
      }
      v1::TurnEvent ev;
      ev.set_dialog_id(dialog_id);
      ev.set_turn_id(turn_id);
      ev.mutable_thought()->set_text(text);
      emit(sink, ev);
    };
    std::string thought;
    if (reply.message.contains("reasoning_content")) {
      thought = json_text(reply.message["reasoning_content"], json_text);
    }
    if (thought.empty() && reply.message.contains("reasoning")) {
      thought = json_text(reply.message["reasoning"], json_text);
    }
    // One Thought for the whole reasoning block (not per SSE crumb).
    emit_thought(thought);

    if (reply.message.contains("tool_calls") && reply.message["tool_calls"].is_array() &&
        !reply.message["tool_calls"].empty()) {
      used_tools = true;
      if (reply.message.contains("content") && reply.message["content"].is_string()) {
        // Visible content alongside tools (rare with thinking-on); still surface it.
        emit_thought(reply.message["content"].get<std::string>());
      }

      const std::size_t assistant_index = messages.size() - 1;
      std::size_t call_index = 0;
      auto calls = reply.message["tool_calls"];
      bool rules_changed_in_batch = false;
      for (const auto& call : calls) {
        const std::string tool = call.at("function").at("name").get<std::string>();
        if (tool_steps >= kMaxToolCallsPerTurn ||
            ((tool == "read_file" || tool == "quote_file") && read_steps >= kMaxReadCallsPerTurn) ||
            (tool_writes_workdir(tool) && mutation_steps >= kMaxWorkspaceMutationsPerTurn)) {
          budget_error = tool_steps >= kMaxToolCallsPerTurn
              ? "достигнут лимит вызовов инструментов за ход"
              : (tool_writes_workdir(tool) ? "достигнут лимит изменений workspace за ход"
                                           : "достигнут лимит чтений за ход");
          v1::TurnEvent ev; ev.set_dialog_id(dialog_id); ev.set_turn_id(turn_id);
          ev.mutable_error()->set_message(budget_error); emit(sink, ev);
          break;
        }
        ++step_id;
        std::string args = "{}";
        if (call.at("function").contains("arguments")) {
          if (call["function"]["arguments"].is_string()) {
            args = call["function"]["arguments"].get<std::string>();
          } else {
            args = call["function"]["arguments"].dump();
          }
        }
        const std::string call_id = call.value("id", "call");

        // Keep repaired arguments in the transcript too, so Gemma and summaries
        // see the same call that was actually executed.
        nlohmann::json canonical_args;
        std::string parse_error;
        if (parse_tool_args(args, canonical_args, parse_error)) args = canonical_args.dump();

        // Recover only an unambiguous target before gates, logging, and the loop key.
        if ((tool == "edit_file" || tool == "append_file") && !recent_reads.empty()) {
          nlohmann::json parsed;
          std::string error;
          if (parse_tool_args(args, parsed, error) && coerce_path_arg(parsed).empty()) {
            const std::string inferred = infer_path_from_reads(parsed, recent_reads);
            if (!inferred.empty()) {
              args = args_with_path(args, inferred);
              parse_tool_args(args, canonical_args, error);
            }
          }
        }
        messages[assistant_index]["tool_calls"][call_index++]["function"]["arguments"] = args;

        {
          v1::TurnEvent ev;
          ev.set_dialog_id(dialog_id);
          ev.set_turn_id(turn_id);
          ev.mutable_action()->set_tool(tool);
          ev.mutable_action()->set_args_json(args);
          ev.mutable_action()->set_step_id(step_id);
          emit(sink, ev);
        }

        const std::string call_key = tool_call_key(tool, args);
        if (call_key == last_tool_key) {
          ++same_key_hits;
        } else {
          last_tool_key = call_key;
          same_key_hits = 1;
        }

        if (same_key_hits >= 5) {
          v1::TurnEvent ev;
          ev.set_dialog_id(dialog_id);
          ev.set_turn_id(turn_id);
          ev.mutable_error()->set_message("цикл без прогресса: один и тот же вызов инструмента 5 раз");
          emit(sink, ev);
          const auto saved =
              db.add_message(dialog_id, "assistant", "ход остановлен: зацикливание на одном вызове.", turn_id);
          v1::TurnEvent done;
          done.set_dialog_id(dialog_id);
          done.set_turn_id(turn_id);
          fill_done(done, saved, edit_stats, mutations.path());
          emit(sink, done);
          return saved.id;
        }

        const auto tool_t0 = std::chrono::steady_clock::now();
        ToolResult result;
        bool foreground_shell_ran=false,shell_refused=false;
        try {
          if(ctl.check_workspace)ctl.check_workspace();
          if(!turn_cfg.read_only)
            mutations.record_acceptance(nlohmann::json::parse(acceptance.serialized()));
          const auto order_failure=acceptance.before_tool(workdir,tool,tool_writes_workdir(tool)||tool=="run_shell"||tool=="start_background"||(ctl.grpc_tools&&ctl.grpc_tools->mutating(tool)));
          if(!order_failure.empty())throw std::runtime_error(order_failure);
          const auto update = rules.for_tool(tool, canonical_args);
          if (update.changed) {
            messages[0]["content"] = system_text + rules.render();
            rules_changed_in_batch = true;
          }
          if (!update.ok) {
            rules_error = update.error;
            result = {false, update.error};
          } else if (rules_changed_in_batch) {
            result = {false, "RULES_UPDATED: правила workspace добавлены или изменились. Сначала учти их в system context, затем повтори подходящее действие."};
          } else if (same_key_hits >= 3) {
            result = {false,
                      "повтор того же вызова 3 раза — смени инструмент или аргументы (на 5-м ход остановится)."};
          } else if (const ToolResult gate = creation_gate(intent, workdir, tool, args,true); !gate.ok) {
            result = gate;
          } else if (turn_cfg.read_only && tool_writes_workdir(tool)) {
            result = {false, "read_only: инструмент изменяет файлы — отключён"};
          } else if (list_only && !modes::tool_allowed_in_list_only(tool)) {
            result = {false, modes::list_only_block_message()};
          } else if (const ToolResult gate = file_pass_gate_tool(pass, tool, args); !gate.ok) {
            result = gate;
          } else if (const ToolResult gate = read_edits.overwrite_gate(workdir, tool, args); !gate.ok) {
            result = gate;
          } else if (tool == "task_stage") {
            result = stage.set(canonical_args);
          } else if (tool == "defer_task_stage") {
            const auto checks = stage.active() ? stage.state()["checks"].get<std::vector<std::string>>() : std::vector<std::string>();
            result = stage.defer(canonical_args);
            if (result.ok) verification.defer_checks(checks);
          } else if (tool == "abandon_edit") {
            if (!canonical_args.contains("path") || !canonical_args["path"].is_string() ||
                !canonical_args.contains("reason") || !canonical_args["reason"].is_string())
              result = {false, "abandon_edit: нужны path и reason"};
            else {
              const bool ok = verification.abandon(canonical_args["path"], canonical_args["reason"]);
              result = {ok, ok ? "Правка отмечена невыполненной; итог будет частичным." : "Нет такой отклонённой правки либо причина недопустима."};
            }
          } else if (ctl.grpc_tools && tool.rfind("grpc_", 0) == 0) {
            const auto rpc = ctl.grpc_tools->invoke(tool, args, turn_cfg.read_only, ctl.cancel,
                [&](const std::string& command) {
                  const auto token = ConfirmHub::instance().begin(dialog_id, command);
                  v1::TurnEvent ev; ev.set_dialog_id(dialog_id); ev.set_turn_id(turn_id);
                  auto* confirm = ev.mutable_confirm_needed(); confirm->set_confirm_token(token);
                  confirm->set_command(command); confirm->set_consequence("Изменит состояние внешнего gRPC сервиса. Без подтверждения вызов не выполняется.");
                  emit(sink, ev);
                  return ConfirmHub::instance().wait_decision(dialog_id, token, ctl.cancel);
                });
            result = {rpc.ok, rpc.output};
            if (result.ok && ctl.grpc_tools->mutating(tool)) { mutation_ok = true; acceptance.mutation(); }
          } else if (tool == "file_pass_set") {
            nlohmann::json parsed;
            std::string perr;
            if (!parse_tool_args(args, parsed, perr)) {
              result = {false, perr};
            } else {
              result = pass.set_paths(parsed);
              if (result.ok) {
                emit_file_pass(pass);
              }
            }
          } else if (tool == "file_pass_review") {
            nlohmann::json parsed;
            std::string perr;
            result = parse_tool_args(args, parsed, perr) ? pass.review_without_changes(parsed) : ToolResult{false, perr};
            if (result.ok) emit_file_pass(pass);
          } else if (tool == "file_pass_clear") {
            result = pass.clear();
            if (result.ok) {
              emit_file_pass(pass);
            }
          } else if (tool == "plan_set") {
            nlohmann::json parsed;
            std::string perr;
            if (!parse_tool_args(args, parsed, perr)) {
              result = {false, perr};
            } else {
              result = plan.set_steps(parsed);
              if (result.ok) {
                emit_plan(plan);
              }
            }
          } else if (tool == "plan_advance") {
            result = plan.advance(pass);
            if (result.ok) {
              emit_plan(plan);
            }
          } else if (tool == "plan_fail") {
            nlohmann::json parsed;
            std::string perr;
            if (!parse_tool_args(args, parsed, perr)) {
              result = {false, perr};
            } else {
              result = plan.mark_failed(parsed);
              if (result.ok) {
                emit_plan(plan);
              }
            }
          } else if (tool == "plan_retry") {
            result = plan.retry();
            if (result.ok) {
              emit_plan(plan);
            }
          } else if (tool == "plan_edit") {
            nlohmann::json parsed;
            std::string perr;
            if (!parse_tool_args(args, parsed, perr)) {
              result = {false, perr};
            } else {
              result = plan.edit_step(parsed);
              if (result.ok) {
                emit_plan(plan);
              }
            }
          } else if (tool == "remember") {
            nlohmann::json parsed;
            std::string perr;
            if (!parse_tool_args(args, parsed, perr)) {
              result = {false, perr};
            } else {
              result = memory.remember(parsed);
              if (result.ok) {
                emit_memory(memory);
              }
            }
          } else if (tool == "forget") {
            nlohmann::json parsed;
            std::string perr;
            if (!parse_tool_args(args, parsed, perr)) {
              result = {false, perr};
            } else {
              result = memory.forget(parsed);
              if (result.ok) {
                emit_memory(memory);
              }
            }
          } else if (tool == "list_memory") {
            result = memory.list();
          } else if (tool == "delete_file" || tool == "delete_dir") {
            std::string path;
            try {
              path = nlohmann::json::parse(args).value("path", "");
            } catch (...) {
              path.clear();
            }
            if (path.empty()) {
              result = {false, "path required"};
            } else {
              const std::string cmd = tool + " " + path;
              const std::string token = ConfirmHub::instance().begin(dialog_id, cmd);
              {
                v1::TurnEvent ev;
                ev.set_dialog_id(dialog_id);
                ev.set_turn_id(turn_id);
                auto* c = ev.mutable_confirm_needed();
                c->set_confirm_token(token);
                c->set_command(cmd);
                c->set_consequence(tool == "delete_file"
                    ? "Удалит файл в workdir после сохранения защищённой резервной копии."
                    : "Удалит до 1024 обычных файлов/каталогов (до 64 MiB). Каждый файл попадёт в защищённый changeset; symlink и special file запрещены.");
                emit(sink, ev);
              }
              const bool accepted = ConfirmHub::instance().wait_decision(dialog_id, token, ctl.cancel);
              if (!accepted) {
                result = {false, cancelled(ctl) ? "отменено" : "отклонено"};
              } else if (tool == "delete_file") {
                result = tool_delete_file(workdir, path);
              } else {
                result = tool_delete_dir(workdir, path);
              }
            }
          } else if (tool == "ask_user") {
            std::string question;
            std::vector<std::string> options;
            try {
              const auto j = nlohmann::json::parse(args);
              question = j.value("question", "");
              if (j.contains("options") && j["options"].is_array()) {
                for (const auto& o : j["options"]) {
                  if (o.is_string()) {
                    options.push_back(o.get<std::string>());
                  }
                }
              }
            } catch (...) {
              question.clear();
            }
            if (question.empty()) {
              result = {false, "ask_user: нужен question"};
            } else {
              const auto question_id = std::to_string(dialog_id) + ":" + std::to_string(turn_id) + ":" + std::to_string(step_id);
              AskHub::instance().begin(dialog_id, question, question_id);
              {
                v1::TurnEvent ev;
                ev.set_dialog_id(dialog_id);
                ev.set_turn_id(turn_id);
                auto* a = ev.mutable_ask_user();
                a->set_question(question);
                a->set_question_id(question_id);
                for (const auto& o : options) {
                  a->add_options(o);
                }
                emit(sink, ev);
              }
              const auto ans = AskHub::instance().wait_answer(dialog_id, ctl.cancel);
              if (!ans) {
                result = {false, cancelled(ctl) ? "отменено" : "ответ не получен"};
              } else {
                result = {true, *ans};
              }
            }
          } else if (tool == "http_request" && canonical_args.value("method", "GET") != "GET" &&
                     canonical_args.value("method", "GET") != "get") {
            const auto method=canonical_args.value("method","GET"),url=canonical_args.value("url","");
            if(url.empty())result={false,"http_request: url required"};
            else{
              const std::string command=method+" "+url;
              const std::string token=ConfirmHub::instance().begin(dialog_id,command);
              v1::TurnEvent ev;ev.set_dialog_id(dialog_id);ev.set_turn_id(turn_id);
              auto* confirm=ev.mutable_confirm_needed();confirm->set_confirm_token(token);confirm->set_command(command);
              confirm->set_consequence("Отправит изменяющий HTTP-запрос внешнему сервису. Повтор после таймаута не выполняется автоматически.");
              emit(sink,ev);
              if(!ConfirmHub::instance().wait_decision(dialog_id,token,ctl.cancel))
                result={false,cancelled(ctl)?"отменено":"HTTP-запрос отклонён"};
              else result=dispatch_tool(workdir,tool,args,turn_cfg.list_ignore,turn_cfg.read_only);
            }
          } else if (tool == "run_shell" || tool == "start_background") {
            std::string command;
            try {
              command = nlohmann::json::parse(args).value("command", "");
            } catch (...) {
              command.clear();
            }
            const bool background = tool == "start_background";
            const bool ask_delete = shell_policy::is_delete_command(command) && turn_cfg.delete_confirm;
            const bool ask = ask_delete || shell_policy::needs_confirm(command, turn_cfg.shell_auto, turn_cfg.shell_ask,
                                                                       turn_cfg.shell_allow_all);
            const ShellRunOptions shell_opts{.timeout_sec = turn_cfg.shell_timeout_sec,
                                             .shell_net = turn_cfg.shell_net,
                                             .allow_unsandboxed = turn_cfg.allow_unsandboxed_shell&&!intent.forbid_create,
                                             .read_only = turn_cfg.read_only||intent.forbid_create, .cancel = ctl.cancel,
                                             .dialog_id = dialog_id, .task_id = ctl.task_id, .turn_id = turn_id};
            auto run = [&]() {
              auto executed=execute_shell(db, workdir, command, turn_cfg.list_ignore, background, shell_opts);
              foreground_shell_ran=!background&&!shell_policy::is_background(command);return executed;
            };
            if (command.empty()) {
              result = {false, "empty command"};
            } else if(acceptance.command_blocked(command)) {
              shell_refused=true;
              result = {false,"Команда уже отклонена в этом ходе; повторное подтверждение не запрашивается."};
            } else if (turn_cfg.read_only && shell_mutates_files(command)) {
              shell_refused=true;
              result = {false, "read_only: shell изменяет файлы — отключён"};
            } else if (shell_mutates_files(command)) {
              shell_refused=true;
              result = {false,
                        "shell не для записи файлов (>, >>, tee, sed -i…). "
                        "Используй edit_file / create_file / append_file."};
            } else if (!ask) {
              v1::TurnEvent ev;
              ev.set_dialog_id(dialog_id);
              ev.set_turn_id(turn_id);
              ev.mutable_status()->set_text((background || shell_policy::is_background(command) ? "в фоне: " : "без вопроса: ") +
                                            command);
              emit(sink, ev);
              result = run();
            } else {
              const std::string token = ConfirmHub::instance().begin(dialog_id, command);
              {
                v1::TurnEvent ev;
                ev.set_dialog_id(dialog_id);
                ev.set_turn_id(turn_id);
                auto* c = ev.mutable_confirm_needed();
                c->set_confirm_token(token);
                c->set_command(command);
                c->set_consequence(ask_delete
                                       ? "Удалит файлы в workdir. Без подтверждения команда не запустится."
                                       : intent.forbid_create?"Выполнит shell с workdir только для чтения: создание и изменение файлов запрещены. Без подтверждения команда не запустится.":"Выполнит shell в workdir. Без подтверждения команда не запустится.");
                emit(sink, ev);
              }
              const bool accepted = ConfirmHub::instance().wait_decision(dialog_id, token, ctl.cancel);
              if (!accepted) {
                shell_refused=true;
                result = {false, "оператор отклонил или отменил shell"};
              } else {
                result = run();
              }
            }
          } else {
            if (tool == "batch_replace") result = read_edits.batch_replace(workdir, args);
            else if (tool == "edit_file" || tool == "append_file" || tool == "replace_text") result = read_edits.edit(workdir, tool, args);
            else if (tool == "write_file" && canonical_args.value("append", false))
              result = read_edits.edit(workdir, "append_file", args);
            else result = dispatch_tool(workdir, tool, args, turn_cfg.list_ignore, turn_cfg.read_only);
            if (result.ok && (tool == "read_file" || tool == "quote_file")) {
              nlohmann::json parsed;
              std::string perr;
              if (parse_tool_args(args, parsed, perr)) {
                const std::string p = coerce_path_arg(parsed);
                if (!p.empty()) {
                  recent_reads.erase(std::remove_if(recent_reads.begin(), recent_reads.end(),
                      [&](const auto& read) { return read.first == p; }), recent_reads.end());
                  recent_reads.push_back({p, result.output});
                  if (recent_reads.size() > 12) {
                    recent_reads.erase(recent_reads.begin());
                  }
                }
              }
            }
          }
        } catch (const std::exception& e) {
          result = {false, std::string("неверные аргументы инструмента: ") + e.what()};
        }
        result.output = redact::text(result.output);
        result.diff = redact::text(result.diff);
        if (ctl.grpc_tools && tool.rfind("grpc_", 0) == 0) verification.external_result(tool, result.ok);

        if (result.ok && (tool == "read_file" || tool == "quote_file")) {
          nlohmann::json parsed;
          std::string perr;
          if (parse_tool_args(args, parsed, perr)) {
            const std::string p = coerce_path_arg(parsed);
            if (!p.empty()) {
              read_edits.record(workdir, p, context::tool_output_for_model(result.output,
                  context::source_output_limit(turn_cfg.context_tokens)));
              pass.on_read_ok(p);
              emit_file_pass(pass);
            }
          }
        }
        if (tool == "run_shell" && canonical_args.is_object() && canonical_args.contains("command") &&
            canonical_args["command"].is_string())
          verification.observe(canonical_args["command"].get<std::string>(), result.ok, result.output);
        durable.record(tool, canonical_args, result.ok, turn_id);
        verification.file_result(tool, coerce_path_arg(canonical_args), result.ok, result.output, canonical_args);
        if (result.ok && tool_writes_workdir(tool)) {
          verification.mutation();
          acceptance.mutation();
          if (tool == "batch_replace") read_edits.invalidate();
          else if (is_mutation_tool(tool)) read_edits.invalidate(workdir, coerce_path_arg(canonical_args));
          else read_edits.invalidate();
          mutation_ok = true;
          // A cached read must never select a path after the file has changed.
          recent_reads.clear();
        }
        acceptance.observe(workdir,tool,canonical_args,result,foreground_shell_ran,shell_refused);
        stage.observe(tool,canonical_args,result,foreground_shell_ran);
        save_workflow();
        if (result.ok && is_mutation_tool(tool)) {
          note_mutation_tool(edit_stats, tool, args);
          nlohmann::json parsed;
          std::string perr;
          if (parse_tool_args(args, parsed, perr)) {
            const std::string p = coerce_path_arg(parsed);
            if (!p.empty()) {
              pass.on_mutation_ok(p);
              if (pass.active()) {
                emit_file_pass(pass);
              }
            }
          }
          mutation_ok = true;
          ++write_ok;
        }
        if (result.ok) {
          fail_streak = 0;
        } else {
          ++fail_streak;
        }

        {
          const auto tool_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - tool_t0)
                                   .count();
          v1::TurnEvent ev;
          ev.set_dialog_id(dialog_id);
          ev.set_turn_id(turn_id);
          auto* obs = ev.mutable_observation();
          obs->set_tool(tool);
          obs->set_args_json(args);
          obs->set_ok(result.ok);
          obs->set_output(result.output);
          obs->set_step_id(step_id);
          obs->set_duration_ms(tool_ms);
          if (!result.diff.empty()) {
            obs->set_diff(result.diff);
          }
          emit(sink, ev);
        }
        log_turn(dialog_id, turn_id,
                 std::string("tool ") + tool + (result.ok ? " ok " : " FAIL ") + result.output.substr(0, 240));
        ++tool_steps;
        if (tool == "read_file" || tool == "quote_file") ++read_steps;
        if (result.ok && tool_writes_workdir(tool)) ++mutation_steps;
        if (!result.ok) {
          ++tool_fail;
        }
        if (fail_streak >= 6) {
          v1::TurnEvent ev;
          ev.set_dialog_id(dialog_id);
          ev.set_turn_id(turn_id);
          ev.mutable_error()->set_message("слишком много ошибок инструментов подряд");
          emit(sink, ev);
          const auto saved = db.add_message(
              dialog_id, "assistant",
              "ход остановлен: 6 неудачных вызовов подряд. Смотри шаги и начни новый ход.", turn_id);
          v1::TurnEvent done;
          done.set_dialog_id(dialog_id);
          done.set_turn_id(turn_id);
          fill_done(done, saved, edit_stats, mutations.path());
          emit(sink, done);
          return saved.id;
        }

        {
          const auto output_limit = (tool == "read_file" || tool == "quote_file")
              ? context::source_output_limit(turn_cfg.context_tokens) : 6000;
          std::string body = result.ok ? context::tool_output_for_model(result.output, output_limit)
                                       : (result.output +
                                          (result.output.find("path отсутствует") != std::string::npos
                                               ? " Сделай тот же вызов с path=… (имя файла из последнего read_file)."
                                               : ""));
          messages.push_back({{"role", "tool"}, {"tool_call_id", call_id},
                              {"content", tool == "ask_user" && result.ok ? result.output
                                  : std::string(result.ok ? "OK\n" : "ERROR\n") + body}});
        }
      }
      if (!budget_error.empty()) break;
      continue;
    }

    assistant_text = reply.content;
    if(!cancelled(ctl)&&ctl.check_workspace)ctl.check_workspace();
    acceptance_failure = acceptance.evaluate(workdir);
    const auto stage_failure = stage.pending();
    const auto pending_check = !verification.pending().empty() ? verification.pending() :
        !acceptance_failure.empty() ? acceptance_failure : stage_failure;
    if (!pending_check.empty()) {
      if (!verification.blocked() && !acceptance.blocked() && verification_resumes < 2 && round + 1 < max_steps) {
        ++verification_resumes;
        messages.push_back({{"role", "user"}, {"content", "Последняя проверка не прошла либо устарела после правок: " +
            pending_check + ". Результат не подтверждён. Исправь причину и повтори проверку. "
            "Не заявляй об успехе без успешной проверки; если действие заблокировано, объясни причину."}});
        assistant_text.clear();
        continue;
      }
      assistant_text = "Проверка не прошла: " + pending_check +
          ".\nРезультат не подтверждён. Правки требуют дальнейшей проверки.";
      v1::TurnEvent error; error.set_dialog_id(dialog_id); error.set_turn_id(turn_id);
      error.mutable_error()->set_message(assistant_text); emit(sink, error);
      exhausted = false;
      break;
    }
    if (stage.code_changed() && !stage.active() && !stage.has_deferred() && !acceptance.has_checks() && verification_resumes < 2 && round + 1 < max_steps) {
      ++verification_resumes;
      messages.push_back({{"role", "user"}, {"content", "Код записан, но поведение ещё не проверено. "
          "Зафиксируй выбранный подэтап инструментом task_stage: title, checks (1–4 точные команды с проверками поведения), "
          "remaining (что осталось). Выполни команды через run_shell после последней правки и исправь обнаруженные ошибки. "
          "Не объявляй весь проект готовым. Если проверка невозможна, объясни блокировку."}});
      assistant_text.clear();
      continue;
    }
    // Give a premature text-only finish a bounded chance to perform the task.
    // One successful edit can be sufficient; never impose a write quota.
    if (((need_mutation && !mutation_ok && !turn_cfg.read_only && !list_only) ||
         (intent.requires_tools && !used_tools)) && resumes < 2 && round + 1 < max_steps) {
      ++resumes;
      messages.push_back(
          {{"role", "user"},
           {"content", "Задание пока не выполнено инструментами. Если задача требует действий, "
                       "выполни их инструментами и проверь результат. Если правки не нужны "
                       "или действие заблокировано, объясни причину. Не придумывай файлы "
                       "и не добавляй код только ради количества записей."}});
      assistant_text.clear();
      continue;
    }
    if (need_mutation && !mutation_ok && !turn_cfg.read_only && !list_only) {
      assistant_text = "Файлы в этом ходе не изменены.\n" + assistant_text;
    }
    if (stage.code_changed() || stage.active() || stage.has_deferred() || verification.has_abandoned())
      assistant_text = stage.final_text(verification.pending(), acceptance.state() != "checks_passed", verification.has_abandoned());
    if (!assistant_text.empty() && !streamed_content) {
      v1::TurnEvent tok;
      tok.set_dialog_id(dialog_id);
      tok.set_turn_id(turn_id);
      tok.mutable_token()->set_text(assistant_text);
      emit(sink, tok);
    }
    exhausted = false;
    break;
  }

  if (stage.code_changed() || stage.active() || stage.has_deferred() || verification.has_abandoned())
    assistant_text = stage.final_text(verification.pending(), acceptance.state() != "checks_passed", verification.has_abandoned());
  if (cancelled(ctl)) assistant_text = "Ход отменён; выполнение задачи не завершено.\n" + assistant_text;
  if (!acceptance_failure.empty()) assistant_text = "Проверка требований не прошла: " + acceptance_failure + "\n" + assistant_text;
  if (!rules_error.empty()) assistant_text = "Ошибка правил проекта: " + rules_error + "\n" + assistant_text;
  if (assistant_text.empty()) {
    if (cancelled(ctl)) {
      assistant_text = "ход отменён";
    } else if (mutation_ok) {
      assistant_text = write_ok > 0 ? "Правки на диске есть (см. шаги хода)." : "Внешний сервис подтвердил выполнение вызова.";
    } else if (need_mutation && !used_tools) {
      assistant_text = "Не меняла файлы: модель не вызвала инструменты.";
    } else {
      assistant_text = "Нет текста ответа.";
    }
  }
  if (exhausted && !cancelled(ctl)) {
    const std::string limit = budget_error.empty() ? "Достигнут лимит шагов" : budget_error;
    assistant_text = limit + "; задача может быть не завершена.\n" + assistant_text;
    v1::TurnEvent error;
    error.set_dialog_id(dialog_id);
    error.set_turn_id(turn_id);
    error.mutable_error()->set_message(budget_error.empty() ? "достигнут лимит шагов" : budget_error);
    emit(sink, error);
  }
  if (!verification.pending().empty() && assistant_text.find("Проверка не прошла:") == std::string::npos)
    assistant_text = "Проверка не прошла: " + verification.pending() + ".\nРезультат не подтверждён.\n" + assistant_text;
  if (mutation_ok && (acceptance.state() == "unverified" || acceptance.state() == "partial"))
    assistant_text = "Автоматическая проверка результата неполная: " + acceptance.note() + "\n" + assistant_text;
  save_workflow();
  const auto saved = db.add_message(dialog_id, "assistant", assistant_text, turn_id);
  v1::TurnEvent done;
  done.set_dialog_id(dialog_id);
  done.set_turn_id(turn_id);
  fill_done(done, saved, edit_stats, mutations.path());
  const bool incomplete = !rules_error.empty() || exhausted || !verification.pending().empty() || !acceptance_failure.empty() || !stage.pending().empty() ||
      (need_mutation && !mutation_ok && !turn_cfg.read_only && !list_only) ||
      (intent.requires_tools && !used_tools);
  done.mutable_done()->set_outcome(cancelled(ctl) ? "cancelled" : incomplete ? "failed" : "done");
  done.mutable_done()->set_error_code(cancelled(ctl) ? "cancelled" : incomplete ? "incomplete" : "");
  done.mutable_done()->set_verification_state(!verification.pending().empty() ? "failed" :
      !stage.pending().empty() || acceptance.state() == "failed" ? "failed" : verification.has_abandoned() || stage.has_deferred() ? "partial" :
      stage.passed() && acceptance.state() == "unverified" ? "partial" :
      mutation_ok || need_mutation || acceptance.has_checks() ? acceptance.state() : std::string());
  done.mutable_done()->set_checked_revision(acceptance.checked_revision());
  done.mutable_done()->set_verification_plan_json(acceptance.serialized());
  emit(sink, done);
  log_turn(dialog_id, turn_id,
           "summary tools=" + std::to_string(tool_steps) + " writes_ok=" + std::to_string(write_ok) +
               " fails=" + std::to_string(tool_fail) + " resumes=" + std::to_string(resumes));
  return saved.id;
}

}  // namespace arli
