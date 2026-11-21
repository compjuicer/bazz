#pragma once
// Immediate drawing in view pixels on the current window's draw list.
#include <string>

#include "reaper_imgui_functions.h"

namespace gfx {

constexpr int kBlack = 0x000000FF, kWhite = 0xFFFFFFFF;

void attach_fonts(ImGui_Context*);
void begin(ImGui_Context*);  // origin = cursor screen pos
double ox();
double oy();

void fill(double x, double y, double w, double h, int col);
void frame(double x, double y, double w, double h, int col = kBlack);
void line(double x0, double y0, double x1, double y1, int col = kBlack, double thickness = 1);
void round_box(double x, double y, double w, double h, double r, int fill_col, int line_col = kBlack);
void tri(const double* x, const double* y, int fill_col, int line_col = kBlack);
void text_centered(double x, double y, double w, double h, const std::string&, int col = kBlack, bool bold = false);  // clipped
void text_in(double x, double y, double w, double h, const std::string&, bool right, int col = kBlack);  // regular, clipped
ImGui_Font* ui_font();
constexpr double kFontSize = 11;  // Tahoma 8pt at 96 dpi

}  // namespace gfx
