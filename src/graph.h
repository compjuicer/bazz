#pragma once
// Machine graph as shown in the machine view. Pure logic: no REAPER, no ImGui.
#include <string>
#include <vector>

namespace bazz {

enum class Kind { Master, Generator, Effect, Input, Output };  // Input: audio input track, Output: hardware output

struct Port {
  std::string name;  // from the plugin's pin names, e.g. "Auxiliary Input"
  int ch = 0;        // first track channel of the stereo pair (0-based)
};

struct Machine {
  std::string id;  // track GUID
  std::string name;
  Kind kind = Kind::Effect;
  double x = 0, y = 0;  // normalized -1..1 across the view, like the original
  bool mute = false;
  std::vector<Port> ins, outs;  // port panel rows: main 1/2 first, then the plugin's extra pairs (empty if no extras)
  bool panel = false;           // port panels shown
};

void add_main_ports(Machine&);  // put Main first on each side that takes wires, if the machine has extra ports

struct Wire {
  int from = 0, to = 0;  // machine indices
  double vol = 1, pan = 0;
  int src = 0, dst = 0;  // first channel at each end; 0 = main
};

struct Graph {
  std::vector<Machine> machines;
  std::vector<Wire> wires;
};

constexpr double kBoxW = 100, kBoxH = 50, kArrow = 10;  // view pixels

struct View {
  double w = 800, h = 600;
  double sx(double x) const { return (x + 1) / 2 * (w - kBoxW) + kBoxW / 2; }  // box centre
  double sy(double y) const { return (y + 1) / 2 * (h - kBoxH) + kBoxH / 2; }
  double nx(double px) const;  // inverse, clamped to -1..1
  double ny(double py) const;
};

bool reaches(const Graph&, int from, int to);
bool can_connect(const Graph&, int from, int to, int src = 0, int dst = 0);
int find_wire(const Graph&, int from, int to, int src = 0, int dst = 0);
// Wires that keep the signal flowing when machine m is removed: each wire into its main input joined to each wire
// out of its main output (A->m->B becomes A->B, volumes multiplied). Indices are of g; skips duplicates and loops.
std::vector<Wire> bridges(const Graph&, int m);

int machine_at(const Graph&, const View&, double px, double py);  // -1 if none; topmost wins

// Extra ports are hidden until a machine's port panels are open: inputs in a list above the box, outputs below,
// one row per port, a connector on the left of each row. Wires on main ports, or on closed panels, run centre to
// centre; parallel wires between the same two points are spread apart.
constexpr double kRowH = 13, kToggle = 9, kConn = 9, kSpread = 6, kPanelW = 140;
struct Pt {
  double x, y;
};
struct Shown {
  std::vector<int> open;  // machines whose port panels are open
  bool has(int m) const;
};
bool has_ports(const Machine&);  // has extra ports, so a panel toggle
double panel_h(const Machine&, bool out);
Pt panel_pos(const View&, const Machine&, bool out);  // top-left
Pt port_pt(const View&, const Machine&, bool out, int k);  // centre of port k's connector
struct PortHit {
  int m = -1;
  bool out = false;
  int k = -1;
};
PortHit port_at(const Graph&, const View&, double px, double py, const Shown&);  // a row of an open panel
Pt toggle_pos(const View&, const Machine&);  // top-left of the panel toggle in the box's top right corner
Pt wire_start(const Graph&, const View&, const Wire&, const Shown& = {});
Pt wire_end(const Graph&, const View&, const Wire&, const Shown& = {});
struct Seg {
  Pt a, b;
};
Seg wire_seg(const Graph&, const View&, int wire, const Shown& = {});  // as drawn
std::string pin_pair_name(const std::string& left, const std::string& right);  // what the two pin names share

struct Tri {
  double x[3], y[3];
};
Tri arrow(const Graph&, const View&, int wire, const Shown& = {});  // at the wire's midpoint, pointing at the destination
int wire_at(const Graph&, const View&, double px, double py, const Shown& = {});  // arrow hit, -1 if none
// Wire whose line passes within r of (px, py), nearest first, ignoring wires of machine skip; -1 if none.
int wire_near(const Graph&, const View&, double px, double py, double r, int skip, const Shown& = {});
bool is_loose(const Graph&, int m);  // an effect with no wires: can be dropped onto a wire

// Installed-FX names as REAPER lists them, e.g. "VSTi: Serum (Xfer Records)".
std::string fx_short_name(const std::string&);  // "Serum"
std::string fx_type(const std::string&);        // "VSTi"
bool fx_is_instrument(const std::string&);      // VSTi, VST3i, AUi, CLAPi, LV2i
bool fx_matches(const std::string& name, const std::string& query);  // every word of query in name, any case

// Audio channel specs as REAPER encodes them: low 10 bits = first channel (0-based), 1024 = stereo pair (inputs)
// or mono mix (outputs). We use stereo = !mono for both and convert at the REAPER edge.
struct Io {
  int ch = 0;
  bool stereo = true;
};
std::string io_name(bool input, Io);  // "In 1/2", "Out 3"

struct OutNode {
  Io io;
  double x = 0, y = 0;
};
std::string outs_str(const std::vector<OutNode>&);
std::vector<OutNode> parse_outs(const std::string&);

std::string pos_str(double x, double y);
bool parse_pos(const std::string&, double* x, double* y);

}  // namespace bazz
