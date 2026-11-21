#pragma once
// C ABI between the loader (reaper_bazz.dylib) and the hot-reloadable core (bazz_core.dylib).
// Returned strings are owned by the core and valid until the next call into it.

extern "C" {

struct BazzAction {
  const char* id;
  const char* desc;
};

struct BazzCore {
  int num_actions;
  const BazzAction* actions;
  void (*run_action)(int index);
  const char* (*snapshot)();
  void (*restore)(const char*);
  const char* (*dump)();
  const char* build_time;
  void (*shutdown)();
};

typedef const BazzCore* (*BazzCoreInit)(void* (*get_func)(const char*));
}
