#include "ui.h"

#include "reaper_core_api.h"
#define REAIMGUIAPI_IMPLEMENT
#include "reaper_imgui_functions.h"
#include "gfx.h"
#include "reaper_graph.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

#undef min
#undef max

namespace G = ImGui;
using namespace bazz;

namespace {

void* (*g_get_func)(const char*) = nullptr;
bool g_api = false, g_open = false, g_reset = false, g_docked = false;
int g_dock_req = 0;  // 1 dock, 2 undock
ImGui_Context* g_ctx = nullptr;
std::string g_error;

constexpr int kBg = 0xDAD6C9FF;
std::string fit(std::string s, double w) {  // bold text cut to width w, with "..." if cut
  if (gfx::text_w(s, true) <= w) return s;
  while (!s.empty() && gfx::text_w(s + "...", true) > w) s.pop_back();
  return s + "...";
}

// 5x7 pixel letters for the M/S/R buttons, emboldened by doubling each pixel to the right.
void btn_glyph(double x, double y, char c, unsigned col) {
  static const char* M[] = {"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
  static const char* S[] = {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
  static const char* R[] = {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
  const char** g = c == 'M' ? M : c == 'S' ? S : R;
  for (int r = 0; r < 7; ++r)
    for (int k = 0; k < 5; ++k)
      if (g[r][k] == '1') gfx::fill(x + k, y + r, 2, 1, col);
}

unsigned box_color(Kind k) {
  if (k == Kind::Generator) return 0xA9AEC7FF;
  if (k == Kind::Input) return 0xD2B9A0FF;  // warm beige
  if (k == Kind::Effect) return 0xC7ADA9FF;
  if (k == Kind::Track) return 0xB4C2AEFF;  // sage: an ordinary REAPER track
  return 0xC6BEAAFF;
}

unsigned mix(unsigned a, unsigned b, double t) {  // RGBA, t = share of b
  unsigned r = 0;
  for (int sh = 8; sh < 32; sh += 8)
    r |= unsigned(std::lround(((a >> sh) & 255) * (1 - t) + ((b >> sh) & 255) * t)) << sh;
  return r | 255;
}

// Classic Windows menus.
constexpr unsigned kMenuBg = 0xD4D0C8FF, kMenuHi = 0x0A246AFF, kMenuShadow = 0x808080FF;
std::map<std::string, bool> g_menu_open, g_menu_open_next;
const std::string kGutter = "    ";  // room on the left like a Windows menu's check column

// We draw the highlight ourselves so exactly one row is lit: the hovered row while the mouse is over this menu,
// otherwise the row whose submenu is open. ImGui's own header colors are transparent.
bool menu_row_hi(const std::string& label, int force = -1) {  // force: 1 lit, 0 dark, -1 follow the mouse
  double x = 0, y = 0, w = 0, h = 0, sx = 0, sy = 0, wx = 0, wy = 0, ww = 0, wh = 0;
  G::GetCursorScreenPos(g_ctx, &x, &y);
  G::GetContentRegionAvail(g_ctx, &w, &h);
  G::GetStyleVar(g_ctx, G::StyleVar_ItemSpacing, &sx, &sy);
  G::GetWindowPos(g_ctx, &wx, &wy);
  G::GetWindowSize(g_ctx, &ww, &wh);
  double y0 = y - std::floor(sy / 2), y1 = y + G::GetTextLineHeight(g_ctx) + std::ceil(sy / 2);
  bool in_menu = G::IsMouseHoveringRect(g_ctx, wx, wy, wx + ww, wy + wh, false);
  bool hi = force >= 0 ? force : in_menu ? G::IsMouseHoveringRect(g_ctx, x, y0, x + w, y1, false) : g_menu_open[label];
  if (hi) G::DrawList_AddRectFilled(G::GetWindowDrawList(g_ctx), x, y0, x + w, y1, kMenuHi);
  return hi;
}

bool menu_item(const char* label, bool* sel = nullptr, int force = -1) {
  bool hi = menu_row_hi(label, force);
  if (hi) G::PushStyleColor(g_ctx, G::Col_Text, gfx::kWhite);
  bool r = G::MenuItem(g_ctx, (kGutter + label + "  ").c_str(), nullptr, sel);
  if (hi) G::PopStyleColor(g_ctx);
  return r;
}

bool begin_menu(const char* label) {
  bool hi = menu_row_hi(label);
  if (hi) G::PushStyleColor(g_ctx, G::Col_Text, gfx::kWhite);
  bool open = G::BeginMenu(g_ctx, (kGutter + label + "  ").c_str());
  if (hi) G::PopStyleColor(g_ctx);
  g_menu_open_next[label] = open;
  return open;
}

int push_menu_style() {
  const std::pair<int, unsigned> cols[] = {
      {G::Col_PopupBg, kMenuBg}, {G::Col_Text, gfx::kBlack}, {G::Col_Border, kMenuShadow},
      {G::Col_HeaderHovered, 0}, {G::Col_Header, 0}, {G::Col_HeaderActive, 0}, {G::Col_Separator, kMenuShadow}};
  for (auto& [c, v] : cols) G::PushStyleColor(g_ctx, c, v);
  G::PushStyleVar(g_ctx, G::StyleVar_PopupRounding, 0);
  G::PushStyleVar(g_ctx, G::StyleVar_PopupBorderSize, 1);
  G::PushStyleVar(g_ctx, G::StyleVar_WindowPadding, 3, 3);
  G::PushStyleVar(g_ctx, G::StyleVar_ItemSpacing, 8, 4);
  return int(sizeof cols / sizeof cols[0]);
}


enum class Drag { None, Move, Connect, Volume };
Drag g_drag = Drag::None;
int g_drag_m = -1, g_drag_w = -1;
double g_off_x = 0, g_off_y = 0, g_start_vol = 1, g_start_y = 0, g_start_mx = 0, g_start_my = 0;
double g_mx = 0, g_my = 0;
int g_drag_src = 0, g_mods = 0;  // output channel of a connect drag
int g_temp_open = -1;  // panels opened while a cable hovers their machine
struct Target {                   // where a connect drag would land
  int to = -1, dst = 0;
  bool ok = false;
} g_target;
constexpr unsigned kTab = 0xF0B030FF, kRed = 0xE01818FF, kSolo = 0xF0D000FF, kSoloDim = 0xC8BC7CFF;
Shown g_sh;        // this frame's open panels
int g_splice = -1;  // wire a dragged loose effect would be dropped into
int g_menu_m = -1, g_menu_w = -1;
double g_menu_x = 0, g_menu_y = 0;
View g_view;
std::vector<std::string> g_fx;
char g_search[128] = "";
int g_search_sel = 0;

double box_x(const View& v, const Machine& m) { return std::round(v.sx(m.x) - kBoxW / 2); }
double box_y(const View& v, const Machine& m) { return std::round(v.sy(m.y) - kBoxH / 2); }


std::string port_label(const Port& p) { return p.name + " " + std::to_string(p.ch + 1) + "/" + std::to_string(p.ch + 2); }

Shown shown(const Graph& g) {
  Shown sh;
  for (int i = 0; i < int(g.machines.size()); ++i)
    if (i == g_temp_open || g.machines[i].panel) sh.open.push_back(i);
  return sh;
}

bool on_toggle(const Graph& g, const View& v, int m, double x, double y) {
  Pt t = toggle_pos(v, g.machines[m]);
  return has_ports(g.machines[m]) && x >= t.x && x < t.x + kToggle && y >= t.y && y < t.y + kToggle;
}

Pt drag_start(const Graph& g, const View& v) {
  Wire w;
  w.from = g_drag_m, w.src = g_drag_src;
  return wire_start(g, v, w, g_sh);
}

void thick_frame(double x, double y, double w, double h) {
  gfx::frame(x - 1, y - 1, w + 2, h + 2);
  gfx::frame(x + 1, y + 1, w - 2, h - 2);
}

void panel(const Machine& m, int i, const View& v) {
  for (bool out : {false, true}) {
    auto& ports = out ? m.outs : m.ins;
    if (ports.empty()) continue;
    Pt p = panel_pos(v, m, out);
    gfx::fill(p.x, p.y, kPanelW, panel_h(m, out), kMenuBg);
    gfx::frame(p.x, p.y, kPanelW, panel_h(m, out));
    for (int k = 0; k < int(ports.size()); ++k) {
      Pt c = port_pt(v, m, out, k);
      double ry = c.y - kRowH / 2;
      bool hi = g_drag == Drag::Connect &&
                (out ? g_drag_m == i && ports[k].ch == g_drag_src : g_target.ok && g_target.to == i && ports[k].ch == g_target.dst);
      if (hi) gfx::fill(p.x + 1, ry, kPanelW - 2, kRowH, kMenuHi);
      gfx::round_box(c.x - kConn / 2, c.y - kConn / 2, kConn, kConn, 2, kTab);
      double tx = c.x + kConn / 2 + 3;
      gfx::text_in(tx, ry, p.x + kPanelW - 2 - tx, kRowH, port_label(ports[k]), false, hi ? gfx::kWhite : gfx::kBlack);
    }
  }
}

void draw(const Scene& s, const View& v) {
  gfx::fill(0, 0, v.w, v.h, kBg);
  auto& g = s.g;
  // Other wires, panels, wires on panel ports, boxes: panel wires reach their connectors, all stay under the boxes.
  auto on_panel = [&](const Wire& w) {
    return (g_sh.has(w.from) && !g.machines[w.from].outs.empty()) || (g_sh.has(w.to) && !g.machines[w.to].ins.empty());
  };
  auto wires = [&](bool panel_wires) {
    for (int i = 0; i < int(g.wires.size()); ++i)
      if (on_panel(g.wires[i]) == panel_wires) {
        Seg sg = wire_seg(g, v, i, g_sh);
        gfx::line(sg.a.x, sg.a.y, sg.b.x, sg.b.y, gfx::kBlack, i == g_splice ? 3 : 1);
      }
    for (int i = 0; i < int(g.wires.size()); ++i)
      if (on_panel(g.wires[i]) == panel_wires) {
        Tri t = arrow(g, v, i, g_sh);
        gfx::tri(t.x, t.y, i == g_splice ? kMenuHi : gfx::kWhite);
      }
  };
  wires(false);
  for (int i : g_sh.open) panel(g.machines[i], i, v);
  wires(true);
  for (size_t i = 0; i < g.machines.size(); ++i) {
    auto& m = g.machines[i];
    double x = box_x(v, m), y = box_y(v, m);
    unsigned col = box_color(m.kind);
    gfx::fill(x, y, kBoxW, kBoxH, m.mute ? mix(col, gfx::kBlack, 0.35) : col);
    gfx::frame(x, y, kBoxW, kBoxH);
    std::string b = buttons(m.kind);
    for (char c : b) {  // M/S/R: red mute, yellow solo (dim: soloed through a chain), red arm
      bool on = c == 'M' ? m.mute : c == 'S' ? m.solo || m.soloed : m.armed;
      unsigned bg = !on ? kMenuBg : c == 'S' ? (m.solo ? kSolo : kSoloDim) : kRed;
      Pt p = button_pos(v, m, c);
      gfx::fill(p.x, p.y, kBtnW, kBtnH, bg);
      gfx::frame(p.x, p.y, kBtnW, kBtnH);
      btn_glyph(p.x + 3, p.y + 2, c, on && c != 'S' ? gfx::kWhite : gfx::kBlack);
    }
    gfx::text_centered(x + 1, y + 1, kBoxW - 2, kBoxH - 2, fit(m.name, kBoxW - 8), gfx::kBlack, true);
    if (g_drag == Drag::Connect && g_target.ok && g_target.to == int(i)) thick_frame(x, y, kBoxW, kBoxH);
    if (has_ports(m)) {  // panel toggle: + closed, - open
      Pt t = toggle_pos(v, m);
      gfx::fill(t.x, t.y, kToggle, kToggle, kMenuBg);
      gfx::frame(t.x, t.y, kToggle, kToggle);
      gfx::fill(t.x + 2, t.y + 4, 5, 1, gfx::kBlack);
      if (!g_sh.has(int(i))) gfx::fill(t.x + 4, t.y + 2, 1, 5, gfx::kBlack);
    }
  }
  if (g_drag == Drag::Connect) {
    Pt a = drag_start(g, v);
    gfx::line(a.x, a.y, g_mx, g_my);
  }
  if (g_drag == Drag::Volume) {
    Tri t = arrow(g, v, g_drag_w, g_sh);
    double x = t.x[0] + 16, y = (t.y[0] + t.y[1] + t.y[2]) / 3 - 50;
    double vol = g.wires[g_drag_w].vol;
    gfx::fill(x, y, 20, 100, 0xD4D0C8FF);
    gfx::frame(x, y, 20, 100);
    gfx::fill(x + 2, y + 2 + (1 - vol) * 88, 16, 8, gfx::kBlack);
    char buf[32];
    snprintf(buf, sizeof buf, "%d%%", int(std::lround(vol * 100)));
    gfx::text_centered(x + 22, y + 40, 40, 20, buf);
  }
}

using Pick = std::function<void(const std::string& fx)>;

void fx_menu(bool instruments, const Pick& pick) {
  std::map<std::string, std::vector<const std::string*>> by_type;
  for (auto& f : g_fx)
    if (fx_is_instrument(f) == instruments) by_type[fx_type(f)].push_back(&f);
  for (auto& [type, list] : by_type) {
    if (!begin_menu(type.empty() ? "Other" : type.c_str())) continue;
    for (auto* f : list)
      if (menu_item(fx_short_name(*f).c_str())) pick(*f);
    G::EndMenu(g_ctx);
  }
}

void io_menu(Scene& s, bool input) {
  int n = input ? num_inputs() : num_outputs();
  for (int pass = 0; pass < 2; ++pass)
    for (int ch = 0; ch + pass < n; ch += 2 - pass) {
      Io io{ch, pass == 0};
      if (!menu_item(io_name(input, io).c_str())) continue;
      UndoBlock u(s.proj, input ? "bazz: new input" : "bazz: new output");
      double x = g_view.nx(g_menu_x), y = g_view.ny(g_menu_y);
      input ? add_input(s, io, x, y) : add_output(s, io, x, y);
    }
}

// Search field on top of the canvas menu: typing replaces the submenus with matching plugins.
// Returns true while there is a query.
// Plugins and ordinary project tracks matching the query; the pick functions add the chosen one.
bool search(Scene& s, bool effects_only, const Pick& pick, const Pick& pick_track) {
  if (G::IsWindowAppearing(g_ctx)) G::SetKeyboardFocusHere(g_ctx);
  G::PushStyleColor(g_ctx, G::Col_FrameBg, gfx::kWhite);
  G::PushStyleVar(g_ctx, G::StyleVar_FrameBorderSize, 1);
  G::SetNextItemWidth(g_ctx, 240);
  G::InputTextWithHint(g_ctx, "##search", "Search plugins and tracks", g_search, sizeof g_search);
  bool enter = G::IsKeyPressed(g_ctx, G::Key_Enter) || G::IsKeyPressed(g_ctx, G::Key_KeypadEnter);
  G::PopStyleVar(g_ctx);
  G::PopStyleColor(g_ctx);
  if (!g_search[0]) return false;
  std::vector<std::pair<std::string, std::function<void()>>> found;
  for (auto& f : g_fx)
    if (found.size() < 40 && fx_matches(f, g_search) && !(effects_only && fx_is_instrument(f)))
      found.push_back({fx_short_name(f) + " (" + fx_type(f) + ")", [&] { pick(f); }});
  for (auto& t : free_tracks(s.proj)) {
    std::string label = "Track " + std::to_string(t.number) + ": " + t.name;
    if (found.size() < 40 && fx_matches(label, g_search)) found.push_back({label, [&pick_track, id = t.id] { pick_track(id); }});
  }
  if (G::IsKeyPressed(g_ctx, G::Key_DownArrow)) ++g_search_sel;
  if (G::IsKeyPressed(g_ctx, G::Key_UpArrow)) --g_search_sel;
  g_search_sel = std::clamp(g_search_sel, 0, std::max(0, int(found.size()) - 1));
  if (found.empty()) menu_item("No matches", nullptr, 0);
  for (int i = 0; i < int(found.size()); ++i) {
    G::PushID(g_ctx, std::to_string(i).c_str());
    bool chosen = menu_item(found[i].first.c_str(), nullptr, i == g_search_sel ? 1 : 0) || (enter && i == g_search_sel);
    if (G::IsItemHovered(g_ctx)) g_search_sel = i;
    G::PopID(g_ctx);
    if (chosen) {
      found[i].second();
      G::CloseCurrentPopup(g_ctx);
      break;
    }
  }
  return true;
}

void track_menu(Scene& s, const Pick& pick_track) {
  auto list = free_tracks(s.proj);
  if (list.empty()) menu_item("No other tracks", nullptr, 0);
  for (auto& t : list)
    if (menu_item((std::to_string(t.number) + ": " + t.name).c_str())) pick_track(t.id);
}

void context_items(Scene& s) {
  if (g_fx.empty()) g_fx = installed_fx();
  double x = g_view.nx(g_menu_x), y = g_view.ny(g_menu_y);
  if (g_menu_w >= 0 && g_menu_w < int(s.g.wires.size())) {
    Pick insert = [&](const std::string& fx) {
      UndoBlock u(s.proj, "bazz: insert machine");
      insert_machine(s, g_menu_w, fx, x, y);
    };
    Pick insert_tr = [&](const std::string& id) {
      UndoBlock u(s.proj, "bazz: insert track");
      insert_track(s, g_menu_w, id, x, y);
    };
    if (search(s, true, insert, insert_tr)) return;
    if (begin_menu("Insert Effect")) {
      fx_menu(false, insert);
      G::EndMenu(g_ctx);
    }
    if (begin_menu("Insert Track")) {
      track_menu(s, insert_tr);
      G::EndMenu(g_ctx);
    }
    if (menu_item("Disconnect")) {
      UndoBlock u(s.proj, "bazz: disconnect");
      disconnect(s, g_menu_w);
    }
  } else if (g_menu_m >= 0 && g_menu_m < int(s.g.machines.size())) {
    bool mute = s.g.machines[g_menu_m].mute, track = s.tracks[g_menu_m];
    if (g_menu_m > 0 && track && menu_item("Parameters...")) show_fx(s, g_menu_m);
    if (track && menu_item("Mute", &mute)) {
      UndoBlock u(s.proj, "bazz: mute");
      set_mute(s, g_menu_m, mute);
    }
    if (g_menu_m > 0 && menu_item(s.g.machines[g_menu_m].kind == Kind::Track ? "Remove from graph" : "Delete")) {
      UndoBlock u(s.proj, "bazz: delete machine");
      delete_machine(s, g_menu_m);
    }
  } else {
    Pick add = [&](const std::string& fx) {
      UndoBlock u(s.proj, "bazz: new machine");
      add_machine(s, fx, x, y);
    };
    Pick add_tr = [&](const std::string& id) {
      UndoBlock u(s.proj, "bazz: add track");
      add_track_node(s, id, x, y);
    };
    if (search(s, false, add, add_tr)) return;
    if (begin_menu("New Generator")) {
      fx_menu(true, add);
      G::EndMenu(g_ctx);
    }
    if (begin_menu("New Effect")) {
      fx_menu(false, add);
      G::EndMenu(g_ctx);
    }
    if (begin_menu("New Input")) {
      io_menu(s, true);
      G::EndMenu(g_ctx);
    }
    if (begin_menu("New Output")) {
      io_menu(s, false);
      G::EndMenu(g_ctx);
    }
    if (begin_menu("Add Track")) {
      track_menu(s, add_tr);
      G::EndMenu(g_ctx);
    }
    G::Separator(g_ctx);
    if (menu_item(g_docked ? "Undock window" : "Dock window")) ui_toggle_dock();
  }
}

void context_menu(Scene& s) {
  g_menu_open = std::move(g_menu_open_next);
  g_menu_open_next.clear();
  int cols = push_menu_style();
  bool open = G::BeginPopup(g_ctx, "ctx");
  if (open && G::IsKeyPressed(g_ctx, G::Key_Escape)) G::CloseCurrentPopup(g_ctx);
  if (open) context_items(s);
  if (open) G::EndPopup(g_ctx);
  G::PopStyleVar(g_ctx, 4);
  G::PopStyleColor(g_ctx, cols);
}
// Classic tabs along the top, one per graph, then "+" for a new one. Right-click a tab to delete its graph.
constexpr double kTabsH = 20;

void tabs(Scene& s) {
  gfx::begin(g_ctx);
  double w = 0, h = 0;
  G::GetContentRegionAvail(g_ctx, &w, &h);
  gfx::fill(0, 0, w, kTabsH, kMenuBg);
  gfx::fill(0, kTabsH - 1, w, 1, gfx::kWhite);
  std::string sel = selected_graph(s.proj);
  auto list = graphs(s.proj);
  double x = 2, ox = gfx::ox(), oy = gfx::oy();
  bool menu = false;
  for (size_t i = 0; i <= list.size(); ++i) {
    bool plus = i == list.size(), on = !plus && list[i].id == sel;
    std::string label = plus ? "+" : list[i].name;
    double tw = gfx::text_w(label, on) + 14, y = on ? 1 : 3;
    gfx::fill(x, y, tw, kTabsH - y, on ? kBg : kMenuBg);
    gfx::fill(x, y, tw - 1, 1, gfx::kWhite);
    gfx::fill(x, y, 1, kTabsH - y - (on ? 0 : 1), gfx::kWhite);
    gfx::fill(x + tw - 1, y + 1, 1, kTabsH - y - 2, kMenuShadow);
    if (on) gfx::fill(x + 1, kTabsH - 1, tw - 2, 1, kBg);
    gfx::text_centered(x, y, tw, kTabsH - y, label, gfx::kBlack, on);
    G::SetCursorScreenPos(g_ctx, ox + x, oy);
    G::PushID(g_ctx, ("tab" + std::to_string(i)).c_str());
    if (G::InvisibleButton(g_ctx, "t", tw, kTabsH, G::ButtonFlags_MouseButtonLeft | G::ButtonFlags_MouseButtonRight)) {
      if (plus) {
        UndoBlock u(s.proj, "bazz: new graph");
        new_graph(s);
      } else
        select_graph(s.proj, list[i].id), s = scan(s.proj);
    }
    if (!plus && G::IsItemClicked(g_ctx, G::MouseButton_Right)) {
      select_graph(s.proj, list[i].id), s = scan(s.proj);
      menu = true;
    }
    G::PopID(g_ctx);
    x += tw;
  }
  if (menu) G::OpenPopup(g_ctx, "tabmenu");
  int cols = push_menu_style();
  if (G::BeginPopup(g_ctx, "tabmenu")) {
    if (menu_item("Delete graph")) {
      UndoBlock u(s.proj, "bazz: delete graph");
      delete_graph(s);
      ensure_root(s);
    }
    G::EndPopup(g_ctx);
  }
  G::PopStyleVar(g_ctx, 4);
  G::PopStyleColor(g_ctx, cols);
  G::SetCursorScreenPos(g_ctx, ox, oy + kTabsH);
}

void canvas(Scene& s) {
  double w = 0, h = 0;
  G::GetContentRegionAvail(g_ctx, &w, &h);
  View v{std::max(w, kBoxW * 2), std::max(h, kBoxH * 2)};
  g_view = v;
  gfx::begin(g_ctx);
  G::InvisibleButton(g_ctx, "canvas", v.w, v.h, G::ButtonFlags_MouseButtonLeft | G::ButtonFlags_MouseButtonRight);
  double mx = 0, my = 0;
  G::GetMousePos(g_ctx, &mx, &my);
  // An invalid position (focus went elsewhere, e.g. a plugin window opened) keeps the last one, so drags don't jump.
  if (G::IsMousePosValid(g_ctx, mx, my)) g_mx = mx - gfx::ox(), g_my = my - gfx::oy();
  g_mods = G::GetKeyMods(g_ctx);
  auto& g = s.g;
  g_sh = shown(g);

  if (G::IsItemActivated(g_ctx) && G::IsMouseClicked(g_ctx, G::MouseButton_Left)) {
    int wi = wire_at(g, v, g_mx, g_my, g_sh), m = machine_at(g, v, g_mx, g_my);
    PortHit h = port_at(g, v, g_mx, g_my, g_sh);
    bool shift = g_mods & G::Mod_Shift;
    if (h.m >= 0) {
      if (h.out) g_drag = Drag::Connect, g_drag_m = h.m, g_drag_src = g.machines[h.m].outs[h.k].ch;
    } else if (m > 0 && wi < 0 && (g_mods & G::Mod_Alt)) {
      UndoBlock u(s.proj, "bazz: delete machine");
      delete_machine(s, m);
      g_sh = shown(s.g);
    } else if (m >= 0 && on_toggle(g, v, m, g_mx, g_my)) {
      set_panel(s, m, !g.machines[m].panel);
      g_sh = shown(g);
    } else if (wi >= 0 && (g_mods & G::Mod_Alt)) {
      UndoBlock u(s.proj, "bazz: disconnect");
      disconnect(s, wi);
    } else if (wi >= 0) {
      g_drag = Drag::Volume, g_drag_w = wi, g_start_vol = g.wires[wi].vol, g_start_y = g_my;
    } else if (char b = m >= 0 ? button_at(v, g.machines[m], g_mx, g_my) : 0) {
      auto& mm = g.machines[m];
      UndoBlock u(s.proj, b == 'M' ? "bazz: mute" : b == 'S' ? "bazz: solo" : "bazz: arm");
      if (b == 'M') set_mute(s, m, !mm.mute);
      else if (b == 'S') set_solo(s, m, !mm.solo);
      else set_arm(s, m, !mm.armed);
    } else if (m >= 0) {
      g_drag_m = m;
      if (shift) g_drag = Drag::Connect, g_drag_src = 0;
      else {
        g_drag = Drag::Move, g_off_x = v.sx(g.machines[m].x) - g_mx, g_off_y = v.sy(g.machines[m].y) - g_my;
        g_start_mx = g.machines[m].x, g_start_my = g.machines[m].y;
      }
    }
  }
  if (G::IsItemHovered(g_ctx) && G::IsMouseDoubleClicked(g_ctx, G::MouseButton_Left)) {
    int m = machine_at(g, v, g_mx, g_my);
    if (m > 0) show_fx(s, m);
  }
  bool down = G::IsMouseDown(g_ctx, G::MouseButton_Left);
  if (g_drag == Drag::Move) {
    double x = v.nx(g_mx + g_off_x), y = v.ny(g_my + g_off_y);
    g_splice = is_loose(g, g_drag_m) ? wire_near(g, v, v.sx(x), v.sy(y), 10, g_drag_m, g_sh) : -1;
    if (down) set_pos(s, g_drag_m, x, y);
    else if (g_splice >= 0) {
      UndoBlock u(s.proj, "bazz: insert machine");
      set_pos(s, g_drag_m, x, y);
      splice(s, g_splice, g_drag_m);
      g_splice = -1;
    } else if (std::abs(x - g_start_mx) > 1e-4 || std::abs(y - g_start_my) > 1e-4) {
      UndoBlock u(s.proj, "bazz: move machine");
      set_pos(s, g_drag_m, x, y);
    }
  } else if (g_drag == Drag::Volume) {
    double vol = std::clamp(g_start_vol - (g_my - g_start_y) / 88, 0.0, 1.0);
    if (down) set_wire_vol(s, g_drag_w, vol);
    else {
      UndoBlock u(s.proj, "bazz: wire volume");
      set_wire_vol(s, g_drag_w, vol);
    }
  } else if (g_drag == Drag::Connect) {
    PortHit h = port_at(g, v, g_mx, g_my, g_sh);
    int to = h.m >= 0 && h.m != g_drag_m ? h.m : machine_at(g, v, g_mx, g_my);
    int was = g_temp_open;
    g_temp_open = to >= 0 && to != g_drag_m && !g.machines[to].ins.empty() ? to : -1;
    if (g_temp_open != was) g_sh = shown(g);
    g_target = {to, h.m == to && h.m >= 0 && !h.out ? g.machines[to].ins[h.k].ch : 0};
    g_target.ok = to >= 0 && (h.m != to || !h.out) && can_connect(g, g_drag_m, to, g_drag_src, g_target.dst);
    if (!down) {
      if (g_target.ok) {
        UndoBlock u(s.proj, "bazz: connect");
        connect(s, g_drag_m, to, g_drag_src, g_target.dst);
      }
      g_temp_open = -1;
      g_target = {};
      g_sh = shown(s.g);
    }
  }
  if (!down) g_drag = Drag::None;

  if (G::IsItemClicked(g_ctx, G::MouseButton_Right)) {
    g_menu_w = wire_at(g, v, g_mx, g_my, g_sh);
    g_menu_m = g_menu_w < 0 ? machine_at(g, v, g_mx, g_my) : -1;
    g_menu_x = g_mx, g_menu_y = g_my;
    g_search[0] = 0, g_search_sel = 0;
    G::OpenPopup(g_ctx, "ctx");
  }
  draw(s, v);
  context_menu(s);
}

}  // namespace

void ui_init(void* (*get_func)(const char*)) { g_get_func = get_func; }
void ui_set_open(bool open) { g_open = open; }
void ui_reset_window() { g_reset = true; }
void ui_toggle_dock() {
  g_dock_req = g_docked ? 2 : 1;
  g_open = true;
}
bool ui_is_open() { return g_open; }

void ui_frame(ReaProject* proj) {
  if (!g_open) return;
  try {
    if (!g_api) {
      G::init(g_get_func);
      g_api = true;
    }
    if (!g_ctx || !G::ValidatePtr(g_ctx, "ImGui_Context*")) {
      g_ctx = G::CreateContext("bazz");
      gfx::attach_fonts(g_ctx);
      int flags = int(G::GetConfigVar(g_ctx, G::ConfigVar_Flags));  // arrow keys belong to our menus, not ImGui's nav
      G::SetConfigVar(g_ctx, G::ConfigVar_Flags, flags & ~G::ConfigFlags_NavEnableKeyboard);
    }
    Scene s = scan(proj);
    if (s.tracks.empty()) {
      UndoBlock u(proj, "bazz: create master");
      ensure_root(s);
    }
    if (g_dock_req) G::SetNextWindowDockID(g_ctx, g_dock_req == 1 ? -1 : 0);
    g_dock_req = 0;
    G::SetNextWindowSize(g_ctx, 900, 650, g_reset ? G::Cond_Always : G::Cond_FirstUseEver);
    if (g_reset) {
      G::SetNextWindowPos(g_ctx, 60, 118);
      G::SetNextWindowCollapsed(g_ctx, false);
      g_reset = false;
    }
    G::PushStyleVar(g_ctx, G::StyleVar_WindowPadding, 0, 0);
    G::PushStyleColor(g_ctx, G::Col_WindowBg, kBg);
    G::PushStyleColor(g_ctx, G::Col_TitleBgActive, kMenuHi);
    G::PushStyleColor(g_ctx, G::Col_TitleBg, kMenuShadow);
    G::PushStyleColor(g_ctx, G::Col_TitleBgCollapsed, kMenuShadow);
    G::PushFont(g_ctx, gfx::ui_font(), gfx::kFontSize);
    bool open = true;
    bool visible = G::Begin(g_ctx, "bazz - Machines", &open, G::WindowFlags_NoScrollbar | G::WindowFlags_NoScrollWithMouse);
    G::PopStyleColor(g_ctx, 4);
    G::PopStyleVar(g_ctx);
    if (visible) {
      g_docked = G::IsWindowDocked(g_ctx);
      tabs(s);
      canvas(s);
      G::End(g_ctx);
    }
    G::PopFont(g_ctx);
    g_open = open;
  } catch (const ImGui_Error& e) {
    g_error = e.what();
    ShowConsoleMsg(("bazz: ImGui error: " + g_error + "\n").c_str());
    g_open = false;
    g_ctx = nullptr;
  }
}

const char* ui_status() {
  static std::string s;
  s = std::string("ui open=") + (g_open ? "1" : "0") + " origin=" + std::to_string(int(gfx::ox())) + "," +
      std::to_string(int(gfx::oy())) + " scale=1 view=" + std::to_string(int(g_view.w)) + "x" +
      std::to_string(int(g_view.h)) + " drag=" + std::to_string(int(g_drag)) + "/" + std::to_string(g_drag_m) + "/" +
      std::to_string(g_drag_src) + " mods=" + std::to_string(g_mods) + " mouse=" + std::to_string(int(g_mx)) + "," + std::to_string(int(g_my)) + " search=[" + g_search + "]/" + std::to_string(g_search_sel) + (g_error.empty() ? "" : " error=" + g_error) + "\n";
  return s.c_str();
}
