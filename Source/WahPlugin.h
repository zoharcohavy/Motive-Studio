#pragma once

#include <tracktion_engine/tracktion_engine.h>

// A Crybaby-style wah built into the app as an engine plugin, so the "Wah"
// automation lane has a real pedal to move. One parameter: Pedal (0 = heel,
// 1 = toe), sweeping a resonant bandpass from 350 Hz to ~2.2 kHz exponentially.
class WahPlugin : public tracktion::Plugin
{
public:
    explicit WahPlugin (tracktion::PluginCreationInfo info)
        : tracktion::Plugin (info)
    {
        auto um = getUndoManager();

        pedalValue.referTo (state, juce::Identifier ("pedal"), um, 0.5f);

        pedalParam = addParam ("pedal", TRANS("Pedal"), { 0.0f, 1.0f },
                               [] (float value)             { return juce::String (value, 2); },
                               [] (const juce::String& s)   { return s.getFloatValue(); });

        pedalParam->attachToCurrentValue (pedalValue);
    }

    ~WahPlugin() override
    {
        notifyListenersOfDeletion();
        pedalParam->detachFromCurrentValue();
    }

    static const char* getPluginName()                  { return NEEDS_TRANS("Motive Wah"); }
    static constexpr const char* xmlTypeName            = "motiveWah";

    juce::String getName() const override               { return "Wah"; }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getSelectableDescription() override    { return TRANS("Motive Wah"); }

    int getNumOutputChannelsGivenInputs (int numInputChannels) override
    {
        return juce::jmin (numInputChannels, 2);
    }

    void initialise (const tracktion::PluginInitialisationInfo& info) override
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = info.sampleRate;
        spec.maximumBlockSize = (juce::uint32) info.blockSizeSamples;
        spec.numChannels = 2;

        filter.prepare (spec);
        filter.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        filter.setResonance (7.0f);
        filter.reset();

        // ~15 ms position smoothing: automation jumps sound like a foot, not a zipper
        positionSmoothingCoeff = 1.0f - std::exp (-1.0f / (0.015f * (float) info.sampleRate));
        smoothedPosition = pedalParam->getCurrentValue();
    }

    void deinitialise() override {}

    void applyToBuffer (const tracktion::PluginRenderContext& fc) override
    {
        if (fc.destBuffer == nullptr)
            return;

        SCOPED_REALTIME_CHECK

        const auto target = juce::jlimit (0.0f, 1.0f, pedalParam->getCurrentValue());
        const int numChannels = juce::jmin (2, fc.destBuffer->getNumChannels());

        for (int i = 0; i < fc.bufferNumSamples; ++i)
        {
            smoothedPosition += positionSmoothingCoeff * (target - smoothedPosition);

            // heel 350 Hz -> toe 2.2 kHz, exponential like the real pot taper
            filter.setCutoffFrequency (350.0f * std::pow (6.2857f, smoothedPosition));

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = fc.destBuffer->getWritePointer (ch, fc.bufferStartSample);
                const float in = data[i];
                data[i] = filter.processSample (ch, in) * 1.5f + in * 0.1f;
            }
        }
    }

    juce::CachedValue<float> pedalValue;
    tracktion::AutomatableParameter::Ptr pedalParam;

private:
    juce::dsp::StateVariableTPTFilter<float> filter;
    float smoothedPosition = 0.5f, positionSmoothingCoeff = 0.05f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WahPlugin)
};
