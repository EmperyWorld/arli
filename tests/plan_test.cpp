#include "file_pass.hpp"
#include "plan.hpp"

#include <iostream>

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  arli::PlanRow row;
  row.steps = {"a", "b", "c"};
  arli::PlanState plan = arli::PlanState::from_row(row);
  if (plan.current() != 0) {
    return fail("current");
  }

  arli::FilePassRow pass_row;
  pass_row.paths = {"x.txt"};
  arli::FilePassState pass = arli::FilePassState::from_row(pass_row);
  pass.on_read_ok("x.txt");
  if (!pass.hold_for_write()) {
    return fail("hold");
  }
  if (plan.advance(pass).ok) {
    return fail("advance blocked during file_pass hold");
  }
  if (!pass.review_without_changes({{"path", "x.txt"}, {"note", "correct"}}).ok)
    return fail("review queue file");
  if (!plan.advance(pass).ok) {
    return fail("advance after write");
  }
  if (plan.current() != 1) {
    return fail("current after advance");
  }

  nlohmann::json fail_j = {{"note", "oops"}};
  if (!plan.mark_failed(fail_j).ok || !plan.current_failed()) {
    return fail("plan_fail");
  }
  if (plan.advance(pass).ok) {
    return fail("advance while failed");
  }
  if (!plan.retry().ok || plan.current_failed()) {
    return fail("plan_retry");
  }

  nlohmann::json edit = {{"text", "beta"}};
  if (!plan.edit_step(edit).ok) {
    return fail("plan_edit");
  }

  std::cout << "OK plan-mode\n";
  return 0;
}
