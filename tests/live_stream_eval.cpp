// Opt-in: real local model + real daemon/worker, reconnecting the UI RPC only.
#include "lifecycle_harness.hpp"
#include "db.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
using namespace lt;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
  if (argc != 6 && argc != 7) return 2;
  const bool interaction = argc == 7 && std::string(argv[6]) == "--ask";
  const bool pins_mode = argc == 7 && std::string(argv[6]) == "--instructions";
  const bool rules_mode = argc == 7 && std::string(argv[6]) == "--rules";
  if (argc == 7 && !interaction && !rules_mode && !pins_mode) return 2; // daemon, agent, base URL, model, report.json
  char tmp[] = "/tmp/arli-live-resume-XXXXXX"; if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp), wd = root / "workspace"; fs::create_directories(wd);
  if (rules_mode) {
    fs::create_directory(wd / "nested");
    std::ofstream(wd / "AGENTS.md") << "Соблюдай AGENTS.md вложенного каталога перед созданием файлов в нём.";
    std::ofstream(wd / "nested/AGENTS.md") << "Файл result.txt в этом каталоге должен содержать ровно пять байтов ready без перевода строки. Других файлов не создавай.";
  }
  const auto endpoint = "127.0.0.1:" + std::to_string(free_port());
  Proc daemon;
  daemon.start(argv[1], {endpoint}, {{"ARLI_STATE_DIR", tmp}, {"ARLI_AGENT_BIN", argv[2]}, {"ARLI_LISTEN", endpoint}});
  Client client(endpoint);
  if (!client.wait_health(8000)) { daemon.kill9(); return 3; }
  client.token = arli::secrets::load(root / "ui_token");
  if (!client.wait_worker("idle", 8000) || !client.set_model(argv[3], argv[4], wd.string(), 180, 5)) {
    daemon.term(); if (daemon.wait(10000) < 0) daemon.kill9(); return 3;
  }
  const auto dialog = client.create_dialog("live reconnect");
  std::string pins;
  if (pins_mode) {
    for (int i = 0; i < 30; ++i) pins += "Сохраняй точное содержимое создаваемых файлов. ";
    pins += "Каждый создаваемый .txt файл должен содержать ровно пять байтов ready без перевода строки. Используй файловые инструменты, shell не нужен.";
    grpc::ClientContext setup; client.auth(setup);
    setup.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    arli::v1::SetDialogInstructionsRequest request;
    request.set_dialog_id(dialog); request.set_instructions(pins);
    arli::v1::Dialog reply;
    if (!client.stub->SetDialogInstructions(&setup, request, &reply).ok() || reply.instructions() != pins) return 4;
  }
  arli::v1::SendRequest req; req.set_dialog_id(dialog); req.set_request_id("live-resume-once");
  req.set_text("Создай result.txt с ровно пятью байтами ready, без перевода строки. Затем прочитай его для проверки. Используй файловые инструменты, shell не нужен. Других файлов не создавай.");
  if (pins_mode) req.set_text("Создай result.txt по закреплённым правилам диалога, затем прочитай его для проверки.");
  if (interaction) req.set_text("Сначала обязательно спроси через ask_user, какое слово записать в файл. Дождись ответа. Затем создай result.txt с полученным словом без перевода строки и прочитай его для проверки. Используй файловые инструменты; shell не нужен.");
  if (rules_mode) req.set_text("Создай nested/result.txt согласно правилам проекта, затем прочитай его для проверки. Shell не нужен.");
  std::int64_t answer_delivery = 0;
  int questions = 0;
  std::int64_t task = 0, after = 0;
  {
    grpc::ClientContext ctx; client.auth(ctx); ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(60));
    auto reader = client.stub->Send(&ctx, req); arli::v1::TurnEvent event;
    if (reader->Read(&event)) { task = event.task_id(); after = event.event_id(); }
    ctx.TryCancel(); reader->Finish();
  }
  req.set_after_event_id(after);
  bool provider_usage = false; int reported_prompt = 0, reserved_output = 0;
  bool ordered = true, done = false, read_verified = false; int count = 0;
  grpc::ClientContext ctx; client.auth(ctx); ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(180));
  auto reader = client.stub->Send(&ctx, req); arli::v1::TurnEvent event;
  std::int64_t last = after;
  while (reader->Read(&event)) {
    ordered &= event.event_id() > last && event.task_id() == task;
    last = event.event_id(); ++count;
    if (event.has_context_use() && event.context_use().provider_reported()) {
      provider_usage = true; reported_prompt = event.context_use().used_tokens();
      reserved_output = event.context_use().reserved_output_tokens();
    }
    if (event.has_observation() && event.observation().tool() == "read_file" && event.observation().ok())
      read_verified = true;
    if (event.has_done()) done = event.done().outcome() == "done";
    if (event.has_confirm_needed() || event.has_ask_user()) {
      grpc::ClientContext control; client.auth(control);
      control.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
      if (event.has_confirm_needed()) {
        arli::v1::ConfirmDangerRequest deny; deny.set_dialog_id(dialog);
        deny.set_confirm_token(event.confirm_needed().confirm_token()); deny.set_accept(false);
        arli::v1::ConfirmDangerReply reply; client.stub->ConfirmDanger(&control, deny, &reply);
      } else if (interaction && questions++ == 0) {
        arli::v1::AnswerAskUserRequest answer;
        answer.set_dialog_id(dialog); answer.set_question_id(event.ask_user().question_id()); answer.set_answer("ready");
        arli::v1::AnswerAskUserReply reply;
        if (client.stub->AnswerAskUser(&control, answer, &reply).ok() && reply.ok())
          answer_delivery = reply.command_id();
      } else {
        arli::v1::CancelRequest cancel; cancel.set_dialog_id(dialog);
        arli::v1::CancelReply reply; client.stub->Cancel(&control, cancel, &reply);
      }
    }
  }
  const auto status = reader->Finish();
  const auto terminal = client.last_task(dialog);
  std::ifstream input(wd / (rules_mode ? "nested/result.txt" : "result.txt"), std::ios::binary);
  const std::string content{std::istreambuf_iterator<char>(input), {}};
  bool next_turn_ok = !pins_mode;
  if (pins_mode && done && status.ok()) {
    daemon.term(); if (daemon.wait(10000) < 0) daemon.kill9();
    daemon.start(argv[1], {endpoint}, {{"ARLI_STATE_DIR", tmp}, {"ARLI_AGENT_BIN", argv[2]}, {"ARLI_LISTEN", endpoint}});
    if (!client.wait_health(10000) || !client.wait_worker("idle", 10000)) return 5;
    grpc::ClientContext opened; client.auth(opened);
    opened.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    arli::v1::OpenDialogRequest open_req; open_req.set_dialog_id(dialog);
    arli::v1::OpenDialogReply open_reply;
    const bool persisted = client.stub->OpenDialog(&opened, open_req, &open_reply).ok() && open_reply.dialog().instructions() == pins;
    arli::v1::SendRequest next; next.set_dialog_id(dialog); next.set_request_id("live-next-turn");
    next.set_text("Продолжаем: создай second.txt по закреплённым правилам диалога, затем прочитай его для проверки.");
    grpc::ClientContext resumed; client.auth(resumed);
    resumed.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(180));
    auto stream = client.stub->Send(&resumed, next);
    arli::v1::TurnEvent item; bool next_done = false, next_read = false;
    while (stream->Read(&item)) {
      if (item.has_done()) next_done = item.done().outcome() == "done";
      if (item.has_observation() && item.observation().tool() == "read_file" && item.observation().ok()) next_read = true;
      if (item.has_confirm_needed() || item.has_ask_user()) { resumed.TryCancel(); break; }
    }
    const auto end = stream->Finish();
    std::ifstream second(wd / "second.txt", std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(second), {}};
    next_turn_ok = persisted && end.ok() && next_done && next_read && bytes == "ready";
  }
  int files = 0; for (const auto& entry : fs::recursive_directory_iterator(wd))
    files += entry.is_regular_file() && (!rules_mode || entry.path().filename() != "AGENTS.md");
  arli::Database journal((root / "arli.db").string());
  // Isolated empty database: this is its first and only Claim delivery.
  const auto receipt_status = journal.command_receipt_status(1);
  const auto answer_status = answer_delivery ? journal.command_receipt_status(answer_delivery) : "";
  const bool passed = next_turn_ok && provider_usage && (!interaction || (questions == 1 && answer_status == "accepted")) && receipt_status == "accepted" && after > 0 && task > 0 && ordered && done && status.ok() &&
      read_verified && terminal.id() == task && terminal.status() == "done" && content == "ready" && files == (pins_mode ? 2 : 1);
  nlohmann::json report = {{"passed", passed}, {"pinned_instructions", pins_mode}, {"next_turn_after_restart", next_turn_ok}, {"workspace_rules", rules_mode}, {"provider_usage", provider_usage}, {"reported_prompt_tokens", reported_prompt}, {"reserved_output_tokens", reserved_output}, {"interaction_receipt", answer_status}, {"questions", questions}, {"claim_receipt", receipt_status}, {"prompt", req.text()}, {"read_verified", read_verified}, {"model", argv[4]}, {"model_endpoint", argv[3]},
      {"task_id", task}, {"first_event_id", after}, {"last_event_id", last}, {"resumed_events", count},
      {"ordered_without_duplicates", ordered}, {"terminal_status", terminal.status()},
      {"grpc_ok", status.ok()}, {"file_content", content}, {"file_count", files}, {"workspace", wd.string()}};
  std::ofstream(argv[5]) << report.dump(2) << '\n';
  daemon.term(); if (daemon.wait(10000) < 0) daemon.kill9();
  return passed ? 0 : 1;
}
