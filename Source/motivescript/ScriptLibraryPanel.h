#pragma once

#include "MotiveScriptPlugin.h"

// The Script panel: docked above the timeline, toggled from the toolbar.
// Write MotiveScript here, name it, Save — and it appears in every track's
// FX menu. The status line live-checks your code as you type (no track or
// sound needed here; this is the workbench, the FX button is the deploy).
class ScriptLibraryPanel : public juce::Component,
                           private juce::Timer,
                           private juce::CodeDocument::Listener
{
public:
    ScriptLibraryPanel()
    {
        library.setTextWhenNothingSelected ("Load saved effect...");
        library.onChange = [this] { loadSelected(); };
        addAndMakeVisible (library);

        nameEditor.setTextToShowWhenEmpty ("effect name...", juce::Colours::grey);
        addAndMakeVisible (nameEditor);

        saveButton.onClick = [this] { save(); };
        addAndMakeVisible (saveButton);

        newButton.onClick = [this]
        {
            document.replaceAllContent (MotiveScriptPlugin::getDefaultScript());
            nameEditor.setText ({});
            library.setSelectedId (0, juce::dontSendNotification);
        };
        addAndMakeVisible (newButton);

        document.replaceAllContent (MotiveScriptPlugin::getDefaultScript());
        document.addListener (this);

        codeEditor = std::make_unique<juce::CodeEditorComponent> (document, &tokeniser);
        codeEditor->setTabSize (4, true);
        codeEditor->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
        addAndMakeVisible (*codeEditor);

        status.setFont (juce::FontOptions (12.5f));
        addAndMakeVisible (status);

        refreshLibrary();
        checkCode();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);

        auto top = r.removeFromTop (24);
        library.setBounds (top.removeFromLeft (190).reduced (1));
        top.removeFromLeft (6);
        saveButton.setBounds (top.removeFromRight (58).reduced (1));
        newButton.setBounds (top.removeFromRight (50).reduced (1));
        nameEditor.setBounds (top.reduced (1));

        status.setBounds (r.removeFromBottom (20));
        codeEditor->setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1b1e22));
    }

private:
    void refreshLibrary()
    {
        library.clear (juce::dontSendNotification);

        int id = 1;
        for (const auto& f : MotiveScriptLibrary::getEffectFiles())
            library.addItem (f.getFileNameWithoutExtension(), id++);
    }

    void loadSelected()
    {
        const auto files = MotiveScriptLibrary::getEffectFiles();
        const int index = library.getSelectedId() - 1;

        if (juce::isPositiveAndBelow (index, files.size()))
        {
            document.replaceAllContent (files[index].loadFileAsString());
            nameEditor.setText (files[index].getFileNameWithoutExtension());
        }
    }

    void save()
    {
        const auto name = juce::File::createLegalFileName (nameEditor.getText().trim());

        if (name.isEmpty())
        {
            status.setColour (juce::Label::textColourId, juce::Colour (0xffef5350));
            status.setText ("give the effect a name before saving", juce::dontSendNotification);
            return;
        }

        MotiveScriptLibrary::getDirectory().getChildFile (name + ".ms")
            .replaceWithText (document.getAllContent());

        refreshLibrary();

        // reselect what we just saved
        for (int i = 0; i < library.getNumItems(); ++i)
            if (library.getItemText (i) == name)
                library.setSelectedId (library.getItemId (i), juce::dontSendNotification);

        status.setColour (juce::Label::textColourId, juce::Colour (0xff2ecc71));
        status.setText ("saved \"" + name + "\" — it's now in every track's FX menu",
                        juce::dontSendNotification);
    }

    void checkCode()
    {
        const auto result = motivescript::Compiler::compile (document.getAllContent().toStdString());

        if (result.program != nullptr)
        {
            juce::StringArray paramNames;
            for (const auto& p : result.program->params)
                paramNames.add (p.name);

            status.setColour (juce::Label::textColourId, juce::Colour (0xff2ecc71));
            status.setText ("OK — " + juce::String (result.program->stereo ? "stereo" : "mono")
                                + (paramNames.isEmpty() ? juce::String()
                                                        : ", params: " + paramNames.joinIntoString (", ")),
                            juce::dontSendNotification);
        }
        else
        {
            status.setColour (juce::Label::textColourId, juce::Colour (0xffef5350));
            status.setText ("line " + juce::String (result.errorLine) + ": " + result.error,
                            juce::dontSendNotification);
        }
    }

    void codeDocumentTextInserted (const juce::String&, int) override  { startTimer (300); }
    void codeDocumentTextDeleted (int, int) override                   { startTimer (300); }
    void timerCallback() override                                      { stopTimer(); checkCode(); }

    juce::ComboBox library;
    juce::TextEditor nameEditor;
    juce::TextButton saveButton { "Save" }, newButton { "New" };
    juce::CodeDocument document;
    juce::CPlusPlusCodeTokeniser tokeniser;
    std::unique_ptr<juce::CodeEditorComponent> codeEditor;
    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScriptLibraryPanel)
};
