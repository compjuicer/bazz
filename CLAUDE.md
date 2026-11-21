# bazz

A REAPER extension (C++17, ReaImGui) that recreates the machine view of a classic Windows tracker: a node graph of generators and effects wired into Master, look included. Refer to that program as "the original"; never write its name anywhere in the repo, commit messages included.

## How the graph lives in REAPER
- REAPER is the source of truth; the view rescans every frame. Undo and save are REAPER's.
- A folder track tagged `P_EXT:bazz_role=root` is the Master machine. Each machine is a hidden child track (`role=machine|input`) with its position in `P_EXT:bazz_pos` ("x y", normalized -1..1 across the view, like the original) and its port panel state in `P_EXT:bazz_panel`.
- Wires are post-fader sends; a wire to Master is a send to the root track. `I_SRCCHAN`/`I_DSTCHAN` pick the stereo pair for extra plugin ports (sidechains, multi-outs); the track gets enough channels when wired.
- Hardware output nodes are listed on the root (`P_EXT:bazz_outs`); wires into them are hardware output sends. Audio input nodes are armed, monitored child tracks that record nothing.
- Generators are tracks whose first FX is an instrument; selecting one arms it for live MIDI.

## Layout
- `src/graph.*`: pure model and geometry (no REAPER, no ImGui), covered by `tests/`.
- `src/reaper_graph.*`: scan REAPER into a `Scene`, edit operations (each caller wraps them in an `UndoBlock`).
- `src/ui.cpp`: the machine view and its menus. `src/gfx.*`: drawing in view pixels.
- `src/core.cpp`: actions, dump, debug hook. `src/loader.cpp`: REAPER entry; dev builds hot-reload the core and poll the `.bridge/` file bridge, release builds (`-DBAZZ_DEV=OFF`) link the core in.
- Font: Tahoma 8 pt as on Windows XP, from the 11 px bitmap strikes of Wine's LGPL Tahoma clone, turned into pixel outlines by `tools/strike2ttf.py` (`vendor/fonts/`). Pixel-exact at size 11; embedded via CMake-generated `fonts.h`.

## Workflow
- Build + hot reload into the running REAPER: `tools/reaper build-reload`. Tests: `./build/graph_tests`. New action IDs or loader changes need a REAPER restart.
- Drive REAPER: `tools/reaper ping|dump|action X|lua -` (`out(...)` in Lua returns values). UI/debug commands via ExtState `bazz/cmd`: `open`, `close`, `reset` (window back to default place), `add X Y <fx name>`, `in|out X Y CH STEREO`, `connect A B`, `insert W X Y <fx name>`, `delete M`.
- The machine view is its own window: capture it with `screencapture -l` on the id from `GetWindowID REAPER --list` ("bazz - Machines"). `tools/ui` clicks in view pixels. Both need to run outside the sandbox.
- Synthetic clicks: macOS only sends mouse moves to the key window, so click empty canvas first to focus the view. Never call ImGui functions outside a frame (e.g. from `dump`): it disturbs input.

## ReaImGui notes (0.10)
- `REAIMGUIAPI_IMPLEMENT` in exactly one file. `PushID` takes a string; `MenuItem`'s selected arg is `bool*`.
- `InputText` with `EnterReturnsTrue` only writes the buffer back on Enter; read Enter with `IsKeyPressed` instead.
- Keyboard nav is off in our context (arrow keys drive our menus); Esc is handled explicitly.
- An invalid mouse position (focus left the window) must not drive a drag.

## Code
- Minimal: no abstractions for one-off code, no comments on unchanged code, no defensive checks for impossible cases.
- Pure logic goes in `graph.*` with a doctest for every behavior change; keep the suite green before every commit.
- License MIT. Vendor only files that are compiled; check every bundled font's license.
