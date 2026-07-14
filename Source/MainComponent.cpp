#include <tracktion_engine/tracktion_engine.h>

using namespace juce;

// Our fork of Tracktion's example UI kit: EngineHelpers, plugin windows, and
// the EditComponent timeline (tracks, clips, waveforms, playhead, plugin menu).
// Forked into Source/ui/kit so we can extend it (per-track heights, etc.).
#include "ui/kit/Utilities.h"
#include "ui/kit/PluginWindow.h"
#include "ui/kit/Components.h"

#include "MainComponent.h"
#include "RoomSession.h"

#include "BinaryData.h"
#include "DrumSounds.h"
#include "ui/DrumConfig.h"
#include "ui/MixerPanel.h"
#include "ui/PianoRollPanel.h"
#include "ui/ClipInspector.h"
#include "ui/PlayPanel.h"
#include "motivescript/ScriptLibraryPanel.h"

// a slim horizontal handle between panels — drag it to resize the panel
class HeightGrip : public Component
{
public:
    std::function<int()> getValue;
    std::function<void (int)> setValue;
    int direction = 1;   // +1: dragging down grows the panel, -1: dragging up

    HeightGrip()    { setMouseCursor (MouseCursor::UpDownResizeCursor); }

    void mouseDown (const MouseEvent&) override         { start = getValue(); }
    void mouseDrag (const MouseEvent& e) override       { setValue (start + direction * e.getDistanceFromDragStartY()); }

    void paint (Graphics& g) override
    {
        g.setColour (Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (getLocalBounds().withSizeKeepingCentre (48, 3).toFloat(), 1.5f);
    }

private:
    int start = 0;
};

class MotiveStudioComponent : public Component,
                              private ChangeListener,
                              private ScrollBar::Listener,
                              private Timer
{
public:
    MotiveStudioComponent()
    {
        // our built-in plugins must be registered before any edit loads one
        engine.getPluginManager().createBuiltInType<WahPlugin>();
        engine.getPluginManager().createBuiltInType<MotiveScriptPlugin>();

        for (auto* b : { &fileButton, &playButton, &stopButton,
                         &recordButton, &rtzButton, &addTrackButton, &deleteButton,
                         &undoButton, &redoButton,
                         &midiClipButton, &loopInButton, &loopOutButton })
            addAndMakeVisible (b);

        playButton.setButtonText (String::fromUTF8 ("▶"));                       // play triangle
        stopButton.setButtonText (String::fromUTF8 ("●"));                       // stop circle
        stopButton.setColour (TextButton::textColourOffId, Colour (0xffe53935));
        rtzButton.setButtonText (String::fromUTF8 ("|◀"));                       // return to zero

        // latching buttons: loop/punch state and the optional panels
        // (the mixer is always open; automation lives under each track now)
        for (auto* b : { &loopButton, &punchButton, &keysButton, &pianoRollButton, &scriptButton })
        {
            b->setClickingTogglesState (true);
            b->setColour (TextButton::buttonOnColourId, Colour (0xff1565c0));
            addAndMakeVisible (b);
        }

        scriptButton.onClick = [this] { resized(); };
        addAndMakeVisible (scriptPanel);

        // drag handles that let every docked panel be resized out of the way
        auto setUpGrip = [this] (HeightGrip& grip, int& value, int direction, int minH, int maxH)
        {
            grip.direction = direction;
            grip.getValue = [&value] { return value; };
            grip.setValue = [this, &value, minH, maxH] (int v)
            {
                value = jlimit (minH, maxH, v);
                resized();
            };
            addAndMakeVisible (grip);
        };

        setUpGrip (scriptGrip, scriptPanelHeight, 1, 120, 520);
        setUpGrip (playGrip, playPanelHeight, -1, 70, 420);
        setUpGrip (mixerGrip, mixerHeight, -1, 90, 400);
        setUpGrip (pianoGrip, pianoRollHeight, -1, 120, 460);

        keysButton.setToggleState (true, dontSendNotification);
        keysButton.onClick = [this] { resized(); };

        midiClipButton.onClick  = [this] { createMidiClip(); };

        addAndMakeVisible (regionButton);
        regionButton.onClick = [this]
        {
            regionExpanded = ! regionExpanded;
            updateRegionButton();
            resized();
        };
        updateRegionButton();

        loopInButton.onClick    = [this] { loopIn = edit->getTransport().getPosition(); applyLoopRange(); };
        loopOutButton.onClick   = [this] { loopOut = edit->getTransport().getPosition(); applyLoopRange(); };
        loopButton.onClick      = [this] { edit->getTransport().looping = loopButton.getToggleState(); updateRegionButton(); };
        punchButton.onClick     = [this] { edit->recordingPunchInOut = punchButton.getToggleState(); updateRegionButton(); };

        pianoRollButton.onClick = [this]
        {
            if (pianoRollButton.getToggleState() && pianoRollPanel != nullptr)
                if (auto midiClip = dynamic_cast<te::MidiClip*> (selectionManager.getSelectedObject (0)))
                    pianoRollPanel->setClip (midiClip);

            resized();
        };

        // timeline navigation: zoom buttons, nudge arrows, proportional scrollbar
        scrollLeftButton.setButtonText (String::fromUTF8 ("◀"));
        scrollRightButton.setButtonText (String::fromUTF8 ("▶"));

        for (auto* b : { &zoomOutButton, &scrollLeftButton, &scrollRightButton, &zoomInButton })
        {
            b->setRepeatSpeed (300, 80);   // hold to keep moving
            addAndMakeVisible (b);
        }

        zoomInButton.onClick     = [this] { zoomTimeline (0.667); };
        zoomOutButton.onClick    = [this] { zoomTimeline (1.5); };
        scrollLeftButton.onClick  = [this] { nudgeTimeline (-0.25); };
        scrollRightButton.onClick = [this] { nudgeTimeline (0.25); };

        timelineScroll.setAutoHide (false);
        timelineScroll.addListener (this);
        addAndMakeVisible (timelineScroll);

        addAndMakeVisible (playPanel);
        playPanel.onMidi = [this] (const MidiMessage& m) { sendKeysMidi (m); };
        playPanel.onModeChange = [this] (int mode)
        {
            setKeysMode (mode);
            playPanelHeight = playPanel.getPreferredHeight();
            resized();
        };
        playPanel.onBankChanged = [this] (int mode) { setKeysMode (mode); };

        addAndMakeVisible (roomStatusLabel);
        roomStatusLabel.setColour (Label::textColourId, Colour (0xff2ecc71));
        roomStatusLabel.setJustificationType (Justification::centredLeft);

        roomSession.onRemoteEditReady = [this] (File editFile)
        {
            loadOrCreateEdit (editFile, true);
            roomSession.attachToEdit (edit.get());
            updateRoomStatus();
        };

        roomSession.onRemoteTransport = [this] (int command, double positionSeconds)
        {
            applyRemoteTransportCommand (command, positionSeconds);
        };

        roomSession.onStatusChanged = [this] { updateRoomStatus(); };

        setWantsKeyboardFocus (true);

        addAndMakeVisible (clickButton);
        addAndMakeVisible (tempoSlider);
        addAndMakeVisible (timeDisplay);
        addAndMakeVisible (projectLabel);

        tempoSlider.setRange (40.0, 240.0, 0.1);
        tempoSlider.setValue (120.0, dontSendNotification);
        tempoSlider.setTextBoxStyle (Slider::TextBoxLeft, false, 55, 22);
        tempoSlider.setTextValueSuffix (" bpm");

        timeDisplay.setJustificationType (Justification::centredRight);
        timeDisplay.setFont (FontOptions (20.0f, Font::bold));
        projectLabel.setJustificationType (Justification::centredLeft);
        projectLabel.setColour (Label::textColourId, Colours::white.withAlpha (0.6f));

        fileButton.onClick     = [this] { showFileMenu(); };
        playButton.onClick     = [this] { EngineHelpers::togglePlay (*edit); };
        stopButton.onClick     = [this] { stopPlayback(); };
        recordButton.onClick   = [this] { toggleRecord(); };
        rtzButton.onClick      = [this] { edit->getTransport().setPosition (te::TimePosition()); };
        addTrackButton.onClick = [this] { edit->ensureNumberOfAudioTracks ((int) te::getAudioTracks (*edit).size() + 1); };
        deleteButton.onClick   = [this] { deleteSelected(); };
        undoButton.onClick     = [this] { edit->getUndoManager().undo(); };
        redoButton.onClick     = [this] { edit->getUndoManager().redo(); };

        clickButton.onClick = [this] { edit->clickTrackEnabled = clickButton.getToggleState(); };

        tempoSlider.onValueChange = [this]
        {
            if (auto tempo = edit->tempoSequence.getTempo (0))
                tempo->setBpm (tempoSlider.getValue());
        };

        selectionManager.addChangeListener (this);

        auto projectDir = File::getSpecialLocation (File::userDocumentsDirectory)
                              .getChildFile ("Motive Studio Projects");
        projectDir.createDirectory();

        loadOrCreateEdit (projectDir.getNonexistentChildFile ("Untitled", ".tracktionedit", false));

        startTimerHz (15);
        setSize (1280, 720);
    }

    ~MotiveStudioComponent() override
    {
        roomSession.leaveRoom();

        if (edit != nullptr)
            te::EditFileOperations (*edit).save (true, true, false);

        engine.getTemporaryFileManager().getTempDirectory().deleteRecursively();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff16181c));

        // raised "wells" behind each toolbar group make the sections obvious
        for (const auto& group : toolbarGroups)
        {
            g.setColour (Colour (0xff22262d));
            g.fillRoundedRectangle (group.toFloat(), 6.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (group.toFloat(), 6.0f, 1.0f);
        }
    }

    void resized() override
    {
        toolbarGroups.clear();
        auto r = getLocalBounds();

        auto place = [] (Rectangle<int>& bar, Component& c, int w)
        {
            c.setBounds (bar.removeFromLeft (w).reduced (3, 1));
        };

        auto beginGroup = [] (Rectangle<int>& bar) { return bar.getX(); };

        auto endGroup = [this] (Rectangle<int>& bar, int startX)
        {
            toolbarGroups.push_back ({ startX, bar.getY(), bar.getX() - startX, bar.getHeight() });
            bar.removeFromLeft (12);
        };

        // row 1: [file] [track & edit tools] [output/settings]
        auto bar = r.removeFromTop (36).reduced (6, 4);

        int gx = beginGroup (bar);
        place (bar, fileButton, 58);
        endGroup (bar, gx);

        gx = beginGroup (bar);
        place (bar, addTrackButton, 80);
        place (bar, midiClipButton, 66);
        place (bar, deleteButton, 62);
        place (bar, undoButton, 54);
        place (bar, redoButton, 54);
        endGroup (bar, gx);

        // row 2: [transport] [loop/punch] [tempo] [panels]      time on the right
        bar = r.removeFromTop (36).reduced (6, 4);
        timeDisplay.setBounds (bar.removeFromRight (150));

        gx = beginGroup (bar);
        place (bar, playButton, 54);
        place (bar, stopButton, 46);
        place (bar, recordButton, 70);
        place (bar, rtzButton, 46);
        endGroup (bar, gx);

        gx = beginGroup (bar);
        place (bar, regionButton, 78);

        for (auto* b : { &loopInButton, &loopOutButton, &loopButton, &punchButton })
            b->setVisible (regionExpanded);

        if (regionExpanded)
        {
            place (bar, loopInButton, 42);
            place (bar, loopOutButton, 46);
            place (bar, loopButton, 52);
            place (bar, punchButton, 58);
        }
        endGroup (bar, gx);

        gx = beginGroup (bar);
        place (bar, tempoSlider, 140);
        place (bar, clickButton, 58);
        endGroup (bar, gx);

        gx = beginGroup (bar);
        place (bar, pianoRollButton, 52);
        place (bar, keysButton, 50);
        place (bar, scriptButton, 54);
        endGroup (bar, gx);

        auto info = r.removeFromTop (22).reduced (8, 1);
        projectLabel.setBounds (info.removeFromLeft (jmin (280, info.getWidth() / 2)));
        roomStatusLabel.setBounds (info);

        // the Script workbench docks at the top, directly above the tracks
        if (scriptButton.getToggleState())
        {
            scriptPanel.setBounds (r.removeFromTop (scriptPanelHeight));
            scriptGrip.setBounds (r.removeFromTop (7));
        }
        else
        {
            scriptPanel.setBounds ({});
            scriptGrip.setBounds ({});
        }

        // bottom-up stack: play keyboard, mixer (always open), piano roll,
        // inspector. every panel has a grip on its inner edge to resize it.
        auto dockBottom = [&r] (Component* panel, HeightGrip& grip, bool open, int height)
        {
            if (panel == nullptr)
                return;

            panel->setBounds (open ? r.removeFromBottom (height) : Rectangle<int>());
            grip.setBounds (open ? r.removeFromBottom (7) : Rectangle<int>());
        };

        dockBottom (&playPanel,           playGrip,  keysButton.getToggleState(),      playPanelHeight);
        dockBottom (mixerPanel.get(),     mixerGrip, true,                             mixerHeight);
        dockBottom (pianoRollPanel.get(), pianoGrip, pianoRollButton.getToggleState(), pianoRollHeight);

        if (clipInspector != nullptr)
            clipInspector->setBounds (inspectorVisible ? r.removeFromBottom (30) : Rectangle<int>());

        // zoom/scroll strip hugs the bottom of the timeline, controls together on the left
        auto scrollStrip = r.removeFromBottom (18).reduced (4, 0);
        zoomOutButton.setBounds (scrollStrip.removeFromLeft (26));
        zoomInButton.setBounds (scrollStrip.removeFromLeft (26));
        scrollLeftButton.setBounds (scrollStrip.removeFromLeft (26));
        scrollRightButton.setBounds (scrollStrip.removeFromLeft (26));
        timelineScroll.setBounds (scrollStrip.reduced (4, 0));

        if (editComponent != nullptr)
            editComponent->setBounds (r.reduced (4));
    }

    //==============================================================================
    // Timeline zoom + scroll over a fixed 10-minute timeline. The zoom (view
    // width) is stored explicitly and only the zoom buttons change it, so
    // scrolling can never accidentally rescale the view (which is what made
    // envelope points look like they were bunching up).
    void setTimelineView (double startSeconds)
    {
        if (editComponent == nullptr)
            return;

        const double start = jlimit (0.0, timelineLengthSeconds - viewWidthSeconds, startSeconds);

        auto& evs = editComponent->getEditViewState();
        evs.viewX1 = te::TimePosition::fromSeconds (start);
        evs.viewX2 = te::TimePosition::fromSeconds (start + viewWidthSeconds);
        updateScrollBar();
    }

    void zoomTimeline (double factor)
    {
        if (editComponent == nullptr)
            return;

        const double oldStart = editComponent->getEditViewState().viewX1.get().inSeconds();
        const double centre = oldStart + viewWidthSeconds * 0.5;

        viewWidthSeconds = jlimit (5.0, timelineLengthSeconds, viewWidthSeconds * factor);
        setTimelineView (centre - viewWidthSeconds * 0.5);
    }

    void nudgeTimeline (double proportionOfView)
    {
        if (editComponent == nullptr)
            return;

        setTimelineView (editComponent->getEditViewState().viewX1.get().inSeconds()
                         + viewWidthSeconds * proportionOfView);
    }

    void updateScrollBar()
    {
        if (editComponent == nullptr)
            return;

        // never fight the user mid-drag
        if (timelineScroll.isMouseButtonDown())
            return;

        timelineScroll.setRangeLimits ({ 0.0, timelineLengthSeconds }, dontSendNotification);
        timelineScroll.setCurrentRange ({ editComponent->getEditViewState().viewX1.get().inSeconds(),
                                          viewWidthSeconds },
                                        dontSendNotification);
    }

    void scrollBarMoved (ScrollBar*, double newRangeStart) override
    {
        if (editComponent == nullptr)
            return;

        // slide the window; the width (zoom) is ours and never changes here
        const double start = jlimit (0.0, timelineLengthSeconds - viewWidthSeconds, newRangeStart);

        auto& evs = editComponent->getEditViewState();
        evs.viewX1 = te::TimePosition::fromSeconds (start);
        evs.viewX2 = te::TimePosition::fromSeconds (start + viewWidthSeconds);
    }

private:
    te::Engine engine { "Motive Studio", std::make_unique<ExtendedUIBehaviour>(), nullptr };
    te::SelectionManager selectionManager { engine };
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<EditComponent> editComponent;
    File currentEditFile;

    TextButton fileButton { "File" },
               playButton, stopButton, recordButton { "Record" }, rtzButton,
               addTrackButton { "+ Track" }, deleteButton { "Delete" },
               undoButton { "Undo" }, redoButton { "Redo" };
    std::vector<Rectangle<int>> toolbarGroups;
    ToggleButton clickButton { "Click" };
    Slider tempoSlider;
    Label timeDisplay { {}, "0:00.0" }, projectLabel;

    RoomSession roomSession { engine };
    Label roomStatusLabel;
    int lastBroadcastTransport = 0;

    // creation tools & performance
    TextButton midiClipButton { "+ MIDI" };
    TextButton regionButton;   // collapses In/Out/Loop/Punch into one toolbar item
    bool regionExpanded = false;
    TextButton loopInButton { "In" }, loopOutButton { "Out" },
               loopButton { "Loop" }, punchButton { "Punch" };
    TextButton pianoRollButton { "Piano" }, keysButton { "Keys" }, scriptButton { "Script" };
    ScriptLibraryPanel scriptPanel;

    // panel heights, adjustable by dragging the grips between panels
    int scriptPanelHeight = 240, playPanelHeight = 112, mixerHeight = 170, pianoRollHeight = 235;
    HeightGrip scriptGrip, playGrip, mixerGrip, pianoGrip;
    TextButton zoomInButton { "+" }, zoomOutButton { "-" },
               scrollLeftButton, scrollRightButton;
    ScrollBar timelineScroll { false };   // horizontal, REAPER-style proportional thumb

    // the timeline is a fixed 10 minutes; the view starts showing 1 minute of it.
    // zoom buttons are the ONLY thing that changes viewWidthSeconds — scrolling
    // and nudging just slide the same-sized window along the timeline.
    static constexpr double timelineLengthSeconds = 600.0;
    double viewWidthSeconds = 60.0;

    te::TimePosition loopIn, loopOut;

    std::unique_ptr<MixerPanel> mixerPanel;
    std::unique_ptr<PianoRollPanel> pianoRollPanel;
    std::unique_ptr<ClipInspector> clipInspector;
    bool inspectorVisible = false;

    PlayPanel playPanel;
    int keysSetupRetries = 0;
    te::VirtualMidiInputDevice* keysDevice = nullptr;
    te::MPESourceID keysMidiSourceID = te::createUniqueMPESourceID();

    static constexpr const char* keysDeviceName = "Motive Keys";

    //==============================================================================
    void loadOrCreateEdit (File editFile, bool triggeredByRoom = false)
    {
        if (! triggeredByRoom && roomSession.isInRoom())
            roomSession.leaveRoom();

        selectionManager.deselectAll();

        // panels reference the edit — tear down before it goes away
        clipInspector = nullptr;
        mixerPanel = nullptr;
        pianoRollPanel = nullptr;
        inspectorVisible = false;
        keysDevice = nullptr;
        editComponent = nullptr;

        if (editFile.existsAsFile())
            edit = te::loadEditFromFile (engine, editFile);
        else
            edit = te::createEmptyEdit (engine, editFile);

        currentEditFile = editFile;
        edit->playInStopEnabled = true;
        edit->getTransport().addChangeListener (this);

        setUpInputs (triggeredByRoom);

        // don't grow a synced room project — the tree must match the host's exactly
        if (! triggeredByRoom)
            edit->ensureNumberOfAudioTracks (jmax (4, (int) te::getAudioTracks (*edit).size()));

        if (auto tempo = edit->tempoSequence.getTempo (0))
            tempoSlider.setValue (tempo->getBpm(), dontSendNotification);
        clickButton.setToggleState (edit->clickTrackEnabled, dontSendNotification);

        te::EditFileOperations (*edit).save (true, true, false);
        projectLabel.setText (editFile.getFileNameWithoutExtension(), dontSendNotification);

        editComponent = std::make_unique<EditComponent> (*edit, selectionManager);
        editComponent->getEditViewState().drawWaveforms = true;
        editComponent->getEditViewState().showMidiDevices = true;   // Motive Keys in the I menu

        // double-clicking a MIDI clip on the timeline opens it in the piano roll
        editComponent->getEditViewState().onOpenMidiClip = [this] (te::MidiClip& midiClip)
        {
            pianoRollButton.setToggleState (true, dontSendNotification);
            pianoRollPanel->setClip (&midiClip);
            resized();
        };

        addAndMakeVisible (*editComponent);

        // fresh, sane view every load: 1 minute visible of the 10-minute timeline
        viewWidthSeconds = 60.0;
        editComponent->getEditViewState().viewX1 = te::TimePosition();
        editComponent->getEditViewState().viewX2 = te::TimePosition::fromSeconds (60.0);

        mixerPanel = std::make_unique<MixerPanel> (*edit);
        addAndMakeVisible (*mixerPanel);

        pianoRollPanel = std::make_unique<PianoRollPanel>();
        addAndMakeVisible (*pianoRollPanel);

        clipInspector = std::make_unique<ClipInspector>();
        addAndMakeVisible (*clipInspector);

        // loop/punch state from the loaded project
        const auto loopRange = edit->getTransport().getLoopRange();
        loopIn = loopRange.getStart();
        loopOut = loopRange.getEnd();
        loopButton.setToggleState (edit->getTransport().looping, dontSendNotification);
        punchButton.setToggleState (edit->recordingPunchInOut, dontSendNotification);
        updateRegionButton();

        setUpPlayKeys (triggeredByRoom);
        updateScrollBar();
        resized();
    }

    //==============================================================================
    // The on-screen keyboard is a real engine input device ("Motive Keys"), so it
    // records to whichever track it's assigned to — independently of other inputs.
    void setUpPlayKeys (bool joinedRoom)
    {
        auto& dm = engine.getDeviceManager();

        if (dynamic_cast<te::VirtualMidiInputDevice*> (dm.findInputDeviceWithName (keysDeviceName)) == nullptr)
            dm.createVirtualMidiDevice (keysDeviceName);

        keysDevice = dynamic_cast<te::VirtualMidiInputDevice*> (dm.findInputDeviceWithName (keysDeviceName));

        if (keysDevice == nullptr)
        {
            // device creation applies on the next message-loop tick — retry shortly
            if (++keysSetupRetries <= 10)
                Timer::callAfterDelay (200, [safe = SafePointer<MotiveStudioComponent> (this), joinedRoom]
                {
                    if (safe != nullptr)
                        safe->setUpPlayKeys (joinedRoom);
                });
            return;
        }

        keysSetupRetries = 0;

        keysDevice->setEnabled (true);
        keysDevice->setMonitorMode (te::InputDevice::MonitorMode::on);

        edit->getTransport().ensureContextAllocated();

        // in a room, don't silently add tracks to a shared project — the user can
        // assign "Motive Keys" to any track with the track's I button instead
        if (! joinedRoom)
            setKeysMode (playPanel.getMode());
    }

    void sendKeysMidi (const MidiMessage& message)
    {
        if (keysDevice != nullptr)
            keysDevice->handleIncomingMidiMessage (message, keysMidiSourceID);
    }

    // Route the play panel's virtual MIDI device to the right track for the
    // chosen mode: Keys -> synth track, Drums/Pads -> a sampler built from that
    // mode's bank. Also called when Customize saves, to rebuild the sounds.
    void setKeysMode (int mode)
    {
        if (edit == nullptr || keysDevice == nullptr)
            return;

        te::AudioTrack* track = nullptr;

        if (mode == PlayPanel::keysMode)
        {
            track = getOrCreateSynthTrack ("Keys");
        }
        else
        {
            track = getOrCreateTrackNamed (mode == PlayPanel::drumsMode ? "Drums" : "Pads");

            if (track != nullptr)
                rebuildSampler (*track, playPanel.getBank (mode));
        }

        if (track == nullptr)
            return;

        for (auto instance : edit->getAllInputDevices())
            if (&instance->getInputDevice() == keysDevice)
                [[maybe_unused]] auto result = instance->setTarget (track->itemID, true, &edit->getUndoManager(), 0);
    }

    te::AudioTrack* getOrCreateTrackNamed (const String& name)
    {
        for (auto t : te::getAudioTracks (*edit))
            if (t->getName() == name)
                return t;

        edit->ensureNumberOfAudioTracks ((int) te::getAudioTracks (*edit).size() + 1);
        auto track = te::getAudioTracks (*edit).getLast();

        if (track != nullptr)
            track->setName (name);

        return track;
    }

    te::AudioTrack* getOrCreateSynthTrack (const String& name)
    {
        auto track = getOrCreateTrackNamed (name);

        if (track != nullptr)
        {
            for (auto p : track->pluginList)
                if (dynamic_cast<te::FourOscPlugin*> (p) != nullptr)
                    return track;

            if (auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
                track->pluginList.insertPlugin (plugin, 0, nullptr);
        }

        return track;
    }

    // make the track's sampler match a drum bank: one sound per pad, each on
    // its own MIDI note, open-ended so short key taps still ring out fully
    void rebuildSampler (te::AudioTrack& track, const DrumBank& bank)
    {
        te::SamplerPlugin* sampler = nullptr;

        for (auto p : track.pluginList)
            if ((sampler = dynamic_cast<te::SamplerPlugin*> (p)) != nullptr)
                break;

        if (sampler == nullptr)
        {
            if (auto plugin = edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {}))
            {
                track.pluginList.insertPlugin (plugin, 0, nullptr);
                sampler = dynamic_cast<te::SamplerPlugin*> (plugin.get());
            }
        }

        if (sampler == nullptr)
            return;

        while (sampler->getNumSounds() > 0)
            sampler->removeSound (0);

        for (int i = 0; i < DrumBank::numPads; ++i)
        {
            const auto& pad = bank.pads[(size_t) i];
            sampler->addSound (pad.resolveFile().getFullPathName(), pad.name, 0.0, 0.0, 0.0f);
            sampler->setSoundParams (i, pad.note, pad.note, pad.note);
            sampler->setSoundOpenEnded (i, true);
        }
    }

    void createMidiClip()
    {
        auto tracks = te::getAudioTracks (*edit);
        if (tracks.isEmpty())
            return;

        // prefer the selected track (or the selected clip's track)
        te::AudioTrack* track = dynamic_cast<te::AudioTrack*> (selectionManager.getSelectedObject (0));

        if (track == nullptr)
            if (auto clip = dynamic_cast<te::Clip*> (selectionManager.getSelectedObject (0)))
                track = dynamic_cast<te::AudioTrack*> (clip->getTrack());

        if (track == nullptr)
            track = tracks.getFirst();

        auto& ts = edit->tempoSequence;
        const auto startBeat = ts.toBeats (edit->getTransport().getPosition());
        const te::TimeRange range (ts.toTime (startBeat),
                                   ts.toTime (startBeat + te::BeatDuration::fromBeats (16.0)));

        if (auto midiClip = track->insertMIDIClip (range, &selectionManager))
        {
            getOrCreateInstrumentTrackFor (*track);
            pianoRollButton.setToggleState (true, dontSendNotification);
            pianoRollPanel->setClip (midiClip.get());
            resized();
        }
    }

    void getOrCreateInstrumentTrackFor (te::AudioTrack& track)
    {
        for (auto p : track.pluginList)
            if (dynamic_cast<te::FourOscPlugin*> (p) != nullptr || dynamic_cast<te::SamplerPlugin*> (p) != nullptr)
                return;

        if (auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
            track.pluginList.insertPlugin (plugin, 0, nullptr);
    }

    void updateRegionButton()
    {
        regionButton.setButtonText (String ("Region ") + String::fromUTF8 (regionExpanded ? "▾" : "▸"));

        // glow while loop or punch is engaged, so a collapsed menu can't hide it
        const bool active = loopButton.getToggleState() || punchButton.getToggleState();
        regionButton.setColour (TextButton::buttonColourId,
                                active ? Colour (0xff1565c0)
                                       : getLookAndFeel().findColour (TextButton::buttonColourId));
    }

    void applyLoopRange()
    {
        // clicking In before any Out gives an instantly visible 8-second region
        if (loopOut <= loopIn)
            loopOut = loopIn + te::TimeDuration::fromSeconds (8.0);

        edit->getTransport().setLoopRange ({ loopIn, loopOut });
    }

    void setUpInputs (bool skipAutoAssign = false)
    {
        auto& dm = engine.getDeviceManager();

        // If no audio inputs exist, the very first launch likely opened the device
        // before macOS granted mic permission (saving an input-less config that's
        // restored every launch). Now that permission exists, reopen with inputs.
        if (dm.getNumWaveInDevices() == 0)
        {
            dm.deviceManager.initialiseWithDefaultDevices (2, 2);
            dm.rescanWaveDeviceList();
            dm.dispatchPendingUpdates();
        }

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        {
            if (auto wip = dm.getWaveInDevice (i))
            {
                wip->setStereoPair (false);
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (true);
            }
        }

        edit->getTransport().ensureContextAllocated();

        // give each hardware input its own track, ready to arm from the track header
        // (skipped when joining a room, to avoid rewiring the host's assignments —
        //  use each track's I button to pick your input instead)
        if (! skipAutoAssign)
        {
            int trackNum = 0;
            for (auto instance : edit->getAllInputDevices())
            {
                if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                {
                    if (auto t = EngineHelpers::getOrInsertAudioTrackAt (*edit, trackNum))
                    {
                        [[maybe_unused]] auto result = instance->setTarget (t->itemID, true, &edit->getUndoManager(), 0);
                        ++trackNum;
                    }
                }
            }
        }

        edit->restartPlayback();
    }

    //==============================================================================
    void stopPlayback()
    {
        auto& transport = edit->getTransport();
        const bool wasRecording = transport.isRecording();
        transport.stop (false, false);

        if (wasRecording)
            te::EditFileOperations (*edit).save (true, true, false);
    }

    void toggleRecord()
    {
        const bool wasRecording = edit->getTransport().isRecording();
        EngineHelpers::toggleRecord (*edit);

        if (wasRecording)
            te::EditFileOperations (*edit).save (true, true, false);
    }

    void deleteSelected()
    {
        auto sel = selectionManager.getSelectedObject (0);
        selectionManager.deselectAll();   // panels drop their references first

        if (auto clip = dynamic_cast<te::Clip*> (sel))
        {
            clip->removeFromParent();
        }
        else if (auto track = dynamic_cast<te::Track*> (sel))
        {
            if (! (track->isMasterTrack() || track->isMarkerTrack()
                   || track->isTempoTrack() || track->isChordTrack()))
                edit->deleteTrack (track);
        }
    }

    //==============================================================================
    void newProject()
    {
        FileChooser fc ("New Project",
                        File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Motive Studio Projects"),
                        "*.tracktionedit");

        if (fc.browseForFileToSave (true))
        {
            te::EditFileOperations (*edit).save (true, true, false);
            loadOrCreateEdit (fc.getResult().withFileExtension (".tracktionedit"));
        }
    }

    void openProject()
    {
        FileChooser fc ("Open Project",
                        File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Motive Studio Projects"),
                        "*.tracktionedit");

        if (fc.browseForFileToOpen())
        {
            te::EditFileOperations (*edit).save (true, true, false);
            loadOrCreateEdit (fc.getResult());
        }
    }

    void saveProject()
    {
        te::EditFileOperations (*edit).save (true, true, false);
        projectLabel.setText (currentEditFile.getFileNameWithoutExtension() + "  (saved)", dontSendNotification);
    }

    //==============================================================================
    void scanForPlugins()
    {
        auto& pm = engine.getPluginManager();
        AudioPluginFormat* vst3 = nullptr;

        for (auto* format : pm.pluginFormatManager.getFormats())
            if (format->getName() == "VST3")
                vst3 = format;

        if (vst3 == nullptr)
            return;

        const auto before = pm.knownPluginList.getNumTypes();

        auto deadMansPedal = engine.getTemporaryFileManager().getTempDirectory()
                                 .getChildFile ("scan_crash.txt");

        PluginDirectoryScanner scanner (pm.knownPluginList, *vst3,
                                        vst3->getDefaultLocationsToSearch(), true, deadMansPedal);

        String currentPlugin;
        while (scanner.scanNextFile (true, currentPlugin)) {}

        const auto found = pm.knownPluginList.getNumTypes();

        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Plugin Scan Finished",
            String (found) + " VST3 plugin(s) available (" + String (found - before)
            + " new). Use the + button at the left of each track to insert them.");
    }

    void renderMixdown()
    {
        FileChooser fc ("Render Mixdown",
                        File::getSpecialLocation (File::userDocumentsDirectory)
                            .getChildFile (currentEditFile.getFileNameWithoutExtension() + ".wav"),
                        "*.wav");

        if (! fc.browseForFileToSave (true))
            return;

        BigInteger tracksToDo;
        for (int i = 0; i < (int) te::getAllTracks (*edit).size(); ++i)
            tracksToDo.setBit (i);

        const auto range = te::TimeRange (te::TimePosition(), edit->getLength());

        if (range.getLength() == te::TimeDuration())
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Nothing to Render",
                                              "The project is empty — record or import something first.");
            return;
        }

        const bool ok = te::Renderer::renderToFile ("Render", fc.getResult().withFileExtension (".wav"),
                                                    *edit, range, tracksToDo, true, false, {}, true);

        AlertWindow::showMessageBoxAsync (ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon,
                                          ok ? "Render Complete" : "Render Failed",
                                          fc.getResult().getFullPathName());
    }

    //==============================================================================
    void showFileMenu()
    {
        PopupMenu m;
        m.addItem (1, "New Project...");
        m.addItem (2, "Open Project...");
        m.addSeparator();
        m.addItem (3, "Save");
        m.addItem (4, "Render Mixdown...");
        m.addSeparator();
        m.addItem (5, "Audio Devices...");
        m.addItem (6, "Scan for Plugins");
        m.addSeparator();
        m.addItem (7, roomSession.isInRoom() ? "Room: " + roomSession.getRoomName() + "..."
                                             : "Rooms (collaborate)...");

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (fileButton), [this] (int result)
        {
            if (result == 1)      newProject();
            else if (result == 2) openProject();
            else if (result == 3) saveProject();
            else if (result == 4) renderMixdown();
            else if (result == 5) showAudioSettings();
            else if (result == 6) scanForPlugins();
            else if (result == 7) showRoomMenu();
        });
    }

    void showRoomMenu()
    {
        PopupMenu m;

        if (roomSession.isInRoom())
        {
            m.addSectionHeader ("Room: " + roomSession.getRoomName());
            m.addItem (1, "Leave Room");
        }
        else
        {
            m.addItem (2, "Host This Project as a Room...");
            m.addSeparator();
            m.addSectionHeader ("Rooms on your network");

            auto services = roomSession.getAvailableRooms();

            if (services.empty())
                m.addItem (3, "(none found)", false);

            int id = 100;
            for (auto& s : services)
                m.addItem (id++, s.description + "   (" + s.address.toString() + ")");

            m.addSeparator();
            m.addItem (4, "Join by IP Address...");
        }

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (fileButton),
                         [this, services = roomSession.getAvailableRooms()] (int result)
        {
            if (result == 1)       roomSession.leaveRoom();
            else if (result == 2)  hostRoomFlow();
            else if (result == 4)  joinByAddressFlow();
            else if (result >= 100 && result - 100 < (int) services.size())
                joinRoomAt (services[(size_t) (result - 100)].address.toString());

            updateRoomStatus();
        });
    }

    void hostRoomFlow()
    {
        AlertWindow w ("Host a Room", "Name your room — friends on the network will see this:",
                       MessageBoxIconType::QuestionIcon);
        w.addTextEditor ("name", currentEditFile.getFileNameWithoutExtension());
        w.addButton ("Host", 1, KeyPress (KeyPress::returnKey));
        w.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

        if (w.runModalLoop() != 1)
            return;

        const auto name = w.getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;

        te::EditFileOperations (*edit).save (true, true, false);

        const auto legalName = File::createLegalFileName (name);
        auto roomDir = RoomSession::getRoomsDirectory().getChildFile (legalName);
        roomDir.createDirectory();
        auto roomEditFile = roomDir.getChildFile (legalName + ".tracktionedit");

        currentEditFile.copyFileTo (roomEditFile);
        loadOrCreateEdit (roomEditFile, true);
        gatherClipSourcesInto (roomDir);
        te::EditFileOperations (*edit).save (true, true, false);

        if (roomSession.hostRoom (name, roomEditFile))
            roomSession.attachToEdit (edit.get());
        else
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Room",
                                              "Couldn't open the room port — is another room already hosted on this Mac?");
    }

    // copy every clip's audio into the room folder and make its reference relative,
    // so the project resolves identically on every connected machine
    void gatherClipSourcesInto (const File& roomDir)
    {
        for (auto track : te::getAudioTracks (*edit))
        {
            for (auto clip : track->getClips())
            {
                auto& ref = clip->getSourceFileReference();
                auto source = ref.getFile();

                if (source.existsAsFile())
                {
                    auto dest = roomDir.getChildFile (source.getFileName());

                    if (source != dest && ! dest.existsAsFile())
                        source.copyFileTo (dest);

                    ref.setToDirectFileReference (dest, true);
                }
            }
        }
    }

    void joinRoomAt (const String& address)
    {
        if (! roomSession.joinRoom (address))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Room",
                                              "Couldn't reach a room at " + address + ".");
    }

    void joinByAddressFlow()
    {
        AlertWindow w ("Join a Room", "Enter the host's IP address (they can see it in Audio MIDI Setup or System Settings > Network):",
                       MessageBoxIconType::QuestionIcon);
        w.addTextEditor ("address", "");
        w.addButton ("Join", 1, KeyPress (KeyPress::returnKey));
        w.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

        if (w.runModalLoop() == 1 && w.getTextEditorContents ("address").trim().isNotEmpty())
            joinRoomAt (w.getTextEditorContents ("address").trim());
    }

    void applyRemoteTransportCommand (int command, double positionSeconds)
    {
        auto& transport = edit->getTransport();
        lastBroadcastTransport = command;   // so our change listener won't echo it back

        transport.setPosition (te::TimePosition::fromSeconds (positionSeconds));

        if (command == RoomSession::cmdPlay)        transport.play (false);
        else if (command == RoomSession::cmdRecord) transport.record (false);
        else                                        transport.stop (false, false);
    }

    void updateRoomStatus()
    {
        if (! roomSession.isInRoom())
        {
            roomStatusLabel.setText ({}, dontSendNotification);
            return;
        }

        const auto peers = roomSession.getNumPeers();
        roomStatusLabel.setText ("ROOM \"" + roomSession.getRoomName() + "\"  —  "
                                     + (roomSession.isHost() ? "hosting, " : "joined, ")
                                     + String (peers) + (peers == 1 ? " peer" : " peers"),
                                 dontSendNotification);
    }

    //==============================================================================
    bool keyPressed (const KeyPress& key) override
    {
        // typing text (code, names) must never trigger shortcuts or notes
        if (isTypingInTextField())
            return false;

        // in Drums/Pads mode the mapped keys play sounds, so they must not
        // trigger app shortcuts (e.g. 'r' = record while 'r' is a pad)
        if (playPanel.isMappedKey (key.getTextCharacter()))
            return true;

        if (key == KeyPress::spaceKey)
        {
            EngineHelpers::togglePlay (*edit);
            return true;
        }

        if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
        {
            toggleRecord();
            return true;
        }

        if (key == KeyPress::deleteKey || key == KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }

        if (key == KeyPress ('z', ModifierKeys::commandModifier, 0))
        {
            edit->getUndoManager().undo();
            return true;
        }

        if (key == KeyPress ('z', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0))
        {
            edit->getUndoManager().redo();
            return true;
        }

        // octave shift for the play keyboard
        if (key.getTextCharacter() == 'z') { playPanel.shiftOctave (-1); return true; }
        if (key.getTextCharacter() == 'x') { playPanel.shiftOctave (1);  return true; }

        return false;
    }

    // true while the user is typing text somewhere (code editor, track rename...)
    static bool isTypingInTextField()
    {
        auto* focused = Component::getCurrentlyFocusedComponent();

        return focused != nullptr
            && (dynamic_cast<TextEditor*> (focused) != nullptr
                || dynamic_cast<CodeEditorComponent*> (focused) != nullptr
                || focused->findParentComponentOfClass<CodeEditorComponent>() != nullptr);
    }

    // musical typing: the play panel owns the key maps for all three modes
    bool keyStateChanged (bool) override
    {
        if (isTypingInTextField())
            return false;

        return playPanel.pollComputerKeys();
    }

    void showAudioSettings()
    {
        auto selector = std::make_unique<AudioDeviceSelectorComponent> (
            engine.getDeviceManager().deviceManager, 0, 8, 0, 8, false, false, true, false);
        selector->setSize (520, 460);

        DialogWindow::LaunchOptions o;
        o.content.setOwned (selector.release());
        o.dialogTitle = "Audio Settings";
        o.dialogBackgroundColour = Colour (0xff23272b);
        o.escapeKeyTriggersCloseButton = true;
        o.resizable = false;
        o.launchAsync();
    }

    //==============================================================================
    void changeListenerCallback (ChangeBroadcaster* source) override
    {
        if (edit != nullptr && source == &edit->getTransport())
        {
            auto& transport = edit->getTransport();
            playButton.setButtonText (String::fromUTF8 (transport.isPlaying() ? "❚❚" : "▶"));
            recordButton.setButtonText (transport.isRecording() ? "Abort Rec" : "Record");
            recordButton.setColour (TextButton::buttonColourId,
                                    transport.isRecording() ? Colour (0xffb71c1c)
                                                            : getLookAndFeel().findColour (TextButton::buttonColourId));

            // while recording, lock out everything that could damage the take
            const bool recording = transport.isRecording();
            for (auto* c : std::initializer_list<Component*> { &fileButton, &addTrackButton, &midiClipButton,
                                                               &deleteButton, &undoButton, &redoButton,
                                                               &loopInButton, &loopOutButton,
                                                               &punchButton, &tempoSlider })
                c->setEnabled (! recording);

            // keep everyone's transport in step (remote-applied states won't re-send)
            const int stateNow = transport.isRecording() ? RoomSession::cmdRecord
                               : transport.isPlaying()   ? RoomSession::cmdPlay
                                                         : RoomSession::cmdStop;
            if (stateNow != lastBroadcastTransport)
            {
                lastBroadcastTransport = stateNow;
                roomSession.sendTransportCommand (stateNow, transport.getPosition().inSeconds());
            }
        }
        else if (source == &selectionManager)
        {
            auto selected = selectionManager.getSelectedObject (0);
            deleteButton.setEnabled (selected != nullptr && ! edit->getTransport().isRecording());

            auto selectedClip = dynamic_cast<te::Clip*> (selected);

            if (clipInspector != nullptr)
                clipInspector->setClip (selectedClip);

            // the inspector only has audio-clip controls, so only show it for those
            const bool showInspector = dynamic_cast<te::WaveAudioClip*> (selected) != nullptr;
            if (showInspector != inspectorVisible)
            {
                inspectorVisible = showInspector;
                resized();
            }

            if (auto midiClip = dynamic_cast<te::MidiClip*> (selected))
                if (pianoRollButton.getToggleState() && pianoRollPanel != nullptr)
                    pianoRollPanel->setClip (midiClip);
        }
    }

    void timerCallback() override
    {
        if (edit == nullptr)
            return;

        const auto seconds = edit->getTransport().getPosition().inSeconds();
        const auto mins = (int) (seconds / 60.0);
        const auto secs = seconds - mins * 60.0;

        timeDisplay.setText (String (mins) + ":" + String (secs, 1).paddedLeft ('0', 4),
                             dontSendNotification);

        updateScrollBar();   // keeps the thumb in step with zoom and project growth
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotiveStudioComponent)
};

juce::Component* createMainComponent()
{
    return new MotiveStudioComponent();
}
