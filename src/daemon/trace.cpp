#include "trace.hpp"

#include <nlohmann/json.hpp>
#include <google/protobuf/util/json_util.h>

namespace arli::trace {

using nlohmann::json;

std::optional<StepRow> step_from_event(const v1::TurnEvent& ev, std::int64_t task_id) {
  StepRow s;
  s.dialog_id = ev.dialog_id();
  s.turn_id = ev.turn_id();
  s.task_id = task_id;
  if (ev.has_thought()) {
    s.kind = "thought";
    s.output = ev.thought().text();
  } else if (ev.has_token()) {
    s.kind = "token";
    s.output = ev.token().text();
  } else if (ev.has_action()) {
    s.kind = "action";
    s.tool = ev.action().tool();
    s.args_json = ev.action().args_json();
    s.step_id = ev.action().step_id();
  } else if (ev.has_observation()) {
    s.kind = "observation";
    s.tool = ev.observation().tool();
    s.args_json = ev.observation().args_json();
    s.ok = ev.observation().ok();
    s.output = ev.observation().output();
    s.step_id = ev.observation().step_id();
    s.diff = ev.observation().diff();
    if (ev.observation().duration_ms() > 0) {
      s.duration_ms = ev.observation().duration_ms();
    }
  } else if (ev.has_confirm_needed()) {
    s.kind = "confirm";
    s.tool = "run_shell";
    s.args_json = json{{"token", ev.confirm_needed().confirm_token()},
                       {"command", ev.confirm_needed().command()},
                       {"consequence", ev.confirm_needed().consequence()}}
                      .dump();
    s.output = ev.confirm_needed().command();
  } else if (ev.has_ask_user()) {
    s.kind = "ask_user";
    json opts = json::array();
    for (const auto& o : ev.ask_user().options()) {
      opts.push_back(o);
    }
    s.args_json = json{{"question", ev.ask_user().question()}, {"question_id", ev.ask_user().question_id()}, {"options", opts}}.dump();
    s.output = ev.ask_user().question();
  } else if (ev.has_status()) {
    s.kind = "status";
    s.output = ev.status().text();
  } else if (ev.has_error()) {
    s.kind = "error";
    s.ok = false;
    s.output = ev.error().message();
  } else if (ev.has_done()) {
    s.kind = "done";
    google::protobuf::util::MessageToJsonString(ev.done(), &s.args_json);
    s.output = ev.done().assistant_text();
    s.step_id = ev.done().message_id();
  } else {
    return std::nullopt;
  }
  return s;
}

v1::TurnEvent event_from_step(const StepRow& s) {
  v1::TurnEvent ev;
  ev.set_dialog_id(s.dialog_id);
  ev.set_turn_id(s.turn_id);
  if (s.kind == "thought") {
    ev.mutable_thought()->set_text(s.output);
  } else if (s.kind == "token") {
    ev.mutable_token()->set_text(s.output);
  } else if (s.kind == "action") {
    auto* a = ev.mutable_action();
    a->set_tool(s.tool);
    a->set_args_json(s.args_json);
    a->set_step_id(s.step_id);
  } else if (s.kind == "observation") {
    auto* o = ev.mutable_observation();
    o->set_tool(s.tool);
    o->set_args_json(s.args_json);
    o->set_ok(s.ok);
    o->set_output(s.output);
    o->set_step_id(s.step_id);
    o->set_diff(s.diff);
    o->set_duration_ms(s.duration_ms);
  } else if (s.kind == "confirm") {
    auto* c = ev.mutable_confirm_needed();
    try {
      const json j = json::parse(s.args_json);
      c->set_confirm_token(j.value("token", ""));
      c->set_command(j.value("command", ""));
      c->set_consequence(j.value("consequence", ""));
    } catch (...) {
      c->set_command(s.output);
    }
  } else if (s.kind == "ask_user") {
    auto* a = ev.mutable_ask_user();
    try {
      const json j = json::parse(s.args_json);
      a->set_question(j.value("question", ""));
      a->set_question_id(j.value("question_id", ""));
      for (const auto& o : j.value("options", json::array())) {
        a->add_options(o.get<std::string>());
      }
    } catch (...) {
      a->set_question(s.output);
    }
  } else if (s.kind == "status") {
    ev.mutable_status()->set_text(s.output);
  } else if (s.kind == "error") {
    ev.mutable_error()->set_message(s.output);
  } else if (s.kind == "done") {
    if (!s.args_json.empty()) google::protobuf::util::JsonStringToMessage(s.args_json, ev.mutable_done());
    ev.mutable_done()->set_assistant_text(s.output);
    ev.mutable_done()->set_message_id(s.step_id);
  }
  return ev;
}

void fill_trace_item(const StepRow& s, v1::TraceItem* out) {
  out->set_row_id(s.id);
  out->set_kind(s.kind);
  out->set_text(s.output);
  out->set_tool(s.tool);
  out->set_args_json(s.args_json);
  out->set_step_id(s.step_id);
  out->set_ok(s.ok);
  out->set_duration_ms(s.duration_ms);
  out->set_created_at(s.created_at);
  out->set_turn_id(s.turn_id);
  out->set_diff(s.diff);
  if (s.kind == "confirm" || s.kind == "ask_user") {
    out->set_detail_json(s.args_json);
  }
}

}  // namespace arli::trace
