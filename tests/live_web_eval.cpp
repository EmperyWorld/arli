// Opt-in Gemma acceptance: real multi-file project, tool writes and runtime validation.
#include "turn.hpp"
#include "tools.hpp"
#include "turn_policy.hpp"
#include "confirm.hpp"
#include "ask.hpp"
#include "live_project_check.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace fs = std::filesystem;
using json = nlohmann::json;
std::string read(const fs::path& p) { std::ifstream f(p); std::ostringstream s; s << f.rdbuf(); return s.str(); }
void write(const fs::path& p, const std::string& s) { std::ofstream(p) << s; }
int main(int argc, char** argv) {
  if (argc < 4) return 2;
  char tmp[] = "/tmp/arli-web-eval-XXXXXX";
  if (!mkdtemp(tmp)) return 2;
  const fs::path root(tmp);
  setenv("ARLI_STATE_DIR", tmp, 1);
  arli::Database db((root / "test.sqlite").string()); db.init();
  json report = {{"workspace",root.string()},{"cases",json::array()}};
  int failures = 0;
  for (int trial = 0; trial < 3; ++trial) {
    const auto wd = root / "workspace" / std::to_string(trial); fs::create_directories(wd);
    const char* node = std::getenv("ARLI_EVAL_NODE");
    if (!node || !*node) return 2;
    fs::copy_file(node, wd / "node");
    std::string script = R"JS(// Public APIs and features must remain available.
class VirtualFS {
  init() { this.files = new Map(); }
  write(path, text) { this.files.set(path, text); }
  read(path) { return this.files.get(path); }
}
class UIController {
  constructor(kernel) { this.kernel = kernel; }
  init() { this.kernel.fs.write('welcome.txt', 'Hello'); }
}
class AppManager {
  constructor(kernel) { this.kernel = kernel; }
  registerAll() { this.kernel.catalog = { calculator: { add: (a, b) => a - b } }; }
}
const Kernel = {
  fs: null,
  ui: null,
  apps: null,
  catalog: {},
  init() {
    this.fs = new VirtualFS();
    this.ui = new UIController(this);
    this.apps = new AppManager();
    this.ui.init();
    this.fs.init();
    this.apps.registerAll();
  }
};
if (typeof module !== "undefined") module.exports = { Kernel, VirtualFS, UIController, AppManager };
)JS";
    std::string html = "<!DOCTYPE html>\n<html lang=\"ru\"><head><link rel=\"stylesheet\" href=\"style.css\"></head><body><main>Web project</main><script src=\"script.js\"></script></body></html>\n";
    std::string css = "body { color: #222; background: #fff; }\nmain { padding: 16px; }\n";
    std::string check = R"JS(const assert = require('node:assert/strict');
const { Kernel, VirtualFS, UIController, AppManager } = require('./script.js');
assert.equal(typeof VirtualFS, 'function');
assert.equal(typeof UIController, 'function');
assert.equal(typeof AppManager, 'function');
Kernel.init();
assert.equal(Kernel.fs.read('welcome.txt'), 'Hello');
assert.equal(Kernel.apps.kernel, Kernel);
assert.equal(Kernel.catalog.calculator.add(2, 3), 5);
assert.equal(Kernel.catalog.calculator.add(-2, 3), 1);
console.log('WEB_RUNTIME_OK');
)JS";
    if (argc > 5) {
      const fs::path source(argv[5]);
      script = read(source / "script.js"); html = read(source / "index.html"); css = read(source / "style.css");
      if (script.empty() || html.empty() || css.empty()) return 2;
      check = kProjectCheck;
    }
    write(wd / "script.js", script); write(wd / "index.html", html); write(wd / "style.css", css);
    write(wd / "runtime-check.cjs", check);
    arli::TurnConfig cfg; cfg.workdir = wd.string(); cfg.llm_base_url = argv[1]; cfg.llm_model = argv[2];
    cfg.context_tokens = argc > 4 ? std::stoi(argv[4]) : 24576; cfg.max_steps = 40;
    cfg.shell_auto = "./node";
    // Acceptance runs only in the generated temporary workspace.
    cfg.allow_unsandboxed_shell = true;
    int calls = 0, writes = 0, errors = 0; bool done = false;
    std::atomic<bool> cancel{false}; std::mutex mutex; std::condition_variable cv; bool finished = false;
    std::thread timer([&] { std::unique_lock lock(mutex); if(!cv.wait_for(lock,std::chrono::minutes(5),[&]{return finished;})) cancel = true; });
    const auto start = std::chrono::steady_clock::now();
    const auto dialog = db.create_dialog("web bugs");
    arli::run_turn(db, cfg, dialog.id,
      "Найди и исправь ошибки в файлах веб-проекта, затем проверь результат. Сохрани существующие API и возможности. "
      "Сначала воспроизведи ошибки, затем исправь и повтори проверку запуска: ./node runtime-check.cjs. "
      "node — готовый интерпретатор в этой папке; его и runtime-check.cjs не меняй. Не создавай новых файлов.",
      1, [&](const auto& event) {
        if(event.has_observation()) {
          const auto& o = event.observation(); ++calls;
          writes += o.ok() && arli::tool_writes_workdir(o.tool());
          std::fprintf(stderr,"WEB trial=%d tool=%s ok=%d %s\n",trial,o.tool().c_str(),o.ok(),o.output().substr(0,150).c_str());
        }
        errors += event.has_error(); done |= event.has_done();
        if(event.has_confirm_needed()) arli::ConfirmHub::instance().decide(dialog.id,event.confirm_needed().confirm_token(),false);
        if(event.has_ask_user()) cancel = true;
        return true;
      }, {&cancel});
    { std::lock_guard lock(mutex); finished = true; } cv.notify_one(); timer.join();
    const std::string cmd = "cd '" + wd.string() + "' && node runtime-check.cjs > runtime-result.log 2>&1";
    const bool runtime = std::system(cmd.c_str()) == 0;
    const bool preserved = read(wd / "index.html") == html && read(wd / "style.css") == css && read(wd / "runtime-check.cjs") == check;
    const bool pass = runtime && preserved && done && !cancel && errors == 0 && calls <= 20 && writes <= 6;
    failures += !pass;
    report["cases"].push_back({{"trial",trial},{"passed",pass},{"runtime_ok",runtime},{"preserved",preserved},{"calls",calls},{"writes",writes},{"errors",errors},
      {"seconds",std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count()}});
    report["failed"] = failures; write(argv[3],report.dump(2)+"\n");
    std::fprintf(stderr,"WEB_RESULT trial=%d pass=%d report=%s\n",trial,pass,argv[3]);
  }
  return failures ? 1 : 0;
}
