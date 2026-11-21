#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "graph.h"

#include <cmath>

using namespace bazz;

namespace {
Graph chain() {  // gen -> fx -> master
  Graph g;
  g.machines = {{"m", "Master", Kind::Master, 0, 0.8}, {"g", "Gen", Kind::Generator, 0, -0.8}, {"f", "Fx", Kind::Effect, 0, 0}};
  g.wires = {{1, 2}, {2, 0}};
  return g;
}
}  // namespace

TEST_CASE("reaches follows wires") {
  Graph g = chain();
  CHECK(reaches(g, 1, 0));
  CHECK(!reaches(g, 0, 1));
}

TEST_CASE("can_connect forbids cycles, duplicates, into generators, out of master") {
  Graph g = chain();
  g.machines.push_back({"f2", "Fx2", Kind::Effect});
  CHECK(!can_connect(g, 2, 1));  // into a generator
  CHECK(!can_connect(g, 0, 3));  // out of master
  CHECK(!can_connect(g, 1, 2));  // duplicate
  CHECK(!can_connect(g, 2, 2));
  CHECK(can_connect(g, 2, 3));
  g.wires.push_back({2, 3});
  CHECK(!can_connect(g, 3, 2));  // cycle
  CHECK(can_connect(g, 1, 3));   // fan-out
}

TEST_CASE("view maps normalized positions with boxes kept inside") {
  View v{800, 600};
  CHECK(v.sx(-1) == doctest::Approx(kBoxW / 2));
  CHECK(v.sx(1) == doctest::Approx(800 - kBoxW / 2));
  CHECK(v.nx(v.sx(0.3)) == doctest::Approx(0.3));
  CHECK(v.ny(-50) == -1);
}

TEST_CASE("machine_at hits boxes, topmost first") {
  Graph g = chain();
  View v{800, 600};
  CHECK(machine_at(g, v, v.sx(0), v.sy(0)) == 2);
  CHECK(machine_at(g, v, v.sx(0) + kBoxW / 2 + 1, v.sy(0)) == -1);
  g.machines[1].y = 0;
  CHECK(machine_at(g, v, v.sx(0), v.sy(0)) == 2);
}

TEST_CASE("arrow sits at the midpoint and points at the destination") {
  Graph g = chain();
  View v{800, 600};
  Tri t = arrow(g, v, 0);
  double mid = (v.sy(-0.8) + v.sy(0)) / 2;
  CHECK(t.x[0] == doctest::Approx(400));
  CHECK(t.y[0] == doctest::Approx(mid + kArrow));  // tip below: destination is below
  CHECK(wire_at(g, v, 400, mid) == 0);
  CHECK(wire_at(g, v, 400 + 2 * kArrow, mid) == -1);
}

TEST_CASE("pos round trip") {
  double x = 0, y = 0;
  CHECK(parse_pos(pos_str(-0.25, 0.5), &x, &y));
  CHECK(x == doctest::Approx(-0.25));
  CHECK(y == doctest::Approx(0.5));
  CHECK(!parse_pos("", &x, &y));
}

TEST_CASE("fx names") {
  CHECK(fx_short_name("VSTi: Serum (Xfer Records)") == "Serum");
  CHECK(fx_short_name("JS: Delay") == "Delay");
  CHECK(fx_short_name("AU: AUDelay (Apple)") == "AUDelay");
  CHECK(fx_type("VST3i: Vital (Vital Audio)") == "VST3i");
  CHECK(fx_is_instrument("VST3i: Vital (Vital Audio)"));
  CHECK(fx_is_instrument("CLAPi: Surge XT (Surge Synth Team)"));
  CHECK(!fx_is_instrument("VST: ReaEQ (Cockos)"));
  CHECK(!fx_is_instrument("JS: Delay"));
}

TEST_CASE("inputs are sources, outputs are sinks") {
  Graph g;
  g.machines = {{"m", "Master", Kind::Master}, {"i", "In 1/2", Kind::Input}, {"f", "Fx", Kind::Effect}, {"o", "Out 3/4", Kind::Output}};
  CHECK(can_connect(g, 1, 2));
  CHECK(can_connect(g, 2, 3));
  CHECK(can_connect(g, 1, 3));
  CHECK(!can_connect(g, 2, 1));
  CHECK(!can_connect(g, 3, 2));
}

TEST_CASE("io names and output list round trip") {
  CHECK(io_name(true, {0, true}) == "In 1/2");
  CHECK(io_name(false, {2, false}) == "Out 3");
  std::vector<OutNode> v = {{{2, true}, 0.5, -0.25}, {{5, false}, -1, 1}};
  auto r = parse_outs(outs_str(v));
  REQUIRE(r.size() == 2);
  CHECK(r[0].io.ch == 2);
  CHECK(r[0].io.stereo);
  CHECK(r[0].x == doctest::Approx(0.5));
  CHECK(r[1].io.ch == 5);
  CHECK(!r[1].io.stereo);
  CHECK(r[1].y == doctest::Approx(1));
  CHECK(parse_outs("").empty());
}

namespace {
Graph ported() {  // chain() where the effect has a sidechain input and two extra outputs
  Graph g = chain();
  g.machines[2].ins.push_back({"Sidechain", 2});
  g.machines[2].outs.push_back({"Out 2", 2});
  g.machines[2].outs.push_back({"Out 3", 4});
  for (auto& m : g.machines) add_main_ports(m);
  return g;
}
}  // namespace

TEST_CASE("ports: wires between the same machines on different channels") {
  Graph g = ported();
  CHECK(!can_connect(g, 1, 2));       // main already wired
  CHECK(can_connect(g, 1, 2, 0, 2));  // into the sidechain
  g.wires.push_back({1, 2, 1, 0, 0, 2});
  CHECK(!can_connect(g, 1, 2, 0, 2));
  CHECK(find_wire(g, 1, 2, 0, 2) == 2);
}

TEST_CASE("main ports join the panel only on machines with extras") {
  Graph g = ported();
  auto& fx = g.machines[2];
  CHECK(fx.ins.size() == 2);
  CHECK(fx.ins[0].name == "Main");
  CHECK(fx.outs.size() == 3);
  CHECK(fx.outs[0].ch == 0);
  CHECK(g.machines[1].outs.empty());  // plain generator: no panel
  Machine gen;
  gen.kind = Kind::Generator;
  gen.outs = {{"Out 2", 2}};
  add_main_ports(gen);
  CHECK(gen.ins.empty());
  CHECK(gen.outs.size() == 2);
}

TEST_CASE("port panels: inputs above the box, outputs below, connector on the left") {
  Graph g = ported();
  View v{800, 600};
  auto& fx = g.machines[2];
  double bx = std::round(v.sx(0) - kBoxW / 2), by = std::round(v.sy(0) - kBoxH / 2);
  CHECK(has_ports(fx));
  CHECK(!has_ports(g.machines[1]));
  CHECK(panel_h(fx, false) == 2 * kRowH + 2);
  CHECK(panel_h(fx, true) == 3 * kRowH + 2);
  CHECK(panel_pos(v, fx, false).y + panel_h(fx, false) - 1 == by);
  CHECK(panel_pos(v, fx, true).y == by + kBoxH - 1);
  Pt in = port_pt(v, fx, false, 0), out0 = port_pt(v, fx, true, 0), out1 = port_pt(v, fx, true, 1);
  CHECK(in.y < by);
  CHECK(out0.y > by + kBoxH);
  CHECK(in.x == out0.x);
  CHECK(in.x < bx + 12);
  CHECK(out1.y == out0.y + kRowH);
  Shown none, open{{2}};
  CHECK(port_at(g, v, v.sx(0), out1.y, none).m == -1);
  PortHit h = port_at(g, v, v.sx(0), out1.y, open);
  CHECK(h.m == 2);
  CHECK(h.out);
  CHECK(h.k == 1);
  h = port_at(g, v, v.sx(0), in.y, open);
  CHECK(!h.out);
  CHECK(h.k == 0);
  CHECK(port_pt(v, fx, false, 1).y == in.y + kRowH);
  CHECK(port_at(g, v, v.sx(0), v.sy(0), open).m == -1);
}

TEST_CASE("wires run centre to centre unless the panel is open") {
  Graph g = ported();
  View v{800, 600};
  Wire w{1, 2, 1, 0, 0, 2};
  Pt closed = wire_end(g, v, w), opened = wire_end(g, v, w, Shown{{2}});
  CHECK(closed.x == v.sx(0));
  CHECK(closed.y == v.sy(0));
  CHECK(opened.y == port_pt(v, g.machines[2], false, 1).y);
  Wire main{1, 2};
  CHECK(wire_end(g, v, main, Shown{{2}}).y == port_pt(v, g.machines[2], false, 0).y);  // main row when open
}

TEST_CASE("parallel wires spread apart") {
  Graph g = ported();
  View v{800, 600};
  g.wires.push_back({1, 2, 1, 0, 0, 2});  // gen -> fx sidechain, same centres as gen -> fx main while closed
  Seg a = wire_seg(g, v, 0), b = wire_seg(g, v, 2), c = wire_seg(g, v, 1);
  CHECK(std::abs(a.a.x - b.a.x) == doctest::Approx(kSpread));  // vertical wire: spread sideways
  CHECK(c.a.x == v.sx(0));                                     // fx -> master alone, untouched
  Seg open = wire_seg(g, v, 2, Shown{{2}}), main = wire_seg(g, v, 0, Shown{{2}});
  CHECK(open.b.y == port_pt(v, g.machines[2], false, 1).y);    // on the open panel: separate rows, not parallel
  CHECK(main.b.y == port_pt(v, g.machines[2], false, 0).y);
  CHECK(main.a.x == v.sx(0));
}

TEST_CASE("pin pair names") {
  CHECK(pin_pair_name("Auxiliary Input L", "Auxiliary Input R") == "Auxiliary Input");
  CHECK(pin_pair_name("sidechain left input", "sidechain right input") == "sidechain");
  CHECK(pin_pair_name("Out 2 1", "Out 2 2") == "Out 2");
  CHECK(pin_pair_name("Output 3,4 L", "Output 3,4 R") == "Output 3,4");
  CHECK(pin_pair_name("AU output 3", "AU output 4") == "AU output");
  CHECK(pin_pair_name("Output 0", "") == "Output 0");
  CHECK(pin_pair_name("Kick", "Snare") == "Kick");
}


TEST_CASE("fx search") {
  CHECK(fx_matches("VST: ReaComp (Cockos)", "comp"));
  CHECK(fx_matches("VST: ReaComp (Cockos)", "vst COCK"));
  CHECK(fx_matches("VST3i: Xenia (The Usual Suspects)", " usual  xen "));
  CHECK(!fx_matches("VST: ReaComp (Cockos)", "comp eq"));
  CHECK(fx_matches("JS: Delay", ""));
}

TEST_CASE("bridges around a removed machine") {
  Graph g = chain();  // gen(1) -> fx(2) -> master(0)
  g.wires[0].vol = 0.5;
  auto b = bridges(g, 2);
  REQUIRE(b.size() == 1);
  CHECK(b[0].from == 1);
  CHECK(b[0].to == 0);
  CHECK(b[0].vol == doctest::Approx(0.5));
  g.machines.push_back({"f2", "Fx2", Kind::Effect});  // a second source into fx
  g.wires.push_back({3, 2});
  g.wires.push_back({1, 0});  // already there: no duplicate bridge
  b = bridges(g, 2);
  REQUIRE(b.size() == 1);
  CHECK(b[0].from == 3);
  g.wires.push_back({1, 2, 1, 0, 0, 2});  // into a sidechain: not bridged
  CHECK(bridges(g, 2).size() == 1);
  CHECK(bridges(g, 1).empty());  // a generator has no inputs
}

TEST_CASE("dropping a loose effect onto a wire") {
  Graph g = chain();  // gen(1) at top -> fx(2) middle -> master(0) bottom, all at x = 0
  View v{800, 600};
  g.machines.push_back({"f2", "Fx2", Kind::Effect, 0.5, 0.5});
  CHECK(is_loose(g, 3));
  CHECK(!is_loose(g, 2));
  CHECK(!is_loose(g, 1));
  double y = (v.sy(0) + v.sy(0.8)) / 2;                      // halfway down fx -> master
  CHECK(wire_near(g, v, v.sx(0) + 5, y, 10, 3) == 1);
  CHECK(wire_near(g, v, v.sx(0) + 15, y, 10, 3) == -1);
  CHECK(wire_near(g, v, v.sx(0), y, 10, 2) == -1);             // the machine's own wires don't count
}
