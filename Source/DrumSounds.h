#pragma once

#include <tracktion_engine/tracktion_engine.h>

// The built-in drum sounds. Every sound is synthesised from a small recipe and
// written to ~/Music/Motive Studio/Samples the first time it's needed — no
// sample files to lose, and users can still point pads at their own files.
namespace DrumSounds
{
    inline juce::File getSamplesDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                       .getChildFile ("Motive Studio").getChildFile ("Samples");
        dir.createDirectory();
        return dir;
    }

    inline const juce::StringArray& getAllNames()
    {
        static const juce::StringArray names { "kick", "boom", "snare", "clap", "rim",
                                               "hat closed", "hat open", "tom low", "tom mid",
                                               "tom high", "crash", "ride", "cowbell",
                                               "shaker", "block", "perc" };
        return names;
    }

    // one small recipe per sound; t is seconds, returns the sample value
    inline void synthesise (const juce::String& name, juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        auto* out = buffer.getWritePointer (0);
        const int numSamples = buffer.getNumSamples();

        juce::Random random;
        double phase = 0.0, lowpassState = 0.0;

        auto sineSweep = [&] (double t, double startHz, double endHz, double sweepRate, double decay)
        {
            const double freq = endHz + (startHz - endHz) * std::exp (-t * sweepRate);
            phase += juce::MathConstants<double>::twoPi * freq / sampleRate;
            return std::sin (phase) * std::exp (-t * decay);
        };

        auto noise = [&] { return random.nextFloat() * 2.0f - 1.0f; };

        // crude one-pole high-pass: noise minus its low-passed self
        auto brightNoise = [&] (double cutoff01)
        {
            const double n = noise();
            lowpassState += cutoff01 * (n - lowpassState);
            return n - lowpassState;
        };

        for (int i = 0; i < numSamples; ++i)
        {
            const double t = i / sampleRate;
            double s = 0.0;

            if      (name == "kick")       s = sineSweep (t, 120, 45, 20, 6) * 0.9 + noise() * std::exp (-t * 200) * 0.2;
            else if (name == "boom")       s = sineSweep (t, 80, 35, 12, 3) * 0.9;
            else if (name == "snare")      s = noise() * std::exp (-t * 18) * 0.7 + std::sin (juce::MathConstants<double>::twoPi * 180 * t) * std::exp (-t * 25) * 0.35;
            else if (name == "clap")       s = noise() * (std::exp (-std::fmod (t, 0.012) * 350) * (t < 0.036 ? 1.0 : 0.0) + std::exp (-t * 14)) * 0.5;
            else if (name == "rim")        s = noise() * std::exp (-t * 120) * 0.6 + std::sin (juce::MathConstants<double>::twoPi * 1100 * t) * std::exp (-t * 70) * 0.5;
            else if (name == "hat closed") s = brightNoise (0.4) * std::exp (-t * 40) * 0.7;
            else if (name == "hat open")   s = brightNoise (0.4) * std::exp (-t * 6) * 0.55;
            else if (name == "tom low")    s = sineSweep (t, 130, 85, 10, 8) * 0.85 + noise() * std::exp (-t * 60) * 0.1;
            else if (name == "tom mid")    s = sineSweep (t, 190, 130, 10, 9) * 0.85 + noise() * std::exp (-t * 60) * 0.1;
            else if (name == "tom high")   s = sineSweep (t, 260, 185, 10, 10) * 0.85 + noise() * std::exp (-t * 60) * 0.1;
            else if (name == "crash")      s = brightNoise (0.3) * std::exp (-t * 2.2) * 0.6;
            else if (name == "ride")       s = brightNoise (0.35) * std::exp (-t * 1.8) * 0.3 + std::sin (juce::MathConstants<double>::twoPi * 5270 * t) * std::exp (-t * 3) * 0.15;
            else if (name == "cowbell")    s = ((std::sin (juce::MathConstants<double>::twoPi * 540 * t) > 0 ? 1 : -1) * 0.3
                                                + (std::sin (juce::MathConstants<double>::twoPi * 800 * t) > 0 ? 1 : -1) * 0.3) * std::exp (-t * 12);
            else if (name == "shaker")     s = brightNoise (0.5) * std::exp (-t * 25) * juce::jmin (1.0, t * 200) * 0.5;
            else if (name == "block")      s = std::sin (juce::MathConstants<double>::twoPi * 900 * t) * std::exp (-t * 40) * 0.8;
            else /* perc */                s = std::sin (juce::MathConstants<double>::twoPi * 330 * t) * std::exp (-t * 15) * 0.7;

            out[i] = (float) s;
        }
    }

    inline double lengthSecondsFor (const juce::String& name)
    {
        if (name == "crash" || name == "ride")  return 2.0;
        if (name == "hat open" || name == "boom") return 1.0;
        return 0.6;
    }

    // returns the sound's WAV file, synthesising it on first use
    inline juce::File ensure (const juce::String& name)
    {
        auto file = getSamplesDirectory().getChildFile (name.replaceCharacter (' ', '_') + ".wav");
        if (file.existsAsFile())
            return file;

        const double sampleRate = 44100.0;
        juce::AudioBuffer<float> buffer (1, (int) (sampleRate * lengthSecondsFor (name)));
        synthesise (name, buffer, sampleRate);

        juce::WavAudioFormat format;
        if (auto stream = file.createOutputStream())
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                    format.createWriterFor (stream.release(), sampleRate, 1, 16, {}, 0)))
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());

        return file;
    }
}
