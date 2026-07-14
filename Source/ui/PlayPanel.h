#pragma once

// Included from MainComponent.cpp after the tracktion example UI kit and DrumConfig.h.
//
// The always-there instrument strip with three modes:
//   Keys  — piano keyboard (mouse or musical typing), centered
//   Drums — the drum-kit photo with key badges on each piece (from the React app)
//   Pads  — a 4x4 pad grid, MPC style
// All modes feed MIDI through the same virtual device, so whatever you play
// records to whichever track that device is assigned to.

//==============================================================================
// Edit names, trigger keys, and sounds for one 16-pad bank.
class DrumCustomizeComponent : public Component
{
public:
    DrumCustomizeComponent (const DrumBank& bankToEdit, std::function<void (DrumBank)> onSaveCallback)
        : bank (bankToEdit), onSave (std::move (onSaveCallback))
    {
        for (int i = 0; i < DrumBank::numPads; ++i)
        {
            auto& pad = bank.pads[(size_t) i];
            auto row = std::make_unique<Row>();

            row->name.setText (pad.name, dontSendNotification);
            row->key.setText (pad.key, dontSendNotification);
            row->key.setInputRestrictions (1, "abcdefghijklmnopqrstuvwxyz0123456789");
            row->key.setJustification (Justification::centred);

            row->sound.setButtonText (soundLabel (pad.sound));
            row->sound.onClick = [this, i] { chooseSound (i); };

            holder.addAndMakeVisible (row->name);
            holder.addAndMakeVisible (row->key);
            holder.addAndMakeVisible (row->sound);
            rows.add (row.release());
        }

        viewport.setViewedComponent (&holder, false);
        addAndMakeVisible (viewport);

        saveButton.onClick = [this]
        {
            for (int i = 0; i < DrumBank::numPads; ++i)
            {
                auto& pad = bank.pads[(size_t) i];
                pad.name = rows[i]->name.getText().trim();
                pad.key = rows[i]->key.getText().trim().toLowerCase();
            }

            onSave (bank);

            if (auto* window = findParentComponentOfClass<DialogWindow>())
                window->exitModalState (1);
        };
        addAndMakeVisible (saveButton);

        setSize (440, 480);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        saveButton.setBounds (r.removeFromBottom (28).withSizeKeepingCentre (120, 26));
        r.removeFromBottom (6);
        viewport.setBounds (r);

        const int rowHeight = 30;
        holder.setSize (r.getWidth() - viewport.getScrollBarThickness(),
                        rowHeight * DrumBank::numPads);

        for (int i = 0; i < rows.size(); ++i)
        {
            auto rowArea = Rectangle<int> (0, i * rowHeight, holder.getWidth(), rowHeight).reduced (0, 3);
            rows[i]->name.setBounds (rowArea.removeFromLeft (150));
            rowArea.removeFromLeft (6);
            rows[i]->key.setBounds (rowArea.removeFromLeft (34));
            rowArea.removeFromLeft (6);
            rows[i]->sound.setBounds (rowArea);
        }
    }

private:
    struct Row
    {
        TextEditor name, key;
        TextButton sound;
    };

    static String soundLabel (const String& sound)
    {
        return File::isAbsolutePath (sound) ? File (sound).getFileName() : sound;
    }

    void chooseSound (int padIndex)
    {
        PopupMenu m;

        int id = 1;
        for (const auto& name : DrumSounds::getAllNames())
            m.addItem (id++, name, true, bank.pads[(size_t) padIndex].sound == name);

        m.addSeparator();
        m.addItem (100, "Choose audio file...");

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (rows[padIndex]->sound),
                         [this, padIndex] (int result)
        {
            auto& pad = bank.pads[(size_t) padIndex];

            if (result >= 1 && result <= DrumSounds::getAllNames().size())
                pad.sound = DrumSounds::getAllNames()[result - 1];
            else if (result == 100)
            {
                FileChooser fc ("Choose a drum sample", File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
                if (fc.browseForFileToOpen())
                    pad.sound = fc.getResult().getFullPathName();
            }
            else
                return;

            rows[padIndex]->sound.setButtonText (soundLabel (pad.sound));
        });
    }

    DrumBank bank;
    std::function<void (DrumBank)> onSave;
    OwnedArray<Row> rows;
    Component holder;
    Viewport viewport;
    TextButton saveButton { "Save" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumCustomizeComponent)
};

//==============================================================================
// The drum-kit photo with a key badge on each piece.
class DrumKitView : public Component
{
public:
    DrumKitView()
    {
        photo = ImageCache::getFromMemory (BinaryData::DrumImage_jpeg, BinaryData::DrumImage_jpegSize);
    }

    std::function<void (int note, bool down)> onTrigger;

    void setBank (const DrumBank& b)    { bank = b; repaint(); }

    void flashNote (int note, bool down)
    {
        for (int i = 0; i < DrumBank::numPads; ++i)
            if (bank.pads[(size_t) i].note == note)
                litPads[(size_t) i] = down;

        repaint();
    }

    void paint (Graphics& g) override
    {
        const auto area = getImageArea();
        g.drawImage (photo, area, RectanglePlacement::centred);

        g.setFont (FontOptions (13.0f, Font::bold));

        for (int i = 0; i < DrumBank::numPads; ++i)
        {
            const auto& pad = bank.pads[(size_t) i];
            const auto centre = badgeCentre (i, area);
            const bool lit = litPads[(size_t) i];

            Rectangle<float> badge (28.0f, 28.0f);
            badge.setCentre (centre);

            g.setColour (lit ? Colour (0xffffb300) : Colours::black.withAlpha (0.72f));
            g.fillEllipse (badge);
            g.setColour (lit ? Colours::black : Colours::white);
            g.drawEllipse (badge, 1.0f);
            g.drawText (pad.key.toUpperCase(), badge, Justification::centred);

            g.setColour (Colours::white.withAlpha (0.85f));
            g.setFont (FontOptions (9.5f));
            g.drawText (pad.name, Rectangle<float> (70.0f, 12.0f).withCentre (centre.translated (0.0f, 22.0f)),
                        Justification::centred);
            g.setFont (FontOptions (13.0f, Font::bold));
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        pressedPad = findPadNear (e.position);

        if (pressedPad >= 0 && onTrigger != nullptr)
            onTrigger (bank.pads[(size_t) pressedPad].note, true);
    }

    void mouseUp (const MouseEvent&) override
    {
        if (pressedPad >= 0 && onTrigger != nullptr)
            onTrigger (bank.pads[(size_t) pressedPad].note, false);

        pressedPad = -1;
    }

private:
    Rectangle<float> getImageArea() const
    {
        return getLocalBounds().toFloat().reduced (4.0f);
    }

    Point<float> badgeCentre (int i, const Rectangle<float>& area) const
    {
        // anchors are relative to the drawn photo, matching the React app
        const auto placement = RectanglePlacement (RectanglePlacement::centred)
                                   .appliedTo (photo.getBounds().toFloat(), area);
        const auto& pad = bank.pads[(size_t) i];
        return { placement.getX() + pad.x * placement.getWidth(),
                 placement.getY() + pad.y * placement.getHeight() };
    }

    int findPadNear (Point<float> pos) const
    {
        const auto area = getImageArea();

        for (int i = 0; i < DrumBank::numPads; ++i)
            if (pos.getDistanceFrom (badgeCentre (i, area)) < 24.0f)
                return i;

        return -1;
    }

    Image photo;
    DrumBank bank;
    std::array<bool, DrumBank::numPads> litPads {};
    int pressedPad = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitView)
};

//==============================================================================
// The 4x4 pad grid.
class PadGridView : public Component
{
public:
    PadGridView() = default;

    std::function<void (int note, bool down)> onTrigger;

    void setBank (const DrumBank& b)    { bank = b; repaint(); }

    void flashNote (int note, bool down)
    {
        for (int i = 0; i < DrumBank::numPads; ++i)
            if (bank.pads[(size_t) i].note == note)
                litPads[(size_t) i] = down;

        repaint();
    }

    void paint (Graphics& g) override
    {
        for (int i = 0; i < DrumBank::numPads; ++i)
        {
            const auto cell = cellBounds (i).toFloat();
            const bool lit = litPads[(size_t) i];

            g.setColour (lit ? Colour (0xffffb300) : Colour (0xff2a2e35));
            g.fillRoundedRectangle (cell, 6.0f);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (cell, 6.0f, 1.0f);

            g.setColour (lit ? Colours::black : Colours::white);
            g.setFont (FontOptions (13.0f, Font::bold));
            g.drawText (bank.pads[(size_t) i].key.toUpperCase(),
                        cell.withTrimmedBottom (cell.getHeight() * 0.4f), Justification::centred);

            g.setColour (lit ? Colours::black.withAlpha (0.8f) : Colours::white.withAlpha (0.6f));
            g.setFont (FontOptions (10.0f));
            g.drawText (bank.pads[(size_t) i].name,
                        cell.withTrimmedTop (cell.getHeight() * 0.55f).reduced (2.0f, 0.0f),
                        Justification::centred);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        pressedPad = findPadAt (e.position.toInt());

        if (pressedPad >= 0 && onTrigger != nullptr)
            onTrigger (bank.pads[(size_t) pressedPad].note, true);
    }

    void mouseUp (const MouseEvent&) override
    {
        if (pressedPad >= 0 && onTrigger != nullptr)
            onTrigger (bank.pads[(size_t) pressedPad].note, false);

        pressedPad = -1;
    }

private:
    Rectangle<int> cellBounds (int i) const
    {
        // 4x4, centred, square-ish cells
        const int cols = 4, rows = 4, gap = 4;
        const int cellSize = jmin ((getWidth() - gap * (cols + 1)) / cols,
                                   (getHeight() - gap * (rows + 1)) / rows);
        const int x0 = (getWidth() - (cellSize * cols + gap * (cols - 1))) / 2;
        const int y0 = (getHeight() - (cellSize * rows + gap * (rows - 1))) / 2;

        return { x0 + (i % cols) * (cellSize + gap),
                 y0 + (i / cols) * (cellSize + gap),
                 cellSize, cellSize };
    }

    int findPadAt (Point<int> pos) const
    {
        for (int i = 0; i < DrumBank::numPads; ++i)
            if (cellBounds (i).contains (pos))
                return i;

        return -1;
    }

    DrumBank bank;
    std::array<bool, DrumBank::numPads> litPads {};
    int pressedPad = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGridView)
};

//==============================================================================
class PlayPanel : public Component,
                  private MidiKeyboardState::Listener
{
public:
    enum Mode { keysMode = 0, drumsMode = 1, padsMode = 2 };

    PlayPanel()
    {
        keyboardState.addListener (this);
        keyboard.setAvailableRange (24, 96);
        keyboard.setKeyWidth (22.0f);
        keyboard.setOctaveForMiddleC (4);
        addAndMakeVisible (keyboard);

        kitBank = DrumBank::load ("kit");
        padsBank = DrumBank::load ("pads");

        kitView.setBank (kitBank);
        kitView.onTrigger = [this] (int note, bool down) { trigger (note, down); };
        addChildComponent (kitView);

        padGrid.setBank (padsBank);
        padGrid.onTrigger = [this] (int note, bool down) { trigger (note, down); };
        addChildComponent (padGrid);

        modeBox.addItem ("Keys", 1);
        modeBox.addItem ("Drums", 2);
        modeBox.addItem ("Pads", 3);
        modeBox.setSelectedId (1, dontSendNotification);
        modeBox.onChange = [this] { modeChanged(); };
        addAndMakeVisible (modeBox);

        customizeButton.onClick = [this] { openCustomize(); };
        addChildComponent (customizeButton);

        hint.setText ("play:  A S D F G H J K   sharps:  W E T Y U   octave:  Z / X", dontSendNotification);
        hint.setJustificationType (Justification::centredRight);
        hint.setFont (FontOptions (11.5f));
        hint.setColour (Label::textColourId, Colours::white.withAlpha (0.45f));
        addAndMakeVisible (hint);

        octaveLabel.setJustificationType (Justification::centred);
        octaveLabel.setFont (FontOptions (11.5f, Font::bold));
        addAndMakeVisible (octaveLabel);
        updateOctaveLabel();
    }

    ~PlayPanel() override
    {
        keyboardState.removeListener (this);
    }

    //==============================================================================
    std::function<void (const MidiMessage&)> onMidi;   // into the virtual MIDI device
    std::function<void (int)> onModeChange;            // retarget device + rebuild sampler
    std::function<void (int)> onBankChanged;           // customize saved

    int getMode() const                 { return modeBox.getSelectedId() - 1; }
    int getPreferredHeight() const      { return getMode() == keysMode ? 112 : 240; }

    const DrumBank& getBank (int mode) const    { return mode == drumsMode ? kitBank : padsBank; }

    //==============================================================================
    // computer-keyboard playing: called from the app's keyStateChanged
    bool pollComputerKeys()
    {
        bool anyChanged = false;

        if (getMode() == keysMode)
        {
            static constexpr struct { char key; int offset; } noteKeys[] =
            {
                { 'a', 0 }, { 'w', 1 }, { 's', 2 }, { 'e', 3 }, { 'd', 4 }, { 'f', 5 },
                { 't', 6 }, { 'g', 7 }, { 'y', 8 }, { 'h', 9 }, { 'u', 10 }, { 'j', 11 }, { 'k', 12 }
            };

            for (size_t i = 0; i < std::size (noteKeys); ++i)
            {
                const bool down = isCharDown (noteKeys[i].key);
                const bool was = (pressedKeyBits & (1u << i)) != 0;

                if (down != was)
                {
                    pressedKeyBits ^= (1u << i);
                    const int note = jlimit (0, 127, baseOctaveNote + noteKeys[i].offset);

                    if (down)  keyboardState.noteOn (1, note, 0.9f);
                    else       keyboardState.noteOff (1, note, 0.0f);

                    anyChanged = true;
                }
            }
        }
        else
        {
            const auto& bank = getBank (getMode());

            for (size_t i = 0; i < DrumBank::numPads; ++i)
            {
                const auto& key = bank.pads[i].key;
                if (key.isEmpty())
                    continue;

                const bool down = isCharDown ((char) key[0]);
                const bool was = (pressedKeyBits & (1u << i)) != 0;

                if (down != was)
                {
                    pressedKeyBits ^= (1u << i);
                    trigger (bank.pads[i].note, down);
                    anyChanged = true;
                }
            }
        }

        return anyChanged;
    }

    // true if this character plays something in the current mode (so app
    // shortcuts like 'r' = record don't fire while drumming)
    bool isMappedKey (juce_wchar c) const
    {
        if (getMode() == keysMode)
            return false;   // piano keys don't collide with any shortcut

        for (const auto& pad : getBank (getMode()).pads)
            if (pad.key.isNotEmpty() && pad.key[0] == CharacterFunctions::toLowerCase (c))
                return true;

        return false;
    }

    void shiftOctave (int delta)
    {
        keyboardState.allNotesOff (1);
        baseOctaveNote = jlimit (24, 84, baseOctaveNote + delta * 12);
        updateOctaveLabel();
    }

    void setBaseNote (int note)
    {
        keyboardState.allNotesOff (1);
        baseOctaveNote = jlimit (24, 84, note);
        updateOctaveLabel();
    }

    //==============================================================================
    void resized() override
    {
        auto r = getLocalBounds().reduced (4);

        auto top = r.removeFromTop (22);
        modeBox.setBounds (top.removeFromLeft (110).reduced (1));
        octaveLabel.setBounds (top.removeFromLeft (64));
        customizeButton.setBounds (top.removeFromLeft (90).reduced (1));
        hint.setBounds (top);

        // centre the piano at its natural width
        const int keyboardWidth = jmin (r.getWidth(), (int) std::ceil (keyboard.getTotalKeyboardWidth()));
        keyboard.setBounds (r.withSizeKeepingCentre (keyboardWidth, r.getHeight()));

        kitView.setBounds (r);
        padGrid.setBounds (r);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff1b1e22));
    }

private:
    void modeChanged()
    {
        const int mode = getMode();

        keyboard.setVisible (mode == keysMode);
        octaveLabel.setVisible (mode == keysMode);
        hint.setVisible (mode == keysMode);
        kitView.setVisible (mode == drumsMode);
        padGrid.setVisible (mode == padsMode);
        customizeButton.setVisible (mode != keysMode);

        keyboardState.allNotesOff (1);
        pressedKeyBits = 0;

        if (onModeChange != nullptr)
            onModeChange (mode);
    }

    void trigger (int note, bool down)
    {
        if (onMidi != nullptr)
            onMidi (down ? MidiMessage::noteOn (1, note, 0.9f)
                         : MidiMessage::noteOff (1, note, 0.0f));

        kitView.flashNote (note, down);
        padGrid.flashNote (note, down);
    }

    void openCustomize()
    {
        const int mode = getMode();

        auto content = std::make_unique<DrumCustomizeComponent> (getBank (mode),
            [this, mode] (DrumBank edited)
            {
                if (mode == drumsMode) { kitBank = edited; kitView.setBank (edited); }
                else                   { padsBank = edited; padGrid.setBank (edited); }

                edited.save (mode == drumsMode ? "kit" : "pads");

                if (onBankChanged != nullptr)
                    onBankChanged (mode);
            });

        DialogWindow::LaunchOptions o;
        o.content.setOwned (content.release());
        o.dialogTitle = "Customize " + String (mode == drumsMode ? "Drums" : "Pads");
        o.dialogBackgroundColour = Colour (0xff23272b);
        o.escapeKeyTriggersCloseButton = true;
        o.resizable = false;
        o.launchAsync();
    }

    static bool isCharDown (char c)
    {
        return KeyPress::isKeyCurrentlyDown (c)
            || KeyPress::isKeyCurrentlyDown (CharacterFunctions::toUpperCase (c));
    }

    void handleNoteOn (MidiKeyboardState*, int channel, int note, float velocity) override
    {
        if (onMidi != nullptr)
            onMidi (MidiMessage::noteOn (channel, note, velocity));
    }

    void handleNoteOff (MidiKeyboardState*, int channel, int note, float velocity) override
    {
        if (onMidi != nullptr)
            onMidi (MidiMessage::noteOff (channel, note, velocity));
    }

    void updateOctaveLabel()
    {
        octaveLabel.setText ("oct: C" + String (baseOctaveNote / 12 - 1), dontSendNotification);
    }

    MidiKeyboardState keyboardState;
    MidiKeyboardComponent keyboard { keyboardState, MidiKeyboardComponent::horizontalKeyboard };
    DrumKitView kitView;
    PadGridView padGrid;
    DrumBank kitBank, padsBank;

    ComboBox modeBox;
    TextButton customizeButton { "Customize" };
    Label hint, octaveLabel;
    int baseOctaveNote = 60;   // C4
    uint32 pressedKeyBits = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlayPanel)
};
