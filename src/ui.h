#pragma once
#include "reaper_plugin.h"

void ui_init(void* (*get_func)(const char*));
void ui_set_open(bool);
void ui_reset_window();  // back to the default size and place, expanded
bool ui_is_open();
void ui_frame(ReaProject*);
const char* ui_status();
