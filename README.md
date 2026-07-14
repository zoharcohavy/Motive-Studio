# Motive Studio

A collaborative DAW for macOS, written in C++20 on [Tracktion Engine](https://github.com/Tracktion/tracktion_engine)
(the open-source engine behind the Waveform DAW) and JUCE.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # first time only
cmake --build build                                   # every rebuild
```

The app lands at `build/MotiveStudio_artefacts/Release/Motive Studio.app`.
Requires: Xcode command-line tools, CMake, Ninja (`brew install cmake ninja`).

## The one concept that explains everything: the Edit

Tracktion Engine keeps an entire project — tracks, clips, plugins, automation
points, tempo, input assignments — in **one live tree of values** (a
`juce::ValueTree`) called the **Edit**. Every feature in this app is either:

1. **reading** the Edit (drawing the timeline, the mixer, the piano roll), or
2. **writing** the Edit (moving a clip, adding an automation point), and the
   engine reacts automatically.

This is why undo works everywhere (the tree records changes), why projects are
just XML files (the tree serialised), and why room collaboration works (tree
changes are broadcast as small binary deltas and applied on the other side).
When you add a feature, ask: *"where does this live in the Edit?"* — and you're
halfway done.

## File map

```
CMakeLists.txt              build definition; lists every source file
libs/tracktion_engine/      the engine (vendored, don't edit) + JUCE inside it
Source/
  Main.cpp                  app + window shell; nothing interesting
  MainComponent.h           one-line factory declaration
  MainComponent.cpp         THE APP. Toolbar, transport, project load/save,
                            panel docking, keyboard shortcuts, play-keys setup,
                            room wiring. Single translation unit that includes
                            everything below.
  RoomSession.h/.cpp        network collaboration: hosting/joining, discovery,
                            edit-delta sync, audio file transfer, transport relay
  ui/kit/                   OUR FORK of the engine's example UI kit:
    Utilities.h             EngineHelpers (arm tracks, toggle play, etc.)
    PluginWindow.h          floating plugin editor windows
    Components.h/.cpp       the timeline: EditComponent > TrackComponent >
                            ClipComponent, track headers (I/A/M/S), footers
                            (plugin chips + "+" insert menu), playhead.
                            Our additions are marked with "Motive" in the name
                            (e.g. MotiveTrackHeight = drag lane bottom to resize).
                            Other fork changes: double-click track name to rename
                            (blocked while recording), fixed-size header buttons,
                            virtual MIDI devices listed in the I menu, and
                            per-track automation lanes: the AUT header button
                            toggles volume/pan/wah lanes (MotiveAutoLanes stores
                            the list on the track; AutomationLaneComponent draws
                            the curve time-aligned beneath the track).
                            Timeline zoom/scroll: MainComponent owns a
                            proportional ScrollBar + zoom buttons that drive
                            EditViewState viewX1/viewX2.
  WahPlugin.h               our built-in Crybaby-style wah (a real te::Plugin,
                            registered in MainComponent's constructor); its
                            "pedal" parameter is what wah automation lanes move
  motivescript/             the MotiveScript live-coding effect (see below):
    MotiveScript.h          the language: lexer -> recursive-descent compiler ->
                            stack bytecode -> zero-allocation VM. Plain C++,
                            no juce, unit-testable on its own.
    MotiveScriptPlugin.h    the engine plugin: source in project state (saves,
                            undoes, room-syncs), 8 automatable param slots,
                            10s input history ring for sample(), lock-free-ish
                            program handoff to the audio thread
    MotiveScriptEditor.h    the per-instance editor window: code pane, live
                            recompile with inline errors, auto param sliders
    ScriptLibraryPanel.h    the "Script" workbench panel (docked above the
                            timeline): write + live-check + Save effects to
                            ~/Music/Motive Studio/Effects/*.ms; each track's
                            FX button inserts them
  DrumSounds.h              16 built-in drum sounds, synthesised from small
                            recipes to ~/Music/Motive Studio/Samples on first use
  assets/DrumImage.jpeg     the drum-kit photo (from the React app), embedded
                            via juce_add_binary_data -> BinaryData::DrumImage_jpeg
  ui/DrumConfig.h           persisted pad banks ("kit" photo view + "pads" grid):
                            name/key/sound per pad, JSON in ~/Music/Motive Studio
  ui/MixerPanel.h           mixer strips: fader, pan, mute/solo, level meters
                            (always docked above the play keyboard)
  ui/PianoRollPanel.h       MIDI note grid (click add, drag move/resize,
                            right-click delete, 1/16 snap)
  ui/ClipInspector.h        strip shown when a clip is selected: fades, speed,
                            stretch-to-tempo
  ui/PlayPanel.h            bottom piano keyboard (mouse + musical typing);
                            feeds the "Motive Keys" virtual MIDI device
```

## How the pieces talk

- **UI -> engine**: call methods on the Edit or its objects
  (`clip->setFadeIn(...)`, `param->getCurve().addPoint(...)`). Never touch
  audio buffers directly — the engine owns the audio thread.
- **engine -> UI**: components listen to the Edit's ValueTree or poll on a
  lightweight Timer (the mixer refreshes at 30 Hz, which also picks up remote
  changes from rooms).
- **The play keyboard** is not a widget wired to a synth — it's a *virtual
  MIDI input device* registered with the engine ("Motive Keys"). It appears in
  every track's **I** (input) menu exactly like a hardware interface input, so
  it records MIDI to whichever track it's assigned, simultaneously with audio
  recording on other tracks. Instruments: FourOsc synth on the "Keys" track,
  a sampler with synthesized kick/snare (written to ~/Music/Motive Studio/
  Samples on first run) on the "Drums" track.
- **Rooms** (`RoomSession`): host = TCP server (port 47815) + LAN broadcast
  (47816, like AirDrop discovery). On join, the host sends audio files then the
  full Edit XML; afterwards both sides exchange ValueTree deltas
  (`juce::ValueTreeSynchroniser`) and transport commands, and a 2s timer ships
  any new audio files. Rooms live in `/Users/Shared/Motive Studio Rooms` so
  file paths resolve identically on every Mac. The host relays everything, so
  any number of peers can join.

## Gotchas learned the hard way (don't relearn these)

- `DeviceManager::rescanMidiDeviceList()` is **asynchronous** (5 ms timer).
  After `createVirtualMidiDevice()` you cannot find the device on the same
  message-loop tick — see the retry in `setUpPlayKeys()`.
- `JUCE_DECLARE_NON_COPYABLE` deletes the copy constructor, which **also
  suppresses the implicit default constructor** — add `Foo() = default;`.
- `Components.h` `#include`s `Components.cpp` at the bottom; the whole UI must
  live in **one** translation unit (MainComponent.cpp) or you get duplicate
  symbols. New UI files: make them headers, include them from MainComponent.cpp
  after the kit.
- Panels hold references to the Edit — in `loadOrCreateEdit()` they are
  destroyed **before** the Edit and recreated after. Keep that order.
- Store `te::Clip::Ptr` (reference-counted), never raw `Clip*`, in anything
  that outlives a selection — remote peers can delete clips under you.

## Adding a feature (the pattern)

1. Find the engine API (grep `libs/tracktion_engine/modules/tracktion_engine`,
   and check `libs/tracktion_engine/examples/` for working usage).
2. If it's a panel: new header in `Source/ui/`, include it in
   MainComponent.cpp, add a toggle button, dock it in `resized()` (panels
   toggle independently and stack bottom-up; see the `dock` lambda).
   While recording, destructive controls are disabled in
   `changeListenerCallback` — add new ones to that list.
3. If it changes the project: write through the Edit with the Edit's
   UndoManager so undo and rooms keep working for free.

## MotiveScript (the language)

The workflow: toggle the **Script** panel (toolbar) to write and **Save** named
effects; every track's **FX** button lists your saved effects and inserts one
onto that track (opening its live editor window for tweaking). You can also
insert a blank "MotiveScript" from the track's + plugin menu — clicking the
plugin's name chip in the track footer opens its editor.
You write the body of the per-sample loop; the host runs it per sample.

```
param drive 1 to 25 = 6;          // becomes a slider AND an automatable param
wet = tanh(@s * drive);           // variables persist across samples (= state)
@s = lerp(@s, wet, 0.5);          // @s is the current sample; write it = effect
@s = @s + sample(@t - 0.25) * 0.3;  // sample() reads the input's past (10s max)
```

- `@s` alone = mono script: runs once per channel, each channel gets its own
  copy of every variable (so filter states stay per-channel).
- `@s.l` / `@s.r` anywhere = stereo script: runs once per frame, both channels
  visible, and bare `@s` becomes a compile error (be explicit).
- Builtins: `@t` (sec), `@n` (sample index), `@srate`, `@beat`, `@tempo`.
- Functions: sin cos tan tanh abs sqrt exp log floor ceil noise() pow min max
  lerp(a,b,t) clamp(x,lo,hi), sample(t) / sample.l(t) / sample.r(t).
- Statements: `=` `+=` `-=` `*=` `/=`, if/else, `param NAME LO to HI = DEF;`.
- Typing recompiles after 350 ms; errors show under the editor with a line
  number; audio keeps running the last good program.
- Params bind to 8 fixed engine slots (P1..P8) -> automation lanes + room sync.
  The editor shows them with their script names and real ranges.
- The language core has no juce dependency — test it headlessly by compiling
  a small main() against Source/motivescript/MotiveScript.h alone.

## Licensing

Tracktion Engine and JUCE are GPL for open/personal use. Selling this app
requires commercial licenses from Tracktion and JUCE.
