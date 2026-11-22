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

// A project can hold several graphs, each its own tagged folder track (its own Master). The selected one is saved
// in the project (ProjExtState bazz/graph); scan() reads it.
struct GraphInfo {
  std::string id, name;  // root track GUID, folder track name
};
std::vector<GraphInfo> graphs(ReaProject*);
std::string selected_graph(ReaProject*);  // id; the first graph if the saved one is gone
void select_graph(ReaProject*, const std::string& id);

Scene scan(ReaProject*);  // the selected graph; empty if the project has none yet

struct UndoBlock {
  ReaProject* proj;
  const char* desc;
  explicit UndoBlock(ReaProject* p, const char* d);
  ~UndoBlock();
};

void ensure_root(Scene&);  // create a first graph if the project has none
void new_graph(Scene&);     // add a graph and select it
void delete_graph(Scene&);  // the selected graph, folder and machines
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
void set_arm(Scene&, int m, bool);
void set_solo(Scene&, int m, bool);  // S on m, then REAPER solo for every machine in a soloed chain

// Ordinary project tracks (not part of any graph) that can be brought in as track nodes.
struct TrackInfo {
  std::string id, name;
  int number;  // 1-based, as REAPER shows it
};
std::vector<TrackInfo> free_tracks(ReaProject*);
int add_track_node(Scene&, const std::string& id, double x, double y);  // the node's index
void insert_track(Scene&, int wire, const std::string& id, double x, double y);  // A->B becomes A->track->B
void show_fx(Scene&, int m);
std::vector<std::string> installed_fx();
int num_inputs();
int num_outputs();
