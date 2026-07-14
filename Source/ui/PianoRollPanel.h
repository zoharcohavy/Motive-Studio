#pragma once

// Included from MainComponent.cpp after the tracktion example UI kit.

//==============================================================================
// The note grid: click empty space to add a note, drag notes to move them,
// drag a note's right edge to resize, right-click to delete.
class PianoRollGrid : public Component
{
public:
    PianoRollGrid() = default;

    static constexpr int keyboardWidth = 56;
    static constexpr int rowHeight = 12;
    static constexpr int lowestNote = 24;    // C1
    static constexpr int highestNote = 96;   // C7
    static constexpr double pixelsPerBeat = 36.0;
    static constexpr double snapBeats = 0.25;   // 1/16th at 4/4

    void setClip (te::MidiClip* c)
    {
        clip = c;
        updateSize();
        repaint();
    }

    void updateSize()
    {
        const auto beats = clip != nullptr ? getClipLengthBeats() : 16.0;
        setSize (keyboardWidth + roundToInt (beats * pixelsPerBeat) + 20,
                 (highestNote - lowestNote + 1) * rowHeight);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff17191d));

        if (clip == nullptr)
            return;

        const auto lengthBeats = getClipLengthBeats();

        // rows: shade the black-key lanes, label the Cs
        for (int note = lowestNote; note <= highestNote; ++note)
        {
            const auto y = noteToY (note);
            const bool isBlack = MidiMessage::isMidiNoteBlack (note);

            g.setColour (isBlack ? Colours::black.withAlpha (0.25f)
                                 : Colours::white.withAlpha (0.03f));
            g.fillRect (keyboardWidth, y, getWidth() - keyboardWidth, rowHeight);

            g.setColour (isBlack ? Colour (0xff26292e) : Colour (0xffb9bec5));
            g.fillRect (0, y, keyboardWidth - 4, rowHeight - 1);

            if (note % 12 == 0)
            {
                g.setColour (isBlack ? Colours::white : Colours::black);
                g.setFont (FontOptions (9.5f));
                g.drawText ("C" + String (note / 12 - 1), 4, y, keyboardWidth - 10, rowHeight,
                            Justification::centredLeft);
            }
        }

        // beat lines (heavy every bar)
        for (double beat = 0.0; beat <= lengthBeats; beat += 1.0)
        {
            const auto x = beatToX (beat);
            g.setColour (Colours::white.withAlpha (std::fmod (beat, 4.0) == 0.0 ? 0.25f : 0.08f));
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        }

        // notes
        for (auto note : clip->getSequence().getNotes())
        {
            const auto x = beatToX (note->getStartBeat().inBeats());
            const auto w = jmax (4, beatToX (note->getStartBeat().inBeats() + note->getLengthBeats().inBeats()) - x);
            const auto y = noteToY (note->getNoteNumber());

            Rectangle<float> r ((float) x, (float) y + 1.0f, (float) w, (float) rowHeight - 2.0f);
            g.setColour (Colour (0xff7e57c2).brighter (note == draggedNote ? 0.4f : 0.0f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (clip == nullptr)
            return;

        auto* um = &clip->edit.getUndoManager();
        draggedNote = findNoteAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (draggedNote != nullptr)
            {
                clip->getSequence().removeNote (*draggedNote, um);
                draggedNote = nullptr;
                repaint();
            }
            return;
        }

        if (draggedNote != nullptr)
        {
            // near the right edge means resize, otherwise move
            const auto endX = beatToX (draggedNote->getStartBeat().inBeats()
                                       + draggedNote->getLengthBeats().inBeats());
            resizing = std::abs (e.position.x - (float) endX) < 6.0f;
            dragOffsetBeats = xToBeat (e.x) - draggedNote->getStartBeat().inBeats();
        }
        else if (e.position.x >= (float) keyboardWidth)
        {
            // add a note where the user clicked, then let them drag its length
            const auto startBeat = snap (xToBeat (e.x));
            const auto pitch = yToNote (e.y);

            draggedNote = clip->getSequence().addNote (pitch,
                                                       te::BeatPosition::fromBeats (startBeat),
                                                       te::BeatDuration::fromBeats (snapBeats),
                                                       100, 0, um);
            resizing = true;
            dragOffsetBeats = 0.0;
            repaint();
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (clip == nullptr || draggedNote == nullptr)
            return;

        auto* um = &clip->edit.getUndoManager();

        if (resizing)
        {
            const auto newLength = jmax (snapBeats,
                                         snap (xToBeat (e.x)) - draggedNote->getStartBeat().inBeats());
            draggedNote->setStartAndLength (draggedNote->getStartBeat(),
                                            te::BeatDuration::fromBeats (newLength), um);
        }
        else
        {
            const auto newStart = jmax (0.0, snap (xToBeat (e.x) - dragOffsetBeats));
            draggedNote->setStartAndLength (te::BeatPosition::fromBeats (newStart),
                                            draggedNote->getLengthBeats(), um);
            draggedNote->setNoteNumber (yToNote (e.y), um);
        }

        repaint();
    }

    void mouseUp (const MouseEvent&) override
    {
        draggedNote = nullptr;
        resizing = false;
        repaint();
    }

private:
    double getClipLengthBeats() const
    {
        auto& ts = clip->edit.tempoSequence;
        return (ts.toBeats (clip->getPosition().getEnd())
                - ts.toBeats (clip->getPosition().getStart())).inBeats();
    }

    int beatToX (double beat) const     { return keyboardWidth + roundToInt (beat * pixelsPerBeat); }
    double xToBeat (int x) const        { return (x - keyboardWidth) / pixelsPerBeat; }
    int noteToY (int note) const        { return (highestNote - note) * rowHeight; }
    int yToNote (float y) const         { return jlimit (lowestNote, highestNote,
                                                         highestNote - (int) (y / (float) rowHeight)); }
    static double snap (double beat)    { return std::round (beat / snapBeats) * snapBeats; }

    te::MidiNote* findNoteAt (Point<float> pos) const
    {
        if (clip == nullptr)
            return nullptr;

        for (auto note : clip->getSequence().getNotes())
        {
            const auto x = beatToX (note->getStartBeat().inBeats());
            const auto w = jmax (4, beatToX (note->getStartBeat().inBeats()
                                             + note->getLengthBeats().inBeats()) - x);

            Rectangle<float> r ((float) x - 2.0f, (float) noteToY (note->getNoteNumber()),
                                (float) w + 6.0f, (float) rowHeight);
            if (r.contains (pos))
                return note;
        }

        return nullptr;
    }

    te::MidiClip::Ptr clip;   // ref-counted: stays valid even if removed remotely
    te::MidiNote* draggedNote = nullptr;
    bool resizing = false;
    double dragOffsetBeats = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollGrid)
};

//==============================================================================
class PianoRollPanel : public Component
{
public:
    PianoRollPanel()
    {
        viewport.setViewedComponent (&grid, false);
        addAndMakeVisible (viewport);

        hint.setText ("Select a MIDI clip on the timeline (or click + MIDI to create one), and its notes appear here.",
                      dontSendNotification);
        hint.setJustificationType (Justification::centred);
        hint.setColour (Label::textColourId, Colours::white.withAlpha (0.45f));
        addChildComponent (hint);

        setClip (nullptr);
    }

    void setClip (te::MidiClip* clip)
    {
        grid.setClip (clip);
        hint.setVisible (clip == nullptr);
        viewport.setVisible (clip != nullptr);

        if (clip != nullptr)
            viewport.setViewPosition (0, PianoRollGrid::rowHeight
                                             * (PianoRollGrid::highestNote - 72));   // start around C4
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().reduced (4));
        hint.setBounds (getLocalBounds());
        grid.updateSize();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff1b1e22));
    }

private:
    Viewport viewport;
    PianoRollGrid grid;
    Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
};
