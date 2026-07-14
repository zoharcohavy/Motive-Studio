#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include "MotiveScript.h"

// The user's saved-effects library: plain .ms text files, one per effect.
// The Script panel saves here; every track's FX button reads from here.
namespace MotiveScriptLibrary
{
    inline juce::File getDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                       .getChildFile ("Motive Studio").getChildFile ("Effects");
        dir.createDirectory();
        return dir;
    }

    inline juce::Array<juce::File> getEffectFiles()
    {
        auto files = getDirectory().findChildFiles (juce::File::findFiles, false, "*.ms");
        files.sort();
        return files;
    }
}

// The MotiveScript plugin: a per-sample script running as a track effect.
//
// - The source code lives in the plugin's state, so it saves with the project,
//   is undoable, and syncs live in rooms (two people can co-write an effect).
// - `param` declarations bind to 8 pre-created automatable slots (P1..P8), so
//   script parameters get automation lanes and room sync like any other knob.
// - Compilation happens on the message thread; the audio thread picks up the
//   new program at the next block via a try-lock, so audio never blocks.
class MotiveScriptPlugin : public tracktion::Plugin
{
public:
    explicit MotiveScriptPlugin (tracktion::PluginCreationInfo info)
        : tracktion::Plugin (info)
    {
        auto um = getUndoManager();
        sourceCode.referTo (state, juce::Identifier ("source"), um, getDefaultScript());

        for (int i = 0; i < motivescript::Program::maxParams; ++i)
        {
            slotValues[(size_t) i].referTo (state, juce::Identifier ("p" + juce::String (i + 1)), um, 0.0f);

            auto param = addParam ("p" + juce::String (i + 1), "P" + juce::String (i + 1), { 0.0f, 1.0f });
            param->attachToCurrentValue (slotValues[(size_t) i]);
            slots[(size_t) i] = param;
        }

        recompile();
    }

    ~MotiveScriptPlugin() override
    {
        notifyListenersOfDeletion();

        for (auto& param : slots)
            param->detachFromCurrentValue();
    }

    static const char* getPluginName()                  { return NEEDS_TRANS("MotiveScript"); }
    static constexpr const char* xmlTypeName            = "motiveScript";

    juce::String getName() const override               { return "MotiveScript"; }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getSelectableDescription() override    { return TRANS("MotiveScript"); }

    int getNumOutputChannelsGivenInputs (int numInputChannels) override
    {
        return juce::jmin (numInputChannels, 2);
    }

    //==============================================================================
    juce::String getSource() const                      { return sourceCode.get(); }

    void setSource (const juce::String& newSource)
    {
        if (newSource != sourceCode.get())
            sourceCode = newSource;   // triggers valueTreePropertyChanged -> recompile
    }

    // last compile outcome, for the editor UI (message thread only)
    juce::String getCompileError() const                { return compileError; }
    int getCompileErrorLine() const                     { return compileErrorLine; }
    std::shared_ptr<const motivescript::Program> getProgram() const { return uiProgram; }

    // the real (range-mapped) value of a declared param, and its setter — used
    // by the editor's sliders, which talk to the underlying 0..1 slot
    tracktion::AutomatableParameter::Ptr getSlot (int index)    { return slots[(size_t) index]; }

    std::function<void()> onRecompiled;   // editor refresh hook

    //==============================================================================
    void initialise (const tracktion::PluginInitialisationInfo& info) override
    {
        sampleRate = info.sampleRate;

        const int historyLength = (int) (sampleRate * historySeconds);
        for (auto& h : historyBuffers)
            h.assign ((size_t) historyLength, 0.0f);

        historyWrite = 0;
        globalSampleCount = 0;
    }

    void deinitialise() override {}

    void applyToBuffer (const tracktion::PluginRenderContext& fc) override
    {
        if (fc.destBuffer == nullptr)
            return;

        SCOPED_REALTIME_CHECK

        // adopt a newly compiled program if one is waiting (never blocks)
        if (swapLock.tryEnter())
        {
            if (pendingProgram != nullptr)
            {
                activeProgram = std::move (pendingProgram);
                vars.swap (pendingVars);
            }
            swapLock.exit();
        }

        const auto program = activeProgram;
        const int numChannels = juce::jmin (2, fc.destBuffer->getNumChannels());
        const int historyLength = (int) historyBuffers[0].size();

        // resolve params to their real ranges once per block
        double paramValues[motivescript::Program::maxParams] = {};
        if (program != nullptr)
            for (size_t i = 0; i < program->params.size(); ++i)
            {
                const auto& p = program->params[i];
                paramValues[i] = p.low + slots[i]->getCurrentValue() * (p.high - p.low);
            }

        // beat/tempo for @beat-synced scripts
        auto& ts = edit.tempoSequence;
        const auto blockStart = fc.editTime.getStart();
        const double startBeat = ts.toBeats (blockStart).inBeats();
        const double bpm = ts.getTempoAt (blockStart).getBpm();
        const double beatsPerSample = bpm / 60.0 / sampleRate;
        const double blockStartSeconds = blockStart.inSeconds();

        float* channelData[2] = { nullptr, nullptr };
        for (int ch = 0; ch < numChannels; ++ch)
            channelData[ch] = fc.destBuffer->getWritePointer (ch, fc.bufferStartSample);

        for (int i = 0; i < fc.bufferNumSamples; ++i)
        {
            const float inL = channelData[0] != nullptr ? channelData[0][i] : 0.0f;
            const float inR = channelData[1] != nullptr ? channelData[1][i] : inL;

            // record the input's past for sample()
            if (historyLength > 0)
            {
                historyBuffers[0][(size_t) historyWrite] = inL;
                historyBuffers[1][(size_t) historyWrite] = inR;
                historyWrite = (historyWrite + 1) % historyLength;
            }

            if (program != nullptr && ! program->ops.empty())
            {
                motivescript::Context ctx;
                ctx.params = paramValues;
                ctx.t = blockStartSeconds + i / sampleRate;
                ctx.n = (double) globalSampleCount;
                ctx.sampleRate = sampleRate;
                ctx.beat = startBeat + i * beatsPerSample;
                ctx.tempo = bpm;
                ctx.history[0] = historyBuffers[0].data();
                ctx.history[1] = historyBuffers[1].data();
                ctx.historySize = historyLength;
                ctx.historyWrite = historyWrite;
                ctx.rngState = rngState;

                const auto numVars = program->varNames.size();
                ctx.s[0] = inL;
                ctx.s[1] = inR;

                if (program->stereo)
                {
                    ctx.vars = vars.data();
                    motivescript::run (*program, ctx);
                }
                else
                {
                    // mono: run per channel, each channel with its own variable bank
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        ctx.channel = ch;
                        ctx.vars = vars.data() + numVars * (size_t) ch;
                        motivescript::run (*program, ctx);
                    }
                }

                rngState = ctx.rngState;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    auto out = (float) ctx.s[ch];
                    channelData[ch][i] = std::isfinite (out) ? juce::jlimit (-4.0f, 4.0f, out) : 0.0f;
                }
            }

            ++globalSampleCount;
        }
    }

    //==============================================================================
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override
    {
        // recompile on any source change — including ones arriving from a room
        if (v == state && id == juce::Identifier ("source"))
            triggerRecompile();

        tracktion::Plugin::valueTreePropertyChanged (v, id);
    }

    static juce::String getDefaultScript()
    {
        return "// MotiveScript: you write the body of the per-sample loop.\n"
               "// @s is the current sample - change it and you've made an effect.\n"
               "\n"
               "param drive 1 to 25 = 6;\n"
               "param mix 0 to 1 = 0.5;\n"
               "\n"
               "wet = tanh(@s * drive);\n"
               "@s = lerp(@s, wet, mix);\n";
    }

private:
    void triggerRecompile()
    {
        // compile is fast, but keep the audio thread untouched: build everything
        // here, hand it over under the lock
        juce::MessageManager::callAsync ([safe = makeSafeRef (*this)]
        {
            if (auto self = safe.get())
                self->recompile();
        });
    }

    // tiny safe-ref helper: plugins are ref-counted, so hold a Ptr
    struct SafeRef
    {
        Plugin::Ptr ptr;
        MotiveScriptPlugin* get() const { return dynamic_cast<MotiveScriptPlugin*> (ptr.get()); }
    };

    static SafeRef makeSafeRef (MotiveScriptPlugin& p)  { return { Plugin::Ptr (&p) }; }

    void recompile()
    {
        auto result = motivescript::Compiler::compile (sourceCode.get().toStdString());

        compileError = juce::String (result.error);
        compileErrorLine = result.errorLine;

        if (result.program != nullptr)
        {
            uiProgram = result.program;

            // set defaults for params that just appeared in the script
            for (size_t i = 0; i < result.program->params.size(); ++i)
            {
                const auto& p = result.program->params[i];
                const auto name = juce::String (p.name);

                if (name != boundParamNames[i])
                {
                    boundParamNames[i] = name;
                    const auto range = p.high - p.low;
                    slots[i]->setParameter (range != 0.0 ? (float) ((p.def - p.low) / range) : 0.0f,
                                            juce::dontSendNotification);
                }
            }

            // per-channel variable banks for mono mode (x2), single bank for stereo
            std::vector<double> freshVars (result.program->varNames.size() * 2, 0.0);

            const juce::SpinLock::ScopedLockType sl (swapLock);
            pendingProgram = result.program;
            pendingVars.swap (freshVars);
        }

        if (onRecompiled != nullptr)
            onRecompiled();
    }

    juce::CachedValue<juce::String> sourceCode;
    std::array<tracktion::AutomatableParameter::Ptr, motivescript::Program::maxParams> slots;
    std::array<juce::CachedValue<float>, motivescript::Program::maxParams> slotValues;
    std::array<juce::String, motivescript::Program::maxParams> boundParamNames;

    // audio-thread state
    std::shared_ptr<const motivescript::Program> activeProgram;
    std::vector<double> vars;
    std::array<std::vector<float>, 2> historyBuffers;
    int historyWrite = 0;
    int64_t globalSampleCount = 0;
    uint32_t rngState = 0x9e3779b9;
    double sampleRate = 44100.0;

    // message-thread -> audio-thread handoff
    juce::SpinLock swapLock;
    std::shared_ptr<const motivescript::Program> pendingProgram;
    std::vector<double> pendingVars;

    // message-thread copies for the editor
    std::shared_ptr<const motivescript::Program> uiProgram;
    juce::String compileError;
    int compileErrorLine = 0;

    static constexpr double historySeconds = 10.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotiveScriptPlugin)
};
