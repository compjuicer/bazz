#define REAPERAPI_IMPLEMENT
#include "reaper_core_api.h"
#include "core_abi.h"
#include "reaper_graph.h"
#include "ui.h"

#include <cstdio>
#include <string>

namespace {

ReaProject* current() { return EnumProjects(-1, nullptr, 0); }

const BazzAction kActions[] = {
    {"BAZZ_WINDOW", "bazz: Show machine view"},
};

void run_action(int index) {
  if (index == 0) ui_set_open(!ui_is_open());
}

#ifdef BAZZ_DEV
// tools/reaper drives the UI through ExtState bazz/cmd: "open" | "close" | "reset" | "add X Y <fx name>" | "in|out X Y CH STEREO" |
// "connect A B" | "insert W X Y <fx name>" | "delete M".
void debug_command() {
  const char* cmd = GetExtState("bazz", "cmd");
  if (!cmd || !*cmd) return;
  std::string c = cmd;
  DeleteExtState("bazz", "cmd", false);
  double x = 0, y = 0;
  int a = 0, b = 0, n = 0;
  if (c == "open" || c == "close") ui_set_open(c == "open");
  else if (c == "reset") ui_reset_window();
  else if (sscanf(c.c_str(), "add %lf %lf %n", &x, &y, &n) == 2 && n) {
    Scene s = scan(current());
    UndoBlock u(s.proj, "bazz: new machine");
    add_machine(s, c.substr(n), x, y);
  } else if (sscanf(c.c_str(), "in %lf %lf %d %d", &x, &y, &a, &b) == 4 || sscanf(c.c_str(), "out %lf %lf %d %d", &x, &y, &a, &b) == 4) {
    Scene s = scan(current());
    UndoBlock u(s.proj, "bazz: new io");
    c[0] == 'i' ? add_input(s, {a, b != 0}, x, y) : add_output(s, {a, b != 0}, x, y);
  } else if (sscanf(c.c_str(), "insert %d %lf %lf %n", &a, &x, &y, &n) == 3 && n) {
    Scene s = scan(current());
    UndoBlock u(s.proj, "bazz: insert machine");
    insert_machine(s, a, c.substr(n), x, y);
  } else if (sscanf(c.c_str(), "delete %d", &a) == 1) {
    Scene s = scan(current());
    UndoBlock u(s.proj, "bazz: delete machine");
    delete_machine(s, a);
  } else if (sscanf(c.c_str(), "connect %d %d", &a, &b) == 2) {
    Scene s = scan(current());
    UndoBlock u(s.proj, "bazz: connect");
    connect(s, a, b);
  }
}
#endif

void tick() {
#ifdef BAZZ_DEV
  debug_command();
#endif
  ui_frame(current());
}

std::string g_ret;

const char* snapshot() { return (g_ret = ui_is_open() ? "!window\n" : "").c_str(); }

void restore(const char* text) { ui_set_open(std::string(text).find("!window") != std::string::npos); }

const char* dump() {
  g_ret = ui_status();
  Scene s = scan(current());
  char buf[512];
  const char* kinds[] = {"master", "gen", "fx", "in", "out"};
  for (size_t i = 0; i < s.g.machines.size(); ++i) {
    auto& m = s.g.machines[i];
    snprintf(buf, sizeof buf, "machine %zu %s \"%s\" pos=%.3f,%.3f mute=%d\n", i, kinds[int(m.kind)], m.name.c_str(), m.x, m.y, m.mute);
    g_ret += buf;
    for (auto& p : m.ins) g_ret += "  in " + std::to_string(p.ch) + " \"" + p.name + "\"\n";
    for (auto& p : m.outs) g_ret += "  out " + std::to_string(p.ch) + " \"" + p.name + "\"\n";
  }
  for (auto& w : s.g.wires) {
    snprintf(buf, sizeof buf, "wire %d->%d vol=%.3f pan=%.3f src=%d dst=%d\n", w.from, w.to, w.vol, w.pan, w.src, w.dst);
    g_ret += buf;
  }
  return g_ret.c_str();
}

void shutdown() { plugin_register("-timer", (void*)tick); }

const BazzCore kCore = {
    int(sizeof(kActions) / sizeof(kActions[0])), kActions, run_action, snapshot, restore, dump, __DATE__ " " __TIME__, shutdown,
};

}  // namespace

extern "C" REAPER_PLUGIN_DLL_EXPORT const BazzCore* bazz_core_init(void* (*get_func)(const char*)) {
  if (REAPERAPI_LoadAPI(get_func)) return nullptr;
  ui_init(get_func);
  plugin_register("timer", (void*)tick);
  return &kCore;
}
