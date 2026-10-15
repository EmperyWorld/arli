// Opt-in real-model acceptance. Isolated temporary files, no production daemon/settings.
#include "turn.hpp"
#include "llm_models.hpp"
#include "confirm.hpp"
#include "ask.hpp"
#include "turn_policy.hpp"
#include "tools/mutation_journal.hpp"
#include "tools/mutation_revert.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string read_text(const fs::path& path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}
void seed(const fs::path& path, const std::string& content) {
  std::ofstream(path) << content;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <base_url> <model> [report.json] [case] [revert] [profile]\n", argv[0]);
    return 2;
  }
  char tmp[] = "/tmp/arli-live-eval-XXXXXX";
  const char* created = mkdtemp(tmp);
  if (!created) return 2;
  const fs::path root(created);
  setenv("ARLI_STATE_DIR", created, 1);
  arli::Database db((root / "eval.sqlite").string());
  db.init();
  arli::TurnConfig cfg;
  cfg.llm_base_url = argv[1];
  cfg.llm_model = argv[2];
  cfg.llm_profile = argc > 6 ? argv[6] : (std::getenv("ARLI_LIVE_PROFILE") ? std::getenv("ARLI_LIVE_PROFILE") : "gemma4");
  cfg.context_tokens = 16384;
  if (arli::normalize_llm_profile(cfg.llm_profile) == "qwen3") {
    cfg.llm_temperature = 1.0; cfg.context_tokens = 49152;
  }
  cfg.max_steps = 64;
  json report = {{"model", cfg.llm_model}, {"profile", cfg.llm_profile}, {"workspace", root.string()}, {"cases", json::array()}};
  int failures = 0;
  for (const std::string name : {"bugfix", "multi_file", "recovery", "long_turn", "file_integrity"}) {
    if (argc > 4 && name != argv[4]) continue;
    const fs::path wd = root / "workspace" / name;
    fs::create_directories(wd);
    cfg.workdir = wd.string();
    std::string prompt, integrity_prefix;
    if (name == "file_integrity") {
      for (int i = 1; i <= 1500; ++i) integrity_prefix += "KEEP " + std::string(100, 'x') + "\xc2\xa0\r\n";
    }
    if (name == "bugfix" || name == "recovery") {
      seed(wd / "calc.py", "def add(a, b):\n    return a - b\n\n# KEEP PUBLIC_API\n");
      prompt = "Исправь calc.py: add должна складывать два числа. Сохрани имя, аргументы и комментарий KEEP PUBLIC_API. "
               "Прочитай файл, внеси минимальную правку, перечитай результат. Не создавай новых файлов. Shell не нужен.";
      if (name == "recovery") prompt = "Сначала вызови read_file для missing.py. Если файл отсутствует, не создавай его и продолжай: " + prompt;
    } else if (name == "multi_file") {
      seed(wd / "config.py", "LIMIT = 3\n# KEEP CONFIG\n");
      seed(wd / "consumer.py", "from config import LIMIT\n\ndef allowed(n):\n    return n < LIMIT\n# KEEP CONSUMER\n");
      prompt = "Исправь два файла: в config.py LIMIT должна быть 5, а в consumer.py allowed должна допускать n == LIMIT. "
               "Сначала изучи оба файла, используй file_pass_set для очереди, внеси правки и прочитай результаты. "
               "Не меняй прочие строки и не создавай новых файлов. Shell не нужен.";
    } else if (name == "file_integrity") {
      seed(wd / "large.txt", integrity_prefix + "value=old");
      seed(wd / "empty.txt", "");
      prompt = "В large.txt замени только строку 1501 value=old на value=new. Сначала прочитай этот диапазон. "
               "Все прочие байты сохрани. Затем прочитай существующий пустой empty.txt и запиши в него ровно 5 байтов ready без перевода строки через append_file. "
               "Проверь результат чтением обоих файлов. Новых файлов не создавай. Shell не нужен.";
    } else {
      std::string board;
      for (int i = 1; i <= 16; ++i) board += std::to_string(i) + " KEEP\n";
      seed(wd / "board.txt", board);
      // Read results add enough observations to trigger compaction.
      prompt = "Измени board.txt: для n от 1 до 16 выполни отдельный edit_file только строки n: '<n> KEEP' на '<n> DONE'. "
               "После КАЖДОЙ правки вызови read_file для всего board.txt и проверь свою строку. "
               "Не объединяй правки, не пропускай строки, не создавай новые файлы. Заверши только когда все 16 строк DONE. Shell не нужен.";
      cfg.context_tokens = 16384;
    }
    int tools = 0, writes = 0, tool_errors = 0, compactions = 0, errors = 0;
    bool done = false;
    std::string verification_state, verification_plan;
    std::atomic<bool> cancel{false};
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    std::thread deadline([&] {
      std::unique_lock lock(mutex);
      if (!cv.wait_for(lock, std::chrono::minutes(8), [&] { return finished; })) cancel.store(true);
    });
    const auto started = std::chrono::steady_clock::now();
    const auto dialog = db.create_dialog(name);
    std::fprintf(stderr, "LIVE_CASE %s\n", name.c_str());
    arli::run_turn(db, cfg, dialog.id, prompt, 1, [&](const auto& ev) {
      if (ev.has_observation()) {
        ++tools;
        tool_errors += !ev.observation().ok();
        writes += ev.observation().ok() && arli::tool_writes_workdir(ev.observation().tool());
        std::fprintf(stderr, "  tool=%s ok=%d steps=%d\n", ev.observation().tool().c_str(), ev.observation().ok(), tools);
      }
      if (ev.has_context_use()) compactions += ev.context_use().compressed();
      if (ev.has_error()) { ++errors; std::fprintf(stderr, "  error: %s\n", ev.error().message().c_str()); }
      if (ev.has_done()) {
        done = ev.done().outcome() == "done";
        verification_state = ev.done().verification_state(); verification_plan = ev.done().verification_plan_json();
      }
      // Unattended eval never grants permissions or answers user questions.
      if (ev.has_confirm_needed()) arli::ConfirmHub::instance().decide(dialog.id, ev.confirm_needed().confirm_token(), false);
      if (ev.has_ask_user()) cancel.store(true);
      return true;
    }, {&cancel});
    { std::lock_guard lock(mutex); finished = true; }
    cv.notify_all();
    deadline.join();
    bool correct = false;
    // Done alone is not proof of success; record the worker terminal outcome.
    if (name == "bugfix" || name == "recovery")
      correct = read_text(wd / "calc.py") == "def add(a, b):\n    return a + b\n\n# KEEP PUBLIC_API\n";
    else if (name == "multi_file")
      correct = read_text(wd / "config.py") == "LIMIT = 5\n# KEEP CONFIG\n" &&
                read_text(wd / "consumer.py") == "from config import LIMIT\n\ndef allowed(n):\n    return n <= LIMIT\n# KEEP CONSUMER\n";
    else if (name == "file_integrity")
      correct = read_text(wd / "large.txt") == integrity_prefix + "value=new" && read_text(wd / "empty.txt") == "ready";
    else {
      std::string expected;
      for (int i = 1; i <= 16; ++i) expected += std::to_string(i) + " DONE\n";
      correct = read_text(wd / "board.txt") == expected && writes == 16 && compactions > 0;
    }
    int file_count = 0;
    for (const auto& entry : fs::directory_iterator(wd)) if (entry.is_regular_file()) ++file_count;
    const bool no_extra_files = file_count == ((name == "multi_file" || name == "file_integrity") ? 2 : 1);
    bool pass = correct && no_extra_files && done && !cancel.load() && errors == 0 &&
                      (name != "recovery" || tool_errors >= 1);
    json reverted;
    if(argc>5&&std::string(argv[5])=="revert"&&name=="file_integrity") {
      bool verified=false;
      try {
        const auto project=root/"changesets"/arli::tools_internal::mutation_hash(fs::canonical(wd).generic_string());
        std::vector<std::string> ids;for(const auto& entry:fs::directory_iterator(project))if(fs::exists(entry.path()/"manifest.json"))ids.push_back(entry.path().filename().string());
        if(ids.size()!=1)throw std::runtime_error("expected one model changeset");
        reverted=arli::tools_internal::revert_mutations(wd,ids[0],dialog.id,2);
        verified=reverted["complete"].get<bool>()&&read_text(wd/"large.txt")==integrity_prefix+"value=old"&&read_text(wd/"empty.txt").empty();
      }catch(const std::exception& e){reverted={{"error",e.what()}};}
      report["revert_verified"]=verified;report["revert"]=reverted;pass=pass&&verified;
    }
    failures += !pass;
    report["cases"].push_back({{"name", name}, {"verification_state", verification_state}, {"verification_plan", verification_plan}, {"passed", pass}, {"disk_correct", correct}, {"no_extra_files", no_extra_files},
      {"tools", tools}, {"writes", writes}, {"tool_errors", tool_errors}, {"compactions", compactions},
      {"errors", errors}, {"seconds", std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count()}});
    report["failed"] = failures;
    const fs::path output = argc > 3 ? fs::path(argv[3]) : root / "report.json";
    seed(output, report.dump(2) + "\n");
    std::fprintf(stderr, "LIVE_RESULT %s pass=%d report=%s\n", name.c_str(), pass, output.c_str());
  }
  return failures ? 1 : 0;
}
