#include "graph.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bazz {

double View::nx(double px) const { return std::clamp((px - kBoxW / 2) / (w - kBoxW) * 2 - 1, -1.0, 1.0); }
double View::ny(double py) const { return std::clamp((py - kBoxH / 2) / (h - kBoxH) * 2 - 1, -1.0, 1.0); }

bool reaches(const Graph& g, int from, int to) {
  std::vector<bool> seen(g.machines.size());
  std::vector<int> stack{from};
  while (!stack.empty()) {
    int m = stack.back();
    stack.pop_back();
    if (m == to) return true;
    if (seen[m]) continue;
    seen[m] = true;
    for (auto& w : g.wires)
      if (w.from == m) stack.push_back(w.to);
  }
  return false;
}

int find_wire(const Graph& g, int from, int to, int src, int dst) {
  for (int i = 0; i < int(g.wires.size()); ++i) {
    auto& w = g.wires[i];
    if (w.from == from && w.to == to && w.src == src && w.dst == dst) return i;
  }
  return -1;
}

bool takes_input(Kind k) { return k == Kind::Effect || k == Kind::Master || k == Kind::Output || k == Kind::Track; }
bool gives_output(Kind k) { return k == Kind::Effect || k == Kind::Generator || k == Kind::Input || k == Kind::Track; }

bool can_connect(const Graph& g, int from, int to, int src, int dst) {
  return from != to && gives_output(g.machines[from].kind) && takes_input(g.machines[to].kind) &&
         find_wire(g, from, to, src, dst) < 0 && !reaches(g, to, from);
}

std::vector<bool> solo_set(const Graph& g) {
  int n = int(g.machines.size());
  std::vector<bool> on(n);
  for (int s = 0; s < n; ++s)
    if (g.machines[s].solo)
      for (int i = 0; i < n; ++i)
        if (reaches(g, s, i) || reaches(g, i, s)) on[i] = true;
  return on;
}

std::string buttons(Kind k) {
  if (k == Kind::Generator) return "MSR";
  if (k == Kind::Master) return "M";
  if (k == Kind::Output) return "";
  return "MS";
}

bool Shown::has(int m) const { return std::find(open.begin(), open.end(), m) != open.end(); }
bool has_ports(const Machine& m) {
  for (auto* ports : {&m.ins, &m.outs})
    for (auto& p : *ports)
      if (p.ch) return true;
  return false;
}

void add_main_ports(Machine& m) {
  if (!has_ports(m)) return;
  if (takes_input(m.kind)) m.ins.insert(m.ins.begin(), {"Main", 0});
  if (gives_output(m.kind)) m.outs.insert(m.outs.begin(), {"Main", 0});
}
double panel_h(const Machine& m, bool out) { return (out ? m.outs : m.ins).size() * kRowH + 2; }

namespace {
double bx(const View& v, const Machine& m) { return std::round(v.sx(m.x) - kBoxW / 2); }
double by(const View& v, const Machine& m) { return std::round(v.sy(m.y) - kBoxH / 2); }
}  // namespace

Pt panel_pos(const View& v, const Machine& m, bool out) {
  return {bx(v, m), out ? by(v, m) + kBoxH - 1 : by(v, m) - panel_h(m, false) + 1};
}

Pt port_pt(const View& v, const Machine& m, bool out, int k) {
  Pt p = panel_pos(v, m, out);
  return {p.x + 3 + kConn / 2, p.y + 1 + k * kRowH + kRowH / 2};
}

PortHit port_at(const Graph& g, const View& v, double px, double py, const Shown& sh) {
  for (int i = int(g.machines.size()) - 1; i >= 0; --i) {
    auto& m = g.machines[i];
    if (!sh.has(i)) continue;
    for (bool out : {false, true}) {
      auto& ports = out ? m.outs : m.ins;
      Pt p = panel_pos(v, m, out);
      if (ports.empty() || px < p.x || px >= p.x + kPanelW || py < p.y || py >= p.y + panel_h(m, out)) continue;
      return {i, out, std::clamp(int((py - p.y - 1) / kRowH), 0, int(ports.size()) - 1)};
    }
  }
  return {};
}

Pt button_pos(const View& v, const Machine& m, char c) {
  if (c == 'R') return {bx(v, m) + 2, by(v, m) + kBoxH - 2 - kBtnH};
  return {bx(v, m) + 2 + (c == 'S') * kBtnStep, by(v, m) + 2};
}

char button_at(const View& v, const Machine& m, double px, double py) {
  for (char c : buttons(m.kind)) {
    Pt p = button_pos(v, m, c);
    if (px >= p.x && px < p.x + kBtnW && py >= p.y && py < p.y + kBtnH) return c;
  }
  return 0;
}

Pt toggle_pos(const View& v, const Machine& m) { return {bx(v, m) + kBoxW - kToggle - 2, by(v, m) + 2}; }

namespace {
Pt end_point(const View& v, const Graph& g, int mi, bool out, int ch, const Shown& sh) {
  auto& m = g.machines[mi];
  auto& ports = out ? m.outs : m.ins;
  if (sh.has(mi))
    for (int k = 0; k < int(ports.size()); ++k)
      if (ports[k].ch == ch) return port_pt(v, m, out, k);
  return {v.sx(m.x), v.sy(m.y)};
}

bool same(Pt a, Pt b) { return std::abs(a.x - b.x) < 0.5 && std::abs(a.y - b.y) < 0.5; }
}  // namespace

Pt wire_start(const Graph& g, const View& v, const Wire& w, const Shown& sh) { return end_point(v, g, w.from, true, w.src, sh); }
Pt wire_end(const Graph& g, const View& v, const Wire& w, const Shown& sh) { return end_point(v, g, w.to, false, w.dst, sh); }

Seg wire_seg(const Graph& g, const View& v, int wi, const Shown& sh) {
  auto& w = g.wires[wi];
  Seg s{wire_start(g, v, w, sh), wire_end(g, v, w, sh)};
  int n = 0, idx = 0;
  for (int j = 0; j < int(g.wires.size()); ++j) {
    auto& o = g.wires[j];
    if (o.from != w.from || o.to != w.to || !same(wire_start(g, v, o, sh), s.a) || !same(wire_end(g, v, o, sh), s.b)) continue;
    if (j == wi) idx = n;
    ++n;
  }
  if (n < 2) return s;
  double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y, len = std::max(std::hypot(dx, dy), 1e-9);
  double off = (idx - (n - 1) / 2.0) * kSpread, ox = -dy / len * off, oy = dx / len * off;
  return {{s.a.x + ox, s.a.y + oy}, {s.b.x + ox, s.b.y + oy}};
}

std::string pin_pair_name(const std::string& a, const std::string& b) {
  if (b.empty()) return a;
  size_t n = 0;
  while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
  std::string s = a.substr(0, n);
  while (!s.empty() && strchr(" -_,(", s.back())) s.pop_back();
  return s.empty() ? a : s;
}

std::vector<Wire> bridges(const Graph& g, int m) {
  Graph rest = g;  // without m's wires, to check loops and duplicates
  rest.wires.clear();
  for (auto& w : g.wires)
    if (w.from != m && w.to != m) rest.wires.push_back(w);
  std::vector<Wire> out;
  for (auto& in : g.wires)
    for (auto& o : g.wires)
      if (in.to == m && in.dst == 0 && o.from == m && o.src == 0 && can_connect(rest, in.from, o.to, in.src, o.dst)) {
        Wire b{in.from, o.to, in.vol * o.vol, 0, in.src, o.dst};
        rest.wires.push_back(b);
        out.push_back(b);
      }
  return out;
}

int machine_at(const Graph& g, const View& v, double px, double py) {
  for (int i = int(g.machines.size()) - 1; i >= 0; --i) {
    auto& m = g.machines[i];
    if (std::abs(px - v.sx(m.x)) <= kBoxW / 2 && std::abs(py - v.sy(m.y)) <= kBoxH / 2) return i;
  }
  return -1;
}

Tri arrow(const Graph& g, const View& v, int wi, const Shown& sh) {
  Seg s = wire_seg(g, v, wi, sh);
  double x0 = s.a.x, y0 = s.a.y, x1 = s.b.x, y1 = s.b.y;
  double dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
  if (len < 1e-9) dx = 1, dy = 0, len = 1;
  dx /= len, dy /= len;
  double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, r = kArrow;
  // equilateral, tip towards the destination, centred on the midpoint
  return {{cx + dx * r, cx - dx * r / 2 - dy * r * 0.866, cx - dx * r / 2 + dy * r * 0.866},
          {cy + dy * r, cy - dy * r / 2 + dx * r * 0.866, cy - dy * r / 2 - dx * r * 0.866}};
}

int wire_at(const Graph& g, const View& v, double px, double py, const Shown& sh) {
  for (int i = int(g.wires.size()) - 1; i >= 0; --i) {
    Tri t = arrow(g, v, i, sh);
    double cx = (t.x[0] + t.x[1] + t.x[2]) / 3, cy = (t.y[0] + t.y[1] + t.y[2]) / 3;
    if (std::hypot(px - cx, py - cy) <= kArrow) return i;
  }
  return -1;
}

int wire_near(const Graph& g, const View& v, double px, double py, double r, int skip, const Shown& sh) {
  int best = -1;
  for (int i = 0; i < int(g.wires.size()); ++i) {
    if (g.wires[i].from == skip || g.wires[i].to == skip) continue;
    Seg s = wire_seg(g, v, i, sh);
    double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y, len2 = std::max(dx * dx + dy * dy, 1e-9);
    double t = std::clamp(((px - s.a.x) * dx + (py - s.a.y) * dy) / len2, 0.0, 1.0);
    double d = std::hypot(px - s.a.x - t * dx, py - s.a.y - t * dy);
    if (d <= r) r = d, best = i;
  }
  return best;
}

bool is_loose(const Graph& g, int m) {
  if (g.machines[m].kind != Kind::Effect && g.machines[m].kind != Kind::Track) return false;
  for (auto& w : g.wires)
    if (w.from == m || w.to == m) return false;
  return true;
}

std::string fx_type(const std::string& s) {
  size_t c = s.find(':');
  return c == std::string::npos ? "" : s.substr(0, c);
}

std::string fx_short_name(const std::string& s) {
  size_t c = s.find(": ");
  std::string n = c == std::string::npos ? s : s.substr(c + 2);
  size_t p = n.rfind(" (");
  if (p != std::string::npos && p > 0 && n.back() == ')') n.resize(p);
  return n;
}

bool fx_is_instrument(const std::string& s) {
  std::string t = fx_type(s);
  return t.size() > 1 && t.back() == 'i' && t != "JS";
}

bool fx_matches(const std::string& name, const std::string& query) {
  auto lower = [](std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
  };
  std::string n = lower(name), q = lower(query);
  size_t i = 0;
  while ((i = q.find_first_not_of(' ', i)) != std::string::npos) {
    size_t e = q.find(' ', i);
    if (n.find(q.substr(i, e - i)) == std::string::npos) return false;
    i = e;
  }
  return true;
}

std::string io_name(bool input, Io io) {
  std::string s = (input ? "In " : "Out ") + std::to_string(io.ch + 1);
  return io.stereo ? s + "/" + std::to_string(io.ch + 2) : s;
}

std::string outs_str(const std::vector<OutNode>& v) {
  std::string s;
  char buf[96];
  for (auto& o : v) {
    snprintf(buf, sizeof buf, "%s%d %d %.4f %.4f", s.empty() ? "" : ";", o.io.ch, o.io.stereo, o.x, o.y);
    s += buf;
  }
  return s;
}

std::vector<OutNode> parse_outs(const std::string& s) {
  std::vector<OutNode> v;
  size_t i = 0;
  while (i < s.size()) {
    size_t e = s.find(';', i);
    if (e == std::string::npos) e = s.size();
    OutNode o;
    int st = 1;
    if (sscanf(s.substr(i, e - i).c_str(), "%d %d %lf %lf", &o.io.ch, &st, &o.x, &o.y) == 4) {
      o.io.stereo = st;
      v.push_back(o);
    }
    i = e + 1;
  }
  return v;
}

std::string pos_str(double x, double y) {
  char buf[64];
  snprintf(buf, sizeof buf, "%.4f %.4f", x, y);
  return buf;
}

bool parse_pos(const std::string& s, double* x, double* y) { return sscanf(s.c_str(), "%lf %lf", x, y) == 2; }

}  // namespace bazz
