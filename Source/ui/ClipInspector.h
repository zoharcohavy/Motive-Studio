#pragma once

// Included from MainComponent.cpp after the tracktion example UI kit.
// A thin strip that appears when a clip is selected: fades and stretch for
// audio clips, a pointer to the piano roll for MIDI clips.

class ClipInspector : public Component
{
public:
    ClipInspector()
    {
        auto setUpSlider = [this] (Slider& s, Label& l, const String& name,
                                   double min, double max, double step)
        {
            l.setText (name, dontSendNotification);
            l.setFont (FontOptions (11.5f, Font::bold));
            l.setJustificationType (Justification::centredRight);
            addAndMakeVisible (l);

            s.setSliderStyle (Slider::LinearHorizontal);
            s.setTextBoxStyle (Slider::TextBoxRight, false, 52, 18);
            s.setRange (min, max, step);
            addAndMakeVisible (s);
        };

        setUpSlider (fadeInSlider,  fadeInLabel,  "Fade In (s)",  0.0, 10.0, 0.01);
        setUpSlider (fadeOutSlider, fadeOutLabel, "Fade Out (s)", 0.0, 10.0, 0.01);
        setUpSlider (speedSlider,   speedLabel,   "Speed %",      25.0, 400.0, 1.0);
        speedSlider.setDoubleClickReturnValue (true, 100.0);

        fadeInSlider.onValueChange = [this]
        {
            if (auto wc = getWaveClip())
                wc->setFadeIn (te::TimeDuration::fromSeconds (fadeInSlider.getValue()));
        };

        fadeOutSlider.onValueChange = [this]
        {
            if (auto wc = getWaveClip())
                wc->setFadeOut (te::TimeDuration::fromSeconds (fadeOutSlider.getValue()));
        };

        speedSlider.onValueChange = [this]
        {
            if (auto wc = getWaveClip())
                wc->setSpeedRatio (speedSlider.getValue() / 100.0);
        };

        stretchToggle.setButtonText ("Stretch to tempo");
        stretchToggle.onClick = [this]
        {
            if (auto wc = getWaveClip())
            {
                wc->setTimeStretchMode (te::TimeStretcher::defaultMode);
                wc->setAutoTempo (stretchToggle.getToggleState());
            }
        };
        addAndMakeVisible (stretchToggle);
    }

    void setClip (te::Clip* newClip)
    {
        clip = newClip;

        auto wave = getWaveClip();
        const bool isWave = wave != nullptr;

        for (auto* c : std::initializer_list<Component*> { &fadeInSlider, &fadeInLabel, &fadeOutSlider,
                                                           &fadeOutLabel, &speedSlider, &speedLabel, &stretchToggle })
            c->setVisible (isWave);

        if (isWave)
        {
            fadeInSlider.setValue (wave->getFadeIn().inSeconds(), dontSendNotification);
            fadeOutSlider.setValue (wave->getFadeOut().inSeconds(), dontSendNotification);
            speedSlider.setValue (wave->getSpeedRatio() * 100.0, dontSendNotification);
            stretchToggle.setToggleState (wave->getAutoTempo(), dontSendNotification);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 3);

        auto third = r.getWidth() / 4;
        fadeInLabel.setBounds (r.removeFromLeft (70));
        fadeInSlider.setBounds (r.removeFromLeft (third - 70));
        fadeOutLabel.setBounds (r.removeFromLeft (76));
        fadeOutSlider.setBounds (r.removeFromLeft (third - 76));
        speedLabel.setBounds (r.removeFromLeft (60));
        speedSlider.setBounds (r.removeFromLeft (third - 60));
        stretchToggle.setBounds (r.reduced (6, 0));
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff23272c));
    }

private:
    te::WaveAudioClip* getWaveClip() const  { return dynamic_cast<te::WaveAudioClip*> (clip.get()); }

    te::Clip::Ptr clip;   // ref-counted: stays valid even if removed remotely
    Slider fadeInSlider, fadeOutSlider, speedSlider;
    Label fadeInLabel, fadeOutLabel, speedLabel;
    ToggleButton stretchToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipInspector)
};
