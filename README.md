# Motive Studio

## Rooms: one shared project

The host sequences canonical project revisions while peers submit identified
edits from known base revisions. Stale peers are rebased or fully synchronized.
Saved project content is shared; audio devices and buffers remain local.

Room names use LAN discovery, with direct-address join as an advanced fallback.
Optional passwords authenticate before project data or files are sent. Only one
peer may record; everyone else stays read-only through content-addressed,
SHA-256-verified file transfer, the canonical take revision, and peer
acknowledgements. Independent concurrent edits—including different DrumLab
steps—converge through the host; revision gaps recover from full canonical
state. Rooms never stream live peer audio.

A lean, JUCE-based DAW: the production rewrite of Motive Studio onto **Motive
Core** (project model + real-time audio graph, no Tracktion Engine). The full
architecture guide is [MOTIVE-CORE.md](MOTIVE-CORE.md); the script language is
documented in [MotiveScript-Guide.md](MotiveScript-Guide.md); the completed
code audit lives in [AUDIT.md](AUDIT.md).

## What's inside

- **Timeline** (trim/move/crop tools, automation lanes, click-to-seek),
  **piano roll**, **mixer**, and an always-there **play keyboard**
  (Keys / Drums / Pads modes, computer-key playable).
- **SoundCreate** — the knobs synth designer: two oscillators plus script
  wavetables, filter, envelopes, LFOs and a drag-a-source macro mod-matrix,
  with a *thru* chip that loops any audio file through the sound being
  designed (audition-only, never exported).
- **NodeView** — a node canvas over each track's rack: wire effects and
  synths freely, group nodes into named frames (⌘G), drive node params from
  synth macros, pull live audio from DrumLab or SoundCreate into the graph,
  and monitor or record any of those from any track via the "Node Output" /
  "DrumLab Output" / "SoundCreate Output" track inputs.
- **MotiveScript** — a built-in per-sample DSP language with
  live-recompiling editors, a factory effects library, and one-click export
  of a whole node graph into a single script effect.
- **DrumLab** — a 16-pad drum synth (per-pad macros, choke groups, sample
  layers) with a step grid (swing, humanize, flam/roll, Euclid fills,
  per-step probability) and a **loop recorder**: every pass around the loop
  is a separate take, reviewed as chips and applied to the grid at an
  adjustable tighten strength — played feel preserved — with an
  audition-only backing track to play along to.
- **VST3 hosting** (scan, vendor editors, automatable mirrored params),
  **offline render / bounce in place**, **Rooms** LAN collaboration
  (macOS), and whole-workspace session restore baked into every project.


## Machine-specific paths (read this first on a new computer)

There are **no machine-specific paths anywhere in the tree** — nothing
references a username or a folder outside this one. The only absolute paths
you might find are in *generated local files*, neither of which is code:

| Where | What it is | What to do on your machine |
|---|---|---|
| `build/` (CMakeCache.txt, build.ninja, …) | absolute paths baked in by CMake when the build tree was generated on the original machine | delete the `build/` folder and re-run the configure step below — it regenerates with your paths |
| `.claude/settings.local.json` | a local dev-tool permissions file | ignore or delete; not part of the app |

Two paths are intentional and identical for every user: Rooms
collaboration uses the shared-documents folder — the same absolute path on
every machine of a given platform (`/Users/Shared/Motive Revi Rooms` on
macOS, `C:\Users\Public\Documents\Motive Revi Rooms` on Windows) so shared
project file references resolve on all peers — and projects are saved to
`~/Documents/Motive Revi Projects`.

## Build & run — macOS

```bash
cd "motive revi"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open "build/MotiveRevi_artefacts/Release/Motive Revi.app"
```

On first launch macOS will ask for microphone access (needed to record audio
input). If you decline, the app still runs — playback, MIDI recording and the
transport all work; grant access later via System Settings > Privacy &
Security > Microphone, then reopen the audio device from File > Audio
Devices (or restart the app) to record audio.

## Audio & MIDI settings

**File > Audio Devices** opens the device panel:

- **Output**: any CoreAudio output (speakers, headphones, interface) and which
  of its channels to use.
- **Input**: any CoreAudio input (built-in/USB mic, interface) and its
  channels — what you enable here is what track input menus offer.
- **MIDI inputs**: tick the keyboards/controllers you want live. Newly
  plugged-in devices are enabled automatically the first time they appear.
- Sample rate and buffer size are managed for you (device-native rate, ~256
  sample buffer ≈ 5 ms). If you must change them, they're behind the
  **Advanced** button, and your choice is respected from then on.

Preferences persist in `~/Library/Motive Revi/` on macOS (`%APPDATA%` on
Windows) and restore on launch; if the saved device is
missing, the app falls back to the system default (and to output-only if
input access is denied) rather than failing.

Each track's **I** (input) and **A** (arm) menus list live audio channels
(mono and stereo pairs), *Motive Keys / SoundCreate* (the on-screen keyboard
and sound-design flow), each enabled MIDI device, and — once the matching
panel has a host track — the internal tap inputs: *Node output (NodeView)*,
*DrumLab output (pads)* and *SoundCreate output (knobs)*, which monitor or
record that source track's pre-fader chain output. Armed tracks record their
selected input with sample-exact bounds; *Input Monitoring* (in the I menu)
passes the input to the output without recording — for tap inputs it works
without arming, while live device audio stays arm-gated to avoid feedback.
The header shows a live input level bar while armed and a red dot (with a
tooltip) when the selected input is no longer connected.

## Build & run — Windows (PC)

The code is portable C++/JUCE and builds with the Visual Studio generator:

```powershell
cd "motive revi"
cmake -B build
cmake --build build --config Release
& "build/MotiveRevi_artefacts/Release/Motive Revi.exe"
```

If the folder was copied from another machine, delete any `build/` folder it
came with first — its CMake cache holds the original machine's paths.

Caveats on Windows:

- Development and testing happen on macOS; the Windows build is expected to
  work via JUCE but is not routinely exercised — please report breakage.
  A dedicated portability review (see [AUDIT.md](AUDIT.md)) swept the whole
  tree for MSVC/Windows hazards and fixed the only two found (both in
  RoomSession); performance-critical buffer operations use JUCE's
  `FloatVectorOperations`, which is SIMD on Windows too (SSE intrinsics,
  where macOS uses Accelerate/vDSP).
- Audio uses **WASAPI/DirectSound** by default (JUCE's Windows backends). For
  low-latency **ASIO**, download Steinberg's ASIO SDK and configure with
  `-DMOTIVE_ASIO_SDK="C:/path/to/asiosdk"`; the ASIO device type then appears
  in the device panel **only when an ASIO driver is installed**.
- **Rooms** (LAN collaboration) is supported Mac↔Mac only: synced projects
  carry platform-absolute file paths, so peers must be on the same platform,
  and the Windows side has never been exercised. (The rooms folder itself
  now resolves per platform — `C:\Users\Public\Documents\Motive Revi Rooms`
  on Windows.)

## Tests

```bash
cmake --build build                          # builds the app and motive_tests
ctest --test-dir build --output-on-failure   # runs the suite (from the build dir)

# or run the binary directly:
./build/motive_tests_artefacts/Release/motive_tests
./build/motive_tests_artefacts/Release/motive_tests --bench   # perf numbers
```

Test file fixtures (generated WAVs, render outputs) are written to a unique
`motive_tests_<id>` directory that is deleted when the run ends. The location
is chosen portably — sandboxed/restricted environments that forbid writing to
the user cache or temp folder are fine:

1. `$MOTIVE_TEST_TEMP_DIR`, if set (absolute, or relative to the working directory);
2. otherwise the working directory — CTest runs the suite from `build/`.

If fixture creation fails, the log prints a `FIXTURE ERROR:` line with the
exact path and reason. When running the binary directly, run it from a
writable directory or set `MOTIVE_TEST_TEMP_DIR`.

The Rooms protocol cases open loopback TCP/UDP listeners. In a restricted
sandbox, grant local-network/listener permission for `ctest`; a listener-bind
failure in a fully network-blocked sandbox is environmental, not a skipped
test. The current suite contains 40 cases, including forced revision-gap
recovery and bounded eviction of a connected peer that never acknowledges a
recording commit.

## Prerequisites

- **CMake ≥ 3.22** and a C++20 compiler
  - macOS: Xcode command-line tools + [Ninja](https://ninja-build.org) (`brew install ninja`)
  - Windows: Visual Studio 2022 (Desktop C++ workload) — CMake and Ninja ship with it
That's it — the tree is **fully self-contained**. JUCE is vendored at
`libs/juce` and the icon/image assets live in `assets/`; no other checkout,
download, or environment variable is needed. Copy the `motive revi` folder
anywhere (USB stick, another Mac, a Windows PC) and build.
