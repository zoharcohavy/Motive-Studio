#pragma once

// Included from MainComponent.cpp after the tracktion example UI kit,
// so juce (via using-directive), te::, and EngineHelpers are available.

//==============================================================================
class MixerStrip : public Component
{
public:
    MixerStrip (te::AudioTrack& t) : track (t)
    {
        nameLabel.setText (track.getName(), dontSendNotification);
        nameLabel.setJustificationType (Justification::centred);
        nameLabel.setFont (FontOptions (11.5f, Font::bold));
        nameLabel.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (nameLabel);

        panKnob.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        panKnob.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        panKnob.setRange (-1.0, 1.0, 0.01);
        panKnob.setDoubleClickReturnValue (true, 0.0);
        panKnob.onValueChange = [this]
        {
            if (auto vp = track.getVolumePlugin())
                vp->setPan ((float) panKnob.getValue());
        };
        addAndMakeVisible (panKnob);

        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.onValueChange = [this]
        {
            if (auto vp = track.getVolumePlugin())
                vp->setVolumeDb ((float) fader.getValue());
        };
        addAndMakeVisible (fader);

        muteButton.setClickingTogglesState (false);
        muteButton.setColour (TextButton::buttonOnColourId, Colour (0xffe53935));
        muteButton.onClick = [this] { track.setMute (! track.isMuted (false)); };
        addAndMakeVisible (muteButton);

        soloButton.setClickingTogglesState (false);
        soloButton.setColour (TextButton::buttonOnColourId, Colour (0xfffbc02d));
        soloButton.onClick = [this] { track.setSolo (! track.isSolo (false)); };
        addAndMakeVisible (soloButton);

        if (auto lm = track.getLevelMeterPlugin())
            lm->measurer.addClient (meterClient);

        refreshFromTrack();
    }

    ~MixerStrip() override
    {
        if (auto lm = track.getLevelMeterPlugin())
            lm->measurer.removeClient (meterClient);
    }

    // pull engine state into the controls (covers remote/room and automation changes)
    void refreshFromTrack()
    {
        if (auto vp = track.getVolumePlugin())
        {
            if (! fader.isMouseButtonDown())
                fader.setValue (vp->getVolumeDb(), dontSendNotification);

            if (! panKnob.isMouseButtonDown())
                panKnob.setValue (vp->getPan(), dontSendNotification);
        }

        muteButton.setToggleState (track.isMuted (false), dontSendNotification);
        soloButton.setToggleState (track.isSolo (false), dontSendNotification);
        nameLabel.setText (track.getName(), dontSendNotification);
    }

    void updateMeter()
    {
        auto levelL = meterClient.getAndClearAudioLevel (0).dB;
        auto levelR = meterClient.getAndClearAudioLevel (1).dB;
        auto db = jmax (levelL, levelR);

        // fast attack, slow release
        displayedDb = db > displayedDb ? db : displayedDb - 1.2f;
        displayedDb = jlimit (-60.0f, 6.0f, displayedDb);
        repaint (meterArea);
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colour (0xff2a2e33));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f);

        // level meter
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRect (meterArea);

        const auto proportion = jmap (displayedDb, -60.0f, 6.0f, 0.0f, 1.0f);
        auto lit = meterArea.withTrimmedTop (roundToInt ((1.0f - proportion) * (float) meterArea.getHeight()));

        g.setGradientFill (ColourGradient (Colour (0xffe53935), 0.0f, (float) meterArea.getY(),
                                           Colour (0xff2ecc71), 0.0f, (float) meterArea.getBottom(), false));
        g.fillRect (lit);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        nameLabel.setBounds (r.removeFromTop (18));
        panKnob.setBounds (r.removeFromTop (34).withSizeKeepingCentre (34, 34));

        auto buttons = r.removeFromBottom (22);
        muteButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1));
        soloButton.setBounds (buttons.reduced (1));

        meterArea = r.removeFromRight (10).reduced (0, 4);
        fader.setBounds (r);
    }

    te::AudioTrack& track;

private:
    Label nameLabel;
    Slider panKnob, fader;
    TextButton muteButton { "M" }, soloButton { "S" };
    te::LevelMeasurer::Client meterClient;
    Rectangle<int> meterArea;
    float displayedDb = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

//==============================================================================
class MixerPanel : public Component,
                   private Timer
{
public:
    explicit MixerPanel (te::Edit& e) : edit (e)
    {
        masterLabel.setText ("MASTER", dontSendNotification);
        masterLabel.setJustificationType (Justification::centred);
        masterLabel.setFont (FontOptions (11.5f, Font::bold));
        addAndMakeVisible (masterLabel);

        masterFader.setSliderStyle (Slider::LinearVertical);
        masterFader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        masterFader.setRange (-60.0, 6.0, 0.1);
        masterFader.setDoubleClickReturnValue (true, 0.0);
        masterFader.setSkewFactorFromMidPoint (-12.0);
        masterFader.onValueChange = [this]
        {
            if (auto mv = edit.getMasterVolumePlugin())
                mv->setVolumeDb ((float) masterFader.getValue());
        };
        addAndMakeVisible (masterFader);

        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&stripHolder, false);
        viewport.setScrollBarsShown (false, true);

        rebuildStrips();
        startTimerHz (30);
    }

    void rebuildStrips()
    {
        strips.clear();
        auto tracks = te::getAudioTracks (edit);

        for (auto t : tracks)
        {
            auto strip = new MixerStrip (*t);
            strips.add (strip);
            stripHolder.addAndMakeVisible (strip);
        }

        resized();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff1b1e22));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);

        auto master = r.removeFromRight (86);
        masterLabel.setBounds (master.removeFromTop (18));
        masterFader.setBounds (master.reduced (18, 4));

        viewport.setBounds (r);

        const int stripWidth = 92;
        stripHolder.setSize (jmax (r.getWidth(), stripWidth * strips.size()), r.getHeight());

        int x = 0;
        for (auto* s : strips)
        {
            s->setBounds (x, 0, stripWidth, stripHolder.getHeight());
            x += stripWidth;
        }
    }

private:
    void timerCallback() override
    {
        // track list changed? rebuild
        auto tracks = te::getAudioTracks (edit);

        if (tracks.size() != strips.size()
            || ! std::equal (strips.begin(), strips.end(), tracks.begin(),
                             [] (auto* s, auto* t) { return &s->track == t; }))
        {
            rebuildStrips();
            return;
        }

        for (auto* s : strips)
        {
            s->updateMeter();
            s->refreshFromTrack();
        }

        if (auto mv = edit.getMasterVolumePlugin())
            if (! masterFader.isMouseButtonDown())
                masterFader.setValue (mv->getVolumeDb(), dontSendNotification);
    }

    te::Edit& edit;
    Viewport viewport;
    Component stripHolder;
    OwnedArray<MixerStrip> strips;
    Label masterLabel;
    Slider masterFader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
};
