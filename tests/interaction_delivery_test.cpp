#include "hub.hpp"
#include "ask.hpp"
#include "confirm.hpp"
#include "interaction_receipt.hpp"
#include <filesystem>
#include <future>
#include <iostream>
#include <unistd.h>
int main() {
  char tmp[] = "/tmp/arli-interaction-delivery-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  int failures = 0;
  auto check = [&](bool ok, const char* text) { if (!ok) { std::cerr << text << '\n'; ++failures; } };
  auto db = std::make_shared<arli::Database>(std::string(tmp) + "/test.sqlite"); db->init();
  auto& hub = arli::OrchestratorHub::instance(); hub.init(db);
  const auto generation = hub.try_attach_session();
  arli::v1::WorkerToOrch frame;
  frame.mutable_hello()->set_worker_id("interactions"); frame.mutable_hello()->set_pid(1); frame.mutable_hello()->set_workspace_identity_supported(true);
  frame.mutable_hello()->set_interaction_ack_supported(true);
  check(hub.on_worker_message(generation, frame), "interaction capability independent of Claim capability");
  const auto dialog = db->create_dialog("delivery");
  const auto task = db->enqueue_task(dialog.id, 1, "ask twice"); hub.dispatch_next();
  hub.pop_outbound(generation, 0);
  frame.Clear(); frame.set_task_id(task.id); frame.set_dialog_id(dialog.id); frame.set_turn_id(1);
  frame.mutable_ask_user()->set_question_id("q1"); frame.mutable_ask_user()->set_question("test");
  hub.on_worker_message(generation, frame);
  arli::AskHub::instance().begin(dialog.id, "test", "q1");
  std::int64_t id = 0;
  check(hub.deliver_ask_answer(dialog.id, "answer", "", &id) && id > 0, "legacy UI ID canonicalized");
  auto command = hub.pop_outbound(generation, 0);
  if (!command) return 2;
  check(command->ask_user_answer().question_id() == "q1", "worker always receives actual question ID");
  arli::worker::InteractionReceipt cache;
  auto receive = [&](const arli::v1::OrchToWorker& msg) {
    const auto payload = msg.SerializeAsString();
    if (auto old = cache.lookup(msg.command_id(), payload); old.has_value()) return *old;
    const auto& answer = msg.ask_user_answer();
    const bool accepted = arli::AskHub::instance().answer(answer.dialog_id(), answer.answer(), answer.question_id());
    cache.record(msg.command_id(), payload, accepted); return accepted;
  };
  check(receive(*command), "answer applied");
  check(arli::AskHub::instance().wait_answer(dialog.id, nullptr) == "answer", "answer consumed once");
  arli::AskHub::instance().begin(dialog.id, "new question", "q2");
  hub.tick(arli::OrchestratorHub::Clock::now() + std::chrono::seconds(4));
  auto retry = hub.pop_outbound(generation, 0);
  check(retry && retry->SerializeAsString() == command->SerializeAsString(), "retry identical");
  if (retry) check(receive(*retry), "cached success after answer consumption");
  check(arli::AskHub::instance().answer(dialog.id, "second", "q2"), "old retry cannot answer new question");
  check(arli::AskHub::instance().wait_answer(dialog.id, nullptr) == "second", "new question unaffected");
  arli::v1::WorkerToOrch ack;
  ack.mutable_command_ack()->set_command_id(id); ack.mutable_command_ack()->set_accepted(true);
  check(hub.on_worker_message(generation, ack), "interaction acknowledgement");
  check(db->command_receipt_status(id) == "accepted", "durable acceptance");
  check(hub.on_worker_message(generation, ack), "duplicate ack accepted with interaction-only capability");
  auto& confirm = arli::ConfirmHub::instance();
  const auto token = confirm.begin(dialog.id, "test command");
  auto wait = std::async(std::launch::async, [&] { return confirm.wait_decision(dialog.id, token, nullptr); });
  check(confirm.decide(dialog.id, token, true), "confirmation applied");
  check(!confirm.decide(dialog.id, token, false), "later opposite decision rejected");
  check(wait.get(), "accepted confirmation remains accepted");
  frame.clear_ask_user(); frame.mutable_confirm_needed()->set_confirm_token("permission");
  hub.on_worker_message(generation, frame);
  check(hub.deliver_confirm(dialog.id, "permission", false, &id), "denial delivered as command");
  hub.pop_outbound(generation, 0);
  for (int seconds : {4, 8, 12}) {
    hub.tick(arli::OrchestratorHub::Clock::now() + std::chrono::seconds(seconds));
    hub.pop_outbound(generation, 0);
  }
  check(db->get_task(task.id)->error == "interaction_ack_timeout", "lost receipt fails explicitly");
  check(db->command_receipt_status(id) == "unconfirmed", "uncertain decision not called accepted");
  hub.on_session_end(generation);
  hub.take_kill_request();
  const auto second_generation = hub.try_attach_session();
  frame.Clear(); frame.mutable_hello()->set_worker_id("cancel-worker"); frame.mutable_hello()->set_pid(1); frame.mutable_hello()->set_workspace_identity_supported(true);
  frame.mutable_hello()->set_interaction_ack_supported(true);
  hub.on_worker_message(second_generation, frame);
  const auto cancelled = db->enqueue_task(dialog.id, 2, "cancel"); hub.dispatch_next();
  hub.pop_outbound(second_generation, 0);
  frame.Clear(); frame.set_task_id(cancelled.id); frame.set_dialog_id(dialog.id); frame.set_turn_id(2);
  frame.mutable_ask_user()->set_question_id("cancel-question");
  hub.on_worker_message(second_generation, frame);
  check(hub.deliver_ask_answer(dialog.id, "test", "cancel-question", &id), "answer queued before cancel");
  check(hub.cancel_dialog(dialog.id), "cancel queued");
  hub.tick(arli::OrchestratorHub::Clock::now() + std::chrono::seconds(12));
  check(db->get_task(cancelled.id)->status == "cancelled", "receipt timeout cannot overwrite cancellation");
  check(db->command_receipt_status(id) == "unconfirmed", "cancel clears pending receipt");
  hub.on_session_end(second_generation);
  std::filesystem::remove_all(tmp);
  return failures ? 1 : 0;
}
