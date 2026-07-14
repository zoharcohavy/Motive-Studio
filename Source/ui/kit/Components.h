/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com
*/

#pragma once

#include "../../WahPlugin.h"
#include "../../motivescript/MotiveScriptEditor.h"

namespace IDs
{
    #define DECLARE_ID(name)  const juce::Identifier name (#name);
    DECLARE_ID (EDITVIEWSTATE)
    DECLARE_ID (showMasterTrack)
    DECLARE_ID (showGlobalTrack)
    DECLARE_ID (showMarkerTrack)
    DECLARE_ID (showChordTrack)
    DECLARE_ID (showMidiDevices)
    DECLARE_ID (showWaveDevices)
    DECLARE_ID (viewX1)
    DECLARE_ID (viewX2)
    DECLARE_ID (viewY)
    DECLARE_ID (drawWaveforms)
    DECLARE_ID (showHeaders)
    DECLARE_ID (showFooters)
    DECLARE_ID (showArranger)
    #undef DECLARE_ID
}

namespace te = tracktion;

//==============================================================================
void drawMidiClip (juce::Graphics&, te::MidiClip&, juce::Rectangle<int>, te::TimeRange);

//==============================================================================
class EditViewState
{
public:
    EditViewState (te::Edit& e, te::SelectionManager& s)
        : edit (e), selectionManager (s)
    {
        state = edit.state.getOrCreateChildWithName (IDs::EDITVIEWSTATE, nullptr);
        
        auto um = &edit.getUndoManager();
        
        showMasterTrack.referTo (state, IDs::showMasterTrack, um, false);
        showGlobalTrack.referTo (state, IDs::showGlobalTrack, um, false);
        showMarkerTrack.referTo (state, IDs::showMarkerTrack, um, false);
        showChordTrack.referTo (state, IDs::showChordTrack, um, false);
        showArrangerTrack.referTo (state, IDs::showArranger, um, false);
        drawWaveforms.referTo (state, IDs::drawWaveforms, um, true);
        showHeaders.referTo (state, IDs::showHeaders, um, true);
        showFooters.referTo (state, IDs::showFooters, um, false);
        showMidiDevices.referTo (state, IDs::showMidiDevices, um, false);
        showWaveDevices.referTo (state, IDs::showWaveDevices, um, true);

        viewX1.referTo (state, IDs::viewX1, um, 0s);
        viewX2.referTo (state, IDs::viewX2, um, 60s);
        viewY.referTo (state, IDs::viewY, um, 0);
    }
    
    int timeToX (te::TimePosition time, int width) const
    {
        return roundToInt (((time - viewX1) * width) / (viewX2 - viewX1));
    }
    
    te::TimePosition xToTime (int x, int width) const
    {
        return toPosition ((viewX2 - viewX1) * (double (x) / width)) + toDuration (viewX1.get());
    }
    
    te::TimePosition beatToTime (te::BeatPosition b) const
    {
        auto& ts = edit.tempoSequence;
        return ts.toTime (b);
    }

    // set by the app: double-clicking a MIDI clip routes here to open the piano roll
    std::function<void (te::MidiClip&)> onOpenMidiClip;

    te::Edit& edit;
    te::SelectionManager& selectionManager;
    
    CachedValue<bool> showMasterTrack, showGlobalTrack, showMarkerTrack, showChordTrack, showArrangerTrack,
                      drawWaveforms, showHeaders, showFooters, showMidiDevices, showWaveDevices;
    
    CachedValue<te::TimePosition> viewX1, viewX2;
    CachedValue<double> viewY;
    
    ValueTree state;
};

//==============================================================================
class ClipComponent : public Component
{
public:
    ClipComponent (EditViewState&, te::Clip::Ptr);

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent& e) override;

    te::Clip& getClip() { return *clip; }
    
protected:
    EditViewState& editViewState;
    te::Clip::Ptr clip;
};

//==============================================================================
class AudioClipComponent : public ClipComponent
{
public:
    AudioClipComponent (EditViewState&, te::Clip::Ptr);
    
    te::WaveAudioClip* getWaveAudioClip() { return dynamic_cast<te::WaveAudioClip*> (clip.get()); }
    
    void paint (Graphics& g) override;
    
private:
    void updateThumbnail();
    void drawWaveform (Graphics& g, te::AudioClipBase& c, te::SmartThumbnail& thumb, Colour colour,
                       int left, int right, int y, int h, int xOffset);
    void drawChannels (Graphics& g, te::SmartThumbnail& thumb, Rectangle<int> area,
                       te::TimeRange time, bool useLeft, bool useRight,
                       float leftGain, float rightGain);

    std::unique_ptr<te::SmartThumbnail> thumbnail;
};

//==============================================================================
class MidiClipComponent : public ClipComponent
{
public:
    MidiClipComponent (EditViewState&, te::Clip::Ptr);
    
    te::MidiClip* getMidiClip() { return dynamic_cast<te::MidiClip*> (clip.get()); }
    
    void paint (Graphics& g) override;
};

//==============================================================================
class RecordingClipComponent : public Component,
                               private Timer
{
public:
    RecordingClipComponent (te::Track::Ptr t, EditViewState&);
    
    void paint (Graphics& g) override;
    
private:
    void timerCallback() override;
    void updatePosition();
    void initialiseThumbnailAndPunchTime();
    void drawThumbnail (Graphics& g, Colour waveformColour) const;
    bool getBoundsAndTime (juce::Rectangle<int>& bounds, tracktion::TimeRange& times) const;
    
    te::Track::Ptr track;
    EditViewState& editViewState;
    
    te::RecordingThumbnailManager::Thumbnail::Ptr thumbnail;
    te::TimePosition punchInTime = -1.0s;
};

//==============================================================================
// Per-track lane height, stored on the track itself so it saves with the
// project (and follows the track into rooms). Drag a lane's bottom edge to change it.
namespace MotiveTrackHeight
{
    static const juce::Identifier heightID ("motiveTrackHeight");

    static inline int get (te::Track& t)          { return jlimit (28, 400, (int) t.state.getProperty (heightID, 80)); }
    static inline void set (te::Track& t, int h)  { t.state.setProperty (heightID, jlimit (28, 400, h), nullptr); }
    static inline bool isNearBottom (Component& c, int y) { return y >= c.getHeight() - 6; }
}

// Which automation lanes a track shows, stored on the track ("volume|pan|wah")
// so lanes save with the project and follow it into rooms.
namespace MotiveAutoLanes
{
    static const juce::Identifier lanesID ("motiveAutoLanes");

    static inline juce::StringArray get (te::Track& t)
    {
        return juce::StringArray::fromTokens (t.state.getProperty (lanesID).toString(), "|", {});
    }

    static inline void set (te::Track& t, const juce::StringArray& lanes)
    {
        t.state.setProperty (lanesID, lanes.joinIntoString ("|"), nullptr);
    }
}

class TrackHeaderComponent : public Component,
                             private te::ValueTreeAllEventListener
{
public:
    TrackHeaderComponent (EditViewState&, te::Track::Ptr);
    ~TrackHeaderComponent() override;

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void resized() override;

private:
    bool resizingHeight = false;
    int resizeStartHeight = 0;
    void valueTreeChanged() override {}
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    
    EditViewState& editViewState;
    te::Track::Ptr track;
    
    ValueTree inputsState;
    Label trackName;
    TextButton armButton {"A"}, muteButton {"M"}, soloButton {"S"}, inputButton {"I"},
               autoButton {"AUT"}, fxButton {"FX"};
};

//==============================================================================
class PluginComponent : public TextButton
{
public:
    PluginComponent (EditViewState&, te::Plugin::Ptr);
    ~PluginComponent() override;
    
    using TextButton::clicked;
    void clicked (const ModifierKeys& modifiers) override;
    
private:
    EditViewState& editViewState;
    te::Plugin::Ptr plugin;
};

//==============================================================================
class TrackFooterComponent : public Component,
                             private FlaggedAsyncUpdater,
                             private te::ValueTreeAllEventListener
{
public:
    TrackFooterComponent (EditViewState&, te::Track::Ptr);
    ~TrackFooterComponent() override;
    
    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void resized() override;
    
private:
    void valueTreeChanged() override {}
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

    void handleAsyncUpdate() override;
    
    void buildPlugins();
    
    EditViewState& editViewState;
    te::Track::Ptr track;
    
    TextButton addButton {"+"};
    OwnedArray<PluginComponent> plugins;
    
    bool updatePlugins = false;
};

//==============================================================================
class TrackComponent : public Component,
                       private te::ValueTreeAllEventListener,
                       private FlaggedAsyncUpdater,
                       private ChangeListener
{
public:
    TrackComponent (EditViewState&, te::Track::Ptr);
    ~TrackComponent() override;

    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void resized() override;

    te::Track::Ptr getTrack() const    { return track; }

private:
    bool resizingHeight = false;
    int resizeStartHeight = 0;

    void changeListenerCallback (ChangeBroadcaster*) override;

    void valueTreeChanged() override {}
    
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    
    void handleAsyncUpdate() override;
    
    void buildClips();
    void buildRecordClips();
    
    EditViewState& editViewState;
    te::Track::Ptr track;
    
    OwnedArray<ClipComponent> clips;
    std::unique_ptr<RecordingClipComponent> recordingClip;
    
    bool updateClips = false, updatePositions = false, updateRecordClips = false;
};

//==============================================================================
class PlayheadComponent : public Component,
                          private Timer
{
public:
    PlayheadComponent (te::Edit&, EditViewState&);
    
    void paint (Graphics& g) override;
    bool hitTest (int x, int y) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;

private:
    void timerCallback() override;

    te::Edit& edit;
    EditViewState& editViewState;

    int xPosition = 0;
    bool firstTimer = true;
    te::TimeRange lastLoopRange;   // so loop-marker changes trigger a repaint
};

//==============================================================================
// An automation curve lane shown directly beneath its track, time-aligned with
// the clips above. Click to add a point, drag to move, right-click to remove.
class AutomationLaneComponent : public Component,
                                private Timer
{
public:
    AutomationLaneComponent (EditViewState&, te::Track::Ptr, const juce::String& type);

    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseDoubleClick (const MouseEvent&) override;

    te::Track::Ptr getTrack() const     { return track; }
    juce::String getType() const        { return laneType; }

    static constexpr int laneHeight = 56;

private:
    void timerCallback() override;
    te::AutomatableParameter::Ptr resolveParameter() const;
    float valueToY (float) const;
    float yToValue (float) const;
    int findPointNear (Point<float>) const;
    Colour laneColour() const;

    EditViewState& editViewState;
    te::Track::Ptr track;
    juce::String laneType;
    te::AutomatableParameter::Ptr parameter;
    int dragIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationLaneComponent)
};

//==============================================================================
class EditComponent : public Component,
                      private te::ValueTreeAllEventListener,
                      private FlaggedAsyncUpdater,
                      private ChangeListener
{
public:
    EditComponent (te::Edit&, te::SelectionManager&);
    ~EditComponent() override;
    
    EditViewState& getEditViewState()   { return editViewState; }
    
private:
    void valueTreeChanged() override {}
    
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    
    void handleAsyncUpdate() override;
    void resized() override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;

    void changeListenerCallback (ChangeBroadcaster*) override { repaint(); }

    void buildTracks();

    int contentHeight = 0;   // total height of all track rows + lanes, for scroll clamping
    
    te::Edit& edit;
    
    EditViewState editViewState;
    
    PlayheadComponent playhead {edit, editViewState};
    OwnedArray<TrackComponent> tracks;
    OwnedArray<TrackHeaderComponent> headers;
    OwnedArray<TrackFooterComponent> footers;
    OwnedArray<AutomationLaneComponent> autoLanes;
    
    bool updateTracks = false, updateZoom = false;
};

#include "Components.cpp"
