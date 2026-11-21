#pragma once
// The machine graph lives in REAPER: a folder track tagged as root (the Master machine) with one hidden child track
// per machine. Wires are sends between them; a wire to Master is a send to the root track. Audio inputs are child
// tracks with that input armed and monitored; hardware outputs are nodes listed on the root track, and wires into
// them are hardware output sends.
#include "graph.h"
#include "reaper_plugin.h"

#include <string>
#include <vector>

struct Scene {
  struct Ref {
    int cat, idx;  // REAPER send category (0 track send, 1 hardware output) and index on the source track
  };
  ReaProject* proj = nullptr;
  bazz::Graph g;
  std::vector<MediaTrack*> tracks;  // per machine, nullptr for outputs; [0] is the root (Master)
  int n_tracks = 0;                 // machines with a track come first
  std::vector<bazz::OutNode> outs;  // machine n_tracks + k
  std::vector<Ref> refs;            // per wire
};

Scene scan(ReaProject*);  // empty graph if there is no root yet

struct UndoBlock {
  ReaProject* proj;
  const char* desc;
  explicit UndoBlock(ReaProject* p, const char* d);
  ~UndoBlock();
};

void ensure_root(Scene&);
int add_machine(Scene&, const std::string& fx, double x, double y);  // new machine's index, -1 if the plugin failed
void insert_machine(Scene&, int wire, const std::string& fx, double x, double y);  // A->B becomes A->fx->B
void splice(Scene&, int wire, int m);  // A->B becomes A->m->B
void add_input(Scene&, bazz::Io, double x, double y);
void add_output(Scene&, bazz::Io, double x, double y);
void delete_machine(Scene&, int m);
void connect(Scene&, int from, int to, int src = 0, int dst = 0);
void disconnect(Scene&, int wire);
void set_wire_vol(Scene&, int wire, double vol);
void set_pos(Scene&, int m, double x, double y);
void set_mute(Scene&, int m, bool);
void set_panel(Scene&, int m, bool open);  // saved with the project, no undo point
void arm_only(Scene&, int m);  // record-arm m (a generator) for live MIDI, disarm the other generators
void show_fx(Scene&, int m);
std::vector<std::string> installed_fx();
int num_inputs();
int num_outputs();
