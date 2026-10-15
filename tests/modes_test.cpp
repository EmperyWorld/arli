#include "modes.hpp"
#include "turn_policy.hpp"

#include <iostream>
#include <string>

static int fail(const std::string& msg) {
  std::cerr << "FAIL " << msg << "\n";
  return 1;
}

int main() {
  if (!arli::modes::turn_is_list_only("Какие файлы в проекте?")) {
    return fail("inventory question");
  }
  if (!arli::modes::turn_is_list_only("Список файлов в src")) {
    return fail("file list");
  }
  if (!arli::modes::turn_is_list_only("Что осталось сделать?")) {
    return fail("progress inventory");
  }
  if (arli::modes::turn_is_list_only("Посмотри файл main.cpp")) {
    return fail("single file read must not be list-only");
  }
  if (!arli::modes::turn_is_list_only("Посмотри, какие файлы в src")) {
    return fail("look + inventory still list-only");
  }
  if (arli::modes::turn_is_list_only("Продолжай улучшать index.html")) {
    return fail("file work must not be list-only");
  }
  if (arli::modes::turn_is_list_only("привет")) {
    return fail("greeting not list-only");
  }

  if (!arli::modes::tool_allowed_in_list_only("list_dir")) {
    return fail("list_dir allowed");
  }
  if (arli::modes::tool_allowed_in_list_only("read_file")) {
    return fail("read_file blocked");
  }
  if (arli::modes::tool_allowed_in_list_only("edit_file")) {
    return fail("edit blocked");
  }
  if (arli::modes::tool_allowed_in_list_only("rg_search")) {
    return fail("rg blocked");
  }

  std::cout << "OK modes-list-only\n";
  return 0;
}
