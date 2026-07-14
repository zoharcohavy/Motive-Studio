#pragma once

#include "MotiveScriptPlugin.h"

// The MotiveScript editor window: code on top, compile status below it, and a
// slider for every `param` the script declares. Typing recompiles after a
// 350 ms pause — the sound changes as you write, and errors show inline.
// Written with explicit juce:: so it can be included from anywhere.
class MotiveScriptEditor : public juce::Component,
                           private juce::MultiTimer,
                           private juce::CodeDocument::Listener
{
public:
    // opens (or refocuses) the editor window for a plugin instance
    static void show (MotiveScriptPlugin& plugin)
    {
        auto& windows = openWindows();

        if (auto existing = windows[&plugin])
        {
            existing->toFront (true);
            return;
        }

        auto* window = new EditorWindow (plugin);
        windows[&plugin] = window;
        window->setVisible (true);
    }

    ~MotiveScriptEditor() override
    {
        plugin->onRecompiled = nullptr;
        openWindows().erase (plugin);
    }

private:
    //==============================================================================
    struct EditorWindow : juce::DocumentWindow
    {
        explicit EditorWindow (MotiveScriptPlugin& p)
            : DocumentWindow ("MotiveScript" + (p.getOwnerTrack() != nullptr
                                                    ? " — " + p.getOwnerTrack()->getName() : juce::String()),
                              juce::Colour (0xff23272b), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MotiveScriptEditor (p), true);
            setResizable (true, true);
            setResizeLimits (420, 320, 10000, 10000);
            centreWithSize (620, 520);
            setAlwaysOnTop (false);
        }

        void closeButtonPressed() override    { delete this; }
    };

    static std::map<void*, juce::Component::SafePointer<juce::DocumentWindow>>& openWindows()
    {
        static std::map<void*, juce::Component::SafePointer<juce::DocumentWindow>> windows;
        return windows;
    }

    //==============================================================================
    explicit MotiveScriptEditor (MotiveScriptPlugin& p)
        : plugin (&p)
    {
        document.replaceAllContent (p.getSource());
        document.addListener (this);

        codeEditor = std::make_unique<juce::CodeEditorComponent> (document, &tokeniser);
        codeEditor->setTabSize (4, true);
        codeEditor->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        addAndMakeVisible (*codeEditor);

        status.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (status);

        plugin->onRecompiled = [this] { refreshFromPlugin(); };
        refreshFromPlugin();

        startTimer (sliderRefreshTimer, 100);
        setSize (620, 520);
    }

    //==============================================================================
    void refreshFromPlugin()
    {
        const auto error = plugin->getCompileError();

        if (error.isEmpty())
        {
            const auto program = plugin->getProgram();
            status.setColour (juce::Label::textColourId, juce::Colour (0xff2ecc71));
            status.setText ("OK — " + juce::String (program != nullptr ? (int) program->ops.size() : 0)
                                + " ops, " + juce::String (program != nullptr ? (int) program->params.size() : 0)
                                + " params" + (program != nullptr && program->stereo ? ", stereo" : ", mono"),
                            juce::dontSendNotification);
        }
        else
        {
            status.setColour (juce::Label::textColourId, juce::Colour (0xffef5350));
            status.setText ("line " + juce::String (plugin->getCompileErrorLine()) + ": " + error,
                            juce::dontSendNotification);
        }

        rebuildParamSliders();
    }

    void rebuildParamSliders()
    {
        paramRows.clear();

        if (auto program = plugin->getProgram())
        {
            for (size_t i = 0; i < program->params.size(); ++i)
            {
                const auto& p = program->params[i];
                auto row = std::make_unique<ParamRow>();

                row->label.setText (p.name, juce::dontSendNotification);
                row->label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
                addAndMakeVisible (row->label);

                row->slider.setRange (p.low, p.high, 0.0);
                row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);

                auto slot = plugin->getSlot ((int) i);
                const auto range = p.high - p.low;

                row->slider.setValue (p.low + slot->getCurrentValue() * range, juce::dontSendNotification);
                row->slider.onValueChange = [slot, low = p.low, range, s = &row->slider]
                {
                    slot->setParameter (range != 0.0 ? (float) ((s->getValue() - low) / range) : 0.0f,
                                        juce::sendNotification);
                };

                addAndMakeVisible (row->slider);
                paramRows.push_back (std::move (row));
            }
        }

        resized();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        // params dock at the bottom; code takes the rest
        for (auto it = paramRows.rbegin(); it != paramRows.rend(); ++it)
        {
            auto row = r.removeFromBottom (26);
            (*it)->label.setBounds (row.removeFromLeft (110));
            (*it)->slider.setBounds (row);
        }

        status.setBounds (r.removeFromBottom (22));

        if (codeEditor != nullptr)
            codeEditor->setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1e22));
    }

    //==============================================================================
    void codeDocumentTextInserted (const juce::String&, int) override  { startTimer (compileTimer, 350); }
    void codeDocumentTextDeleted (int, int) override                   { startTimer (compileTimer, 350); }

    void timerCallback (int timerID) override
    {
        if (timerID == compileTimer)
        {
            stopTimer (compileTimer);
            plugin->setSource (document.getAllContent());
            return;
        }

        // keep sliders following automation / remote peers (when not being dragged)
        if (auto program = plugin->getProgram())
        {
            for (size_t i = 0; i < paramRows.size() && i < program->params.size(); ++i)
            {
                auto& slider = paramRows[i]->slider;

                if (! slider.isMouseButtonDown())
                {
                    const auto& p = program->params[i];
                    slider.setValue (p.low + plugin->getSlot ((int) i)->getCurrentValue() * (p.high - p.low),
                                     juce::dontSendNotification);
                }
            }
        }
    }

    //==============================================================================
    struct ParamRow
    {
        juce::Label label;
        juce::Slider slider;
    };

    enum { compileTimer = 0, sliderRefreshTimer = 1 };

    MotiveScriptPlugin* plugin;                 // set first (declaration order matters)
    tracktion::Plugin::Ptr keepAlive { plugin };   // then holds it alive

    juce::CodeDocument document;
    juce::CPlusPlusCodeTokeniser tokeniser;
    std::unique_ptr<juce::CodeEditorComponent> codeEditor;
    juce::Label status;
    std::vector<std::unique_ptr<ParamRow>> paramRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MotiveScriptEditor)
};
