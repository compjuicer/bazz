#include "reaper_graph.h"

#include "reaper_core_api.h"

#include <algorithm>

using namespace bazz;

namespace {

const char* kRole = "P_EXT:bazz_role";
const char* kPos = "P_EXT:bazz_pos";
const char* kOuts = "P_EXT:bazz_outs";
const char* kPanel = "P_EXT:bazz_panel";

std::string get_str(MediaTrack* tr, const char* key) {
  char buf[4096] = "";
  GetSetMediaTrackInfo_String(tr, key, buf, false);
  return buf;
}

void set_str(MediaTrack* tr, const char* key, const std::string& v) {
  GetSetMediaTrackInfo_String(tr, key, const_cast<char*>(v.c_str()), true);
}

std::string guid(MediaTrack* tr) {
  char buf[64];
  guidToString(GetTrackGUID(tr), buf);
  return buf;
}

int track_index(MediaTrack* tr) { return int(GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER")) - 1; }

MediaTrack* find_root(ReaProject* proj) {
  for (int i = 0; i < CountTracks(proj); ++i)
    if (get_str(GetTrack(proj, i), kRole) == "root") return GetTrack(proj, i);
  return nullptr;
}

std::vector<MediaTrack*> children(ReaProject* proj, MediaTrack* root) {
  std::vector<MediaTrack*> v;
  for (int i = track_index(root) + 1; i < CountTracks(proj) && GetParentTrack(GetTrack(proj, i)) == root; ++i)
    v.push_back(GetTrack(proj, i));
  return v;
}

int dst_chan(Io io) { return io.ch | (io.stereo ? 0 : 1024); }

int out_machine(const Scene& s, Io io) {
  for (size_t k = 0; k < s.outs.size(); ++k)
    if (s.outs[k].io.ch == io.ch && s.outs[k].io.stereo == io.stereo) return s.n_tracks + int(k);
  return -1;
}

void save_outs(Scene& s) { set_str(s.tracks[0], kOuts, outs_str(s.outs)); }

void add_out_machine(Scene& s, const OutNode& o) {
  s.outs.push_back(o);
  s.tracks.push_back(nullptr);
  Machine m;
  m.id = "out" + std::to_string(dst_chan(o.io));
  m.name = io_name(false, o.io);
  m.kind = Kind::Output;
  m.x = o.x, m.y = o.y;
  s.g.machines.push_back(m);
}

MediaTrack* add_child(Scene& s, const std::string& name, const char* role, double x, double y) {
  MediaTrack* root = s.tracks[0];
  auto kids = children(s.proj, root);
  if (kids.empty()) SetMediaTrackInfo_Value(root, "I_FOLDERDEPTH", 1);
  else SetMediaTrackInfo_Value(kids.back(), "I_FOLDERDEPTH", 0);
  int idx = track_index(root) + 1 + int(kids.size());
  InsertTrackAtIndex(idx, true);
  MediaTrack* tr = GetTrack(s.proj, idx);
  SetMediaTrackInfo_Value(tr, "I_FOLDERDEPTH", -1);
  SetMediaTrackInfo_Value(tr, "B_MAINSEND", 0);
  SetMediaTrackInfo_Value(tr, "B_SHOWINMIXER", 0);
  SetMediaTrackInfo_Value(tr, "B_SHOWINTCP", 0);
  set_str(tr, kRole, role);
  set_str(tr, kPos, pos_str(x, y));
  set_str(tr, "P_NAME", name);
  return tr;
}

std::vector<Port> ports(MediaTrack* tr, bool out) {
  std::vector<Port> v;
  int ins = 0, outs = 0;
  if (TrackFX_GetCount(tr) == 0 || !TrackFX_GetIOSize(tr, 0, &ins, &outs)) return v;
  int n = out ? outs : ins;
  auto pin = [&](int p) {
    char name[256] = "";
    if (p < n) TrackFX_GetNamedConfigParm(tr, 0, ((out ? "out_pin_" : "in_pin_") + std::to_string(p)).c_str(), name, sizeof name);
    return std::string(name);
  };
  for (int p = 2; p < n; p += 2) v.push_back({pin_pair_name(pin(p), pin(p + 1)), p});
  return v;
}

void need_channels(MediaTrack* tr, int ch) {
  int n = (ch + 3) & ~1;
  if (GetMediaTrackInfo_Value(tr, "I_NCHAN") < n) SetMediaTrackInfo_Value(tr, "I_NCHAN", n);
}

void monitor_input(MediaTrack* tr, int recinput) {
  SetMediaTrackInfo_Value(tr, "I_RECINPUT", recinput);
  SetMediaTrackInfo_Value(tr, "I_RECMODE", 2);  // input monitoring only, records nothing
  SetMediaTrackInfo_Value(tr, "I_RECMON", 1);
}

int find_machine(const Scene& s, const std::string& id) {
  for (int i = 0; i < int(s.g.machines.size()); ++i)
    if (s.g.machines[i].id == id) return i;
  return -1;
}

}  // namespace

UndoBlock::UndoBlock(ReaProject* p, const char* d) : proj(p), desc(d) {
  Undo_BeginBlock2(proj);
  PreventUIRefresh(1);
}

UndoBlock::~UndoBlock() {
  PreventUIRefresh(-1);
  TrackList_AdjustWindows(false);
  Undo_EndBlock2(proj, desc, -1);
}

Scene scan(ReaProject* proj) {
  Scene s;
  s.proj = proj;
  MediaTrack* root = find_root(proj);
  if (!root) return s;
  s.tracks.push_back(root);
  for (MediaTrack* tr : children(proj, root)) s.tracks.push_back(tr);
  for (size_t i = 0; i < s.tracks.size(); ++i) {
    MediaTrack* tr = s.tracks[i];
    Machine m;
    m.id = guid(tr);
    char name[256] = "";
    GetTrackName(tr, name, sizeof name);
    m.name = i == 0 ? "Master" : name;
    m.kind = i == 0                               ? Kind::Master
             : get_str(tr, kRole) == "input"      ? Kind::Input
             : TrackFX_GetInstrument(tr) >= 0     ? Kind::Generator
                                                  : Kind::Effect;
    if (!parse_pos(get_str(tr, kPos), &m.x, &m.y)) m.x = m.y = 0;
    m.mute = GetMediaTrackInfo_Value(tr, "B_MUTE") != 0;
    m.panel = get_str(tr, kPanel) == "1";
    if (m.kind == Kind::Effect) m.ins = ports(tr, false);
    if (m.kind == Kind::Effect || m.kind == Kind::Generator) m.outs = ports(tr, true);
    add_main_ports(m);
    s.g.machines.push_back(m);
  }
  s.n_tracks = int(s.tracks.size());
  for (auto& o : parse_outs(get_str(root, kOuts))) add_out_machine(s, o);
  for (int i = 1; i < s.n_tracks; ++i) {
    MediaTrack* tr = s.tracks[i];
    for (int k = 0; k < GetTrackNumSends(tr, 0); ++k) {
      auto dest = (MediaTrack*)(intptr_t)GetTrackSendInfo_Value(tr, 0, k, "P_DESTTRACK");
      auto j = std::find(s.tracks.begin(), s.tracks.end(), dest) - s.tracks.begin();
      int src = int(GetTrackSendInfo_Value(tr, 0, k, "I_SRCCHAN"));
      if (j == int(s.tracks.size()) || src < 0) continue;
      s.g.wires.push_back({i, int(j), GetTrackSendInfo_Value(tr, 0, k, "D_VOL"), GetTrackSendInfo_Value(tr, 0, k, "D_PAN"),
                           src & 1023, int(GetTrackSendInfo_Value(tr, 0, k, "I_DSTCHAN")) & 1023});
      s.refs.push_back({0, k});
    }
    for (int k = 0; k < GetTrackNumSends(tr, 1); ++k) {
      int dst = int(GetTrackSendInfo_Value(tr, 1, k, "I_DSTCHAN"));
      Io io{dst & 1023, !(dst & 1024)};
      int j = out_machine(s, io);
      if (j < 0) {  // hardware output added outside bazz: show it
        add_out_machine(s, {io, 0.9, 0.9});
        j = int(s.g.machines.size()) - 1;
      }
      int src = int(GetTrackSendInfo_Value(tr, 1, k, "I_SRCCHAN"));
      if (src < 0) continue;
      s.g.wires.push_back({i, j, GetTrackSendInfo_Value(tr, 1, k, "D_VOL"), GetTrackSendInfo_Value(tr, 1, k, "D_PAN"), src & 1023, 0});
      s.refs.push_back({1, k});
    }
  }
  return s;
}

void ensure_root(Scene& s) {
  if (!s.tracks.empty()) return;
  int n = CountTracks(s.proj);
  InsertTrackAtIndex(n, true);
  MediaTrack* root = GetTrack(s.proj, n);
  set_str(root, kRole, "root");
  set_str(root, "P_NAME", "bazz");
  set_str(root, kPos, pos_str(0, 0));
  s = scan(s.proj);
}

int add_machine(Scene& s, const std::string& fx, double x, double y) {
  MediaTrack* tr = add_child(s, fx_short_name(fx), "machine", x, y);
  if (TrackFX_AddByName(tr, fx.c_str(), false, -1) < 0) {
    s = scan(s.proj);
    delete_machine(s, s.n_tracks - 1);
    return -1;
  }
  if (fx_is_instrument(fx)) monitor_input(tr, 4096 | (63 << 5));  // MIDI, all inputs, all channels
  std::string id = guid(tr);
  s = scan(s.proj);
  return find_machine(s, id);
}

void splice(Scene& s, int wire, int m) {
  Wire w = s.g.wires[wire];
  std::string from = s.g.machines[w.from].id, to = s.g.machines[w.to].id, mid = s.g.machines[m].id;
  disconnect(s, wire);
  int a = find_machine(s, from), b = find_machine(s, to);
  m = find_machine(s, mid);
  connect(s, a, m, w.src, 0);
  connect(s, m, b, 0, w.dst);
  set_wire_vol(s, find_wire(s.g, a, m, w.src, 0), w.vol);
}

void insert_machine(Scene& s, int wire, const std::string& fx, double x, double y) {
  Wire w = s.g.wires[wire];
  std::string from = s.g.machines[w.from].id, to = s.g.machines[w.to].id;
  int m = add_machine(s, fx, x, y);
  if (m < 0) return;
  splice(s, find_wire(s.g, find_machine(s, from), find_machine(s, to), w.src, w.dst), m);
}

void add_input(Scene& s, Io io, double x, double y) {
  MediaTrack* tr = add_child(s, io_name(true, io), "input", x, y);
  monitor_input(tr, io.ch | (io.stereo ? 1024 : 0));
  SetMediaTrackInfo_Value(tr, "I_RECARM", 1);
  s = scan(s.proj);
}

void add_output(Scene& s, Io io, double x, double y) {
  if (out_machine(s, io) >= 0) return;
  s.outs.push_back({io, x, y});
  save_outs(s);
  s = scan(s.proj);
}

void delete_machine(Scene& s, int m) {
  if (m <= 0) return;
  if (!s.tracks[m]) {
    for (int w = int(s.g.wires.size()) - 1; w >= 0; --w)
      if (s.g.wires[w].to == m) disconnect(s, w);
    s.outs.erase(s.outs.begin() + (m - s.n_tracks));
    save_outs(s);
    s = scan(s.proj);
    return;
  }
  std::vector<std::pair<std::string, std::string>> ends;
  auto keep = bridges(s.g, m);
  for (auto& b : keep) ends.push_back({s.g.machines[b.from].id, s.g.machines[b.to].id});
  auto kids = children(s.proj, s.tracks[0]);
  if (kids.back() == s.tracks[m]) {
    if (kids.size() > 1) SetMediaTrackInfo_Value(kids[kids.size() - 2], "I_FOLDERDEPTH", -1);
    else SetMediaTrackInfo_Value(s.tracks[0], "I_FOLDERDEPTH", 0);
    SetMediaTrackInfo_Value(s.tracks[m], "I_FOLDERDEPTH", 0);
  }
  DeleteTrack(s.tracks[m]);
  s = scan(s.proj);
  for (size_t i = 0; i < keep.size(); ++i) {
    int a = find_machine(s, ends[i].first), b = find_machine(s, ends[i].second);
    connect(s, a, b, keep[i].src, keep[i].dst);
    set_wire_vol(s, find_wire(s.g, a, b, keep[i].src, keep[i].dst), keep[i].vol);
  }
}

void connect(Scene& s, int from, int to, int src, int dst) {
  if (!can_connect(s.g, from, to, src, dst)) return;
  MediaTrack* tr = s.tracks[from];
  need_channels(tr, src);
  if (s.tracks[to]) {
    need_channels(s.tracks[to], dst);
    int k = CreateTrackSend(tr, s.tracks[to]);
    SetTrackSendInfo_Value(tr, 0, k, "I_SENDMODE", 0);
    SetTrackSendInfo_Value(tr, 0, k, "I_SRCCHAN", src);
    SetTrackSendInfo_Value(tr, 0, k, "I_DSTCHAN", dst);
  } else {
    int k = CreateTrackSend(tr, nullptr);
    SetTrackSendInfo_Value(tr, 1, k, "I_SRCCHAN", src);
    SetTrackSendInfo_Value(tr, 1, k, "I_DSTCHAN", dst_chan(s.outs[to - s.n_tracks].io));
  }
  s = scan(s.proj);
}

void disconnect(Scene& s, int w) {
  RemoveTrackSend(s.tracks[s.g.wires[w].from], s.refs[w].cat, s.refs[w].idx);
  s = scan(s.proj);
}

void set_wire_vol(Scene& s, int w, double vol) {
  SetTrackSendInfo_Value(s.tracks[s.g.wires[w].from], s.refs[w].cat, s.refs[w].idx, "D_VOL", vol);
  s.g.wires[w].vol = vol;
}

void set_pos(Scene& s, int m, double x, double y) {
  s.g.machines[m].x = x, s.g.machines[m].y = y;
  if (s.tracks[m]) {
    set_str(s.tracks[m], kPos, pos_str(x, y));
    return;
  }
  auto& o = s.outs[m - s.n_tracks];
  o.x = x, o.y = y;
  save_outs(s);
}

void set_mute(Scene& s, int m, bool on) {
  SetMediaTrackInfo_Value(s.tracks[m], "B_MUTE", on);
  s.g.machines[m].mute = on;
}

void set_panel(Scene& s, int m, bool open) {
  set_str(s.tracks[m], kPanel, open ? "1" : "");
  s.g.machines[m].panel = open;
}

void arm_only(Scene& s, int m) {
  for (int i = 1; i < s.n_tracks; ++i)
    if (s.g.machines[i].kind == Kind::Generator) SetMediaTrackInfo_Value(s.tracks[i], "I_RECARM", i == m);
}

void show_fx(Scene& s, int m) {
  if (m > 0 && s.tracks[m] && TrackFX_GetCount(s.tracks[m]) > 0) TrackFX_Show(s.tracks[m], 0, 3);
}

std::vector<std::string> installed_fx() {
  std::vector<std::string> v;
  const char *name = nullptr, *ident = nullptr;
  for (int i = 0; EnumInstalledFX(i, &name, &ident); ++i) v.push_back(name);
  return v;
}

int num_inputs() { return GetNumAudioInputs(); }
int num_outputs() { return GetNumAudioOutputs(); }
