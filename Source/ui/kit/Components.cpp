/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com
*/

#pragma once

namespace te = tracktion;
using namespace std::literals;

//==============================================================================
class PluginTreeBase
{
public:
    virtual ~PluginTreeBase() = default;
    virtual String getUniqueName() const = 0;

    void addSubItem (PluginTreeBase* itm)   { subitems.add (itm);       }
    int getNumSubItems()                    { return subitems.size();   }
    PluginTreeBase* getSubItem (int idx)    { return subitems[idx];     }

private:
    OwnedArray<PluginTreeBase> subitems;
};

//==============================================================================
class PluginTreeItem : public PluginTreeBase
{
public:
    PluginTreeItem (const PluginDescription&);
    PluginTreeItem (const String& uniqueId, const String& name, const String& xmlType, bool isSynth, bool isPlugin);

    te::Plugin::Ptr create (te::Edit&);

    String getUniqueName() const override
    {
        if (desc.fileOrIdentifier.startsWith (te::RackType::getRackPresetPrefix()))
            return desc.fileOrIdentifier;

        return desc.createIdentifierString();
    }

    PluginDescription desc;
    String xmlType;
    bool isPlugin = true;

    JUCE_LEAK_DETECTOR (PluginTreeItem)
};

//==============================================================================
class PluginTreeGroup : public PluginTreeBase
{
public:
    PluginTreeGroup (te::Edit&, KnownPluginList::PluginTree&, te::Plugin::Type);
    PluginTreeGroup (const String&);

    String getUniqueName() const override           { return name; }

    String name;

private:
    void populateFrom (KnownPluginList::PluginTree&);
    void createBuiltInItems (int& num, te::Plugin::Type);

    JUCE_LEAK_DETECTOR (PluginTreeGroup)
};

//==============================================================================
PluginTreeItem::PluginTreeItem (const juce::PluginDescription& d)
    : desc (d), xmlType (te::ExternalPlugin::xmlTypeName), isPlugin (true)
{
    jassert (xmlType.isNotEmpty());
}

PluginTreeItem::PluginTreeItem (const juce::String& uniqueId, const juce::String& name,
                                const juce::String& xmlType_, bool isSynth, bool isPlugin_)
    : xmlType (xmlType_), isPlugin (isPlugin_)
{
    jassert (xmlType.isNotEmpty());
    desc.name = name;
    desc.fileOrIdentifier = uniqueId;
    desc.pluginFormatName = (uniqueId.endsWith ("_trkbuiltin") || xmlType == te::RackInstance::xmlTypeName)
                                ? juce::String (te::PluginManager::builtInPluginFormatName) : juce::String();
    desc.category = xmlType;
    desc.isInstrument = isSynth;
}

te::Plugin::Ptr PluginTreeItem::create (te::Edit& ed)
{
    return ed.getPluginCache().createNewPlugin (xmlType, desc);
}

//==============================================================================
PluginTreeGroup::PluginTreeGroup (te::Edit& edit, KnownPluginList::PluginTree& tree, te::Plugin::Type types)
    : name ("Plugins")
{
    {
        int num = 1;

        auto builtinFolder = new PluginTreeGroup (TRANS("Builtin Plugins"));
        addSubItem (builtinFolder);
        builtinFolder->createBuiltInItems (num, types);
    }

    {
        auto racksFolder = new PluginTreeGroup (TRANS("Plugin Racks"));
        addSubItem (racksFolder);

        racksFolder->addSubItem (new PluginTreeItem (String (te::RackType::getRackPresetPrefix()) + "-1",
                                                     TRANS("Create New Empty Rack"),
                                                     te::RackInstance::xmlTypeName, false, false));

        int i = 0;
        for (auto rf : edit.getRackList().getTypes())
            racksFolder->addSubItem (new PluginTreeItem ("RACK__" + String (i++), rf->rackName,
                                                         te::RackInstance::xmlTypeName, false, false));
    }

    populateFrom (tree);
}

PluginTreeGroup::PluginTreeGroup (const String& s)  : name (s)
{
    jassert (name.isNotEmpty());
}

void PluginTreeGroup::populateFrom (KnownPluginList::PluginTree& tree)
{
    for (auto subTree : tree.subFolders)
    {
        if (subTree->plugins.size() > 0 || subTree->subFolders.size() > 0)
        {
            auto fs = new PluginTreeGroup (subTree->folder);
            addSubItem (fs);

            fs->populateFrom (*subTree);
        }
    }

    for (const auto& pd : tree.plugins)
        addSubItem (new PluginTreeItem (pd));
}


template<class FilterClass>
void addInternalPlugin (PluginTreeBase& item, int& num, bool synth = false)
{
    item.addSubItem (new PluginTreeItem (String (num++) + "_trkbuiltin",
                                         TRANS (FilterClass::getPluginName()),
                                         FilterClass::xmlTypeName, synth, false));
}

void PluginTreeGroup::createBuiltInItems (int& num, te::Plugin::Type types)
{
    addInternalPlugin<te::VolumeAndPanPlugin> (*this, num);
    addInternalPlugin<te::LevelMeterPlugin> (*this, num);
    addInternalPlugin<te::EqualiserPlugin> (*this, num);
    addInternalPlugin<te::ReverbPlugin> (*this, num);
    addInternalPlugin<te::DelayPlugin> (*this, num);
    addInternalPlugin<te::ChorusPlugin> (*this, num);
    addInternalPlugin<te::PhaserPlugin> (*this, num);
    addInternalPlugin<te::CompressorPlugin> (*this, num);
    addInternalPlugin<te::PitchShiftPlugin> (*this, num);
    addInternalPlugin<te::LowPassPlugin> (*this, num);
    addInternalPlugin<te::MidiModifierPlugin> (*this, num);
    addInternalPlugin<te::MidiPatchBayPlugin> (*this, num);
    addInternalPlugin<te::PatchBayPlugin> (*this, num);
    addInternalPlugin<te::AuxSendPlugin> (*this, num);
    addInternalPlugin<te::AuxReturnPlugin> (*this, num);
    addInternalPlugin<te::TextPlugin> (*this, num);
    addInternalPlugin<te::FreezePointPlugin> (*this, num);

   #if TRACKTION_ENABLE_REWIRE
    addInternalPlugin<te::ReWirePlugin> (*this, num, true);
   #endif

    // Motive's own built-ins
    addInternalPlugin<WahPlugin> (*this, num);
    addInternalPlugin<MotiveScriptPlugin> (*this, num);

    if (types == te::Plugin::Type::allPlugins)
    {
        addInternalPlugin<te::SamplerPlugin> (*this, num, true);
        addInternalPlugin<te::FourOscPlugin> (*this, num, true);
    }

    addInternalPlugin<te::InsertPlugin> (*this, num);

   #if ENABLE_INTERNAL_PLUGINS
    for (auto& d : PluginTypeBase::getAllPluginDescriptions())
        if (isPluginAuthorised (d))
            addSubItem (new PluginTreeItem (d));
   #endif
}

//==============================================================================
class PluginMenu : public PopupMenu
{
public:
    PluginMenu() = default;

    PluginMenu (PluginTreeGroup& node, std::function<void (PluginTreeItem*)> callback = nullptr)
    {
        for (int i = 0; i < node.getNumSubItems(); ++i)
            if (auto subNode = dynamic_cast<PluginTreeGroup*> (node.getSubItem (i)))
                addSubMenu (subNode->name, PluginMenu (*subNode, callback), true);

        for (int i = 0; i < node.getNumSubItems(); ++i)
        {
            if (auto subType = dynamic_cast<PluginTreeItem*> (node.getSubItem (i)))
            {
                if (callback)
                    addItem (subType->desc.name, [subType, callback] { callback (subType); });
                else
                    addItem (subType->getUniqueName().hashCode(), subType->desc.name, true, false);
            }
        }
    }

    static PluginTreeItem* findType (PluginTreeGroup& node, int hash)
    {
        for (int i = 0; i < node.getNumSubItems(); ++i)
            if (auto subNode = dynamic_cast<PluginTreeGroup*> (node.getSubItem (i)))
                if (auto* t = findType (*subNode, hash))
                    return t;

        for (int i = 0; i < node.getNumSubItems(); ++i)
            if (auto t = dynamic_cast<PluginTreeItem*> (node.getSubItem (i)))
                if (t->getUniqueName().hashCode() == hash)
                    return t;

        return nullptr;
    }

    PluginTreeItem* runMenu (PluginTreeGroup& node)
    {
        int res = show();

        if (res == 0)
            return nullptr;

        return findType (node, res);
    }
};

//==============================================================================
inline te::Plugin::Ptr showMenuAndCreatePlugin (te::Edit& edit)
{
    if (auto tree = EngineHelpers::createPluginTree (edit.engine))
    {
        PluginTreeGroup root (edit, *tree, te::Plugin::Type::allPlugins);
        PluginMenu m (root);

        if (auto type = m.runMenu (root))
            return type->create (edit);
    }

    return {};
}

inline void showMenuAndCreatePluginAsync (te::Track::Ptr destTrack, int index,
                                          std::function<void (te::Plugin::Ptr)> onInserted)
{
    if (auto tree = std::shared_ptr<juce::KnownPluginList::PluginTree> (EngineHelpers::createPluginTree (destTrack->edit.engine)))
    {
        auto root = std::make_shared<PluginTreeGroup> (destTrack->edit, *tree, te::Plugin::Type::allPlugins);
        PluginMenu m (*root,
                      [tree, root, destTrack, index, onInserted] (PluginTreeItem* selectedItem)
                      {
                          if (auto newPlugin = selectedItem->create (destTrack->edit))
                          {
                              destTrack->pluginList.insertPlugin (newPlugin, index, nullptr);

                              if (onInserted)
                                  onInserted (newPlugin);
                          }
                      });
        m.showMenuAsync({});
    }
}


//==============================================================================
ClipComponent::ClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : editViewState (evs), clip (c)
{
}

void ClipComponent::paint (Graphics& g)
{
    g.fillAll (clip->getColour().withAlpha (0.5f));
    g.setColour (Colours::black);
    g.drawRect (getLocalBounds());

    if (editViewState.selectionManager.isSelected (clip.get()))
    {
        g.setColour (Colours::red);
        g.drawRect (getLocalBounds(), 2);
    }
}

void ClipComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (clip.get());
}

void ClipComponent::mouseDoubleClick (const MouseEvent&)
{
    // double-clicking a MIDI clip opens it in the piano roll
    if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        if (editViewState.onOpenMidiClip != nullptr)
            editViewState.onOpenMidiClip (*mc);
}

//==============================================================================
AudioClipComponent::AudioClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : ClipComponent (evs, c)
{
    updateThumbnail();
}

void AudioClipComponent::paint (Graphics& g)
{
    ClipComponent::paint (g);

    if (editViewState.drawWaveforms && thumbnail != nullptr)
        drawWaveform (g, *getWaveAudioClip(), *thumbnail, Colours::black.withAlpha (0.5f),
                      0, getWidth(), 0, getHeight(), 0);
}

void AudioClipComponent::drawWaveform (Graphics& g, te::AudioClipBase& c, te::SmartThumbnail& thumb, Colour colour,
                                       int left, int right, int y, int h, int xOffset)
{
    auto getTimeRangeForDrawing = [this] (const int l, const int r) -> tracktion::TimeRange
    {
        if (auto p = getParentComponent())
        {
            auto t1 = editViewState.xToTime (l, p->getWidth());
            auto t2 = editViewState.xToTime (r, p->getWidth());

            return { t1, t2 };
        }

        return {};
    };

    jassert (left <= right);
    const auto gain = c.getGain();
    const auto pan = thumb.getNumChannels() == 1 ? 0.0f : c.getPan();

    const float pv = pan * gain;
    const float gainL = (gain - pv);
    const float gainR = (gain + pv);

    const bool usesTimeStretchedProxy = c.usesTimeStretchedProxy();

    const auto clipPos = c.getPosition();
    auto offset = clipPos.getOffset();
    auto speedRatio = c.getSpeedRatio();

    g.setColour (colour);

    if (usesTimeStretchedProxy)
    {
        const Rectangle<int> area (left + xOffset, y, right - left, h);

        if (! thumb.isOutOfDate())
        {
            drawChannels (g, thumb, area,
                          getTimeRangeForDrawing (left, right),
                          c.isLeftChannelActive(), c.isRightChannelActive(),
                          gainL, gainR);
        }
    }
    else if (c.getLoopLength() == 0s)
    {
        auto region = getTimeRangeForDrawing (left, right);

        auto t1 = (region.getStart() + offset) * speedRatio;
        auto t2 = (region.getEnd()   + offset) * speedRatio;

        drawChannels (g, thumb,
                      { left + xOffset, y, right - left, h },
                      { t1, t2 },
                      c.isLeftChannelActive(), c.isRightChannelActive(),
                      gainL, gainR);
    }
}

void AudioClipComponent::drawChannels (Graphics& g, te::SmartThumbnail& thumb, Rectangle<int> area,
                                       te::TimeRange time, bool useLeft, bool useRight,
                                       float leftGain, float rightGain)
{
    if (useLeft && useRight && thumb.getNumChannels() > 1)
    {
        thumb.drawChannel (g, area.removeFromTop (area.getHeight() / 2), time, 0, leftGain);
        thumb.drawChannel (g, area, time, 1, rightGain);
    }
    else if (useLeft)
    {
        thumb.drawChannel (g, area, time, 0, leftGain);
    }
    else if (useRight)
    {
        thumb.drawChannel (g, area, time, 1, rightGain);
    }
}

void AudioClipComponent::updateThumbnail()
{
    if (auto* wac = getWaveAudioClip())
    {
        te::AudioFile af (wac->getAudioFile());

        if (af.getFile().existsAsFile() || (! wac->usesSourceFile()))
        {
            if (af.isValid())
            {
                const te::AudioFile proxy ((wac->hasAnyTakes() && wac->isShowingTakes()) ? wac->getAudioFile() : wac->getPlaybackFile());

                if (thumbnail == nullptr)
                    thumbnail = std::make_unique<te::SmartThumbnail> (wac->edit.engine, proxy, *this, &wac->edit);
                else
                    thumbnail->setNewFile (proxy);
            }
            else
            {
                thumbnail = nullptr;
            }
        }
    }
}

void drawMidiClip (juce::Graphics& g, te::MidiClip& mc, juce::Rectangle<int> r, te::TimeRange tr)
{
    auto timeToX = [width = r.getWidth(), tr] (auto time)
    {
        return juce::roundToInt (((time - tr.getStart()) * width) / (tr.getLength()));
    };

    for (auto n : mc.getSequence().getNotes())
    {
        auto sBeat = mc.getStartBeat() + toDuration (n->getStartBeat());
        auto eBeat = mc.getStartBeat() + toDuration (n->getEndBeat());

        auto s = mc.edit.tempoSequence.toTime (sBeat);
        auto e = mc.edit.tempoSequence.toTime (eBeat);

        auto t1 = (double) timeToX (s) - r.getX();
        auto t2 = (double) timeToX (e) - r.getX();

        double y = (1.0 - double (n->getNoteNumber()) / 127.0) * r.getHeight();

        g.setColour (Colours::white.withAlpha (n->getVelocity() / 127.0f));
        g.drawLine (float (t1), float (y), float (t2), float (y));
    }
}

//==============================================================================
MidiClipComponent::MidiClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : ClipComponent (evs, c)
{
}

void MidiClipComponent::paint (Graphics& g)
{
    ClipComponent::paint (g);

    if (auto mc = getMidiClip())
    {
        auto& seq = mc->getSequence();
        for (auto n : seq.getNotes())
        {
            auto sBeat = mc->getStartBeat() + toDuration (n->getStartBeat());
            auto eBeat = mc->getStartBeat() + toDuration (n->getEndBeat());

            auto s = editViewState.beatToTime (sBeat);
            auto e = editViewState.beatToTime (eBeat);

            if (auto p = getParentComponent())
            {
                auto t1 = (double) editViewState.timeToX (s, p->getWidth()) - getX();
                auto t2 = (double) editViewState.timeToX (e, p->getWidth()) - getX();

                double y = (1.0 - double (n->getNoteNumber()) / 127.0) * getHeight();

                g.setColour (Colours::white.withAlpha (n->getVelocity() / 127.0f));
                g.drawLine (float (t1), float (y), float (t2), float (y));
            }
        }
    }
}

//==============================================================================
RecordingClipComponent::RecordingClipComponent (te::Track::Ptr t, EditViewState& evs)
    : track (t), editViewState (evs)
{
    startTimerHz (10);
    initialiseThumbnailAndPunchTime();
}

void RecordingClipComponent::initialiseThumbnailAndPunchTime()
{
    if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        for (auto idi : at->edit.getEditInputDevices().getDevicesForTargetTrack (*at))
        {
            punchInTime = idi->getPunchInTime (at->itemID);

            if (idi->getRecordingFile (at->itemID).exists())
                thumbnail = at->edit.engine.getRecordingThumbnailManager().getThumbnailFor (idi->getRecordingFile (at->itemID));
        }
    }
}

void RecordingClipComponent::paint (Graphics& g)
{
    g.fillAll (Colours::red.withAlpha (0.5f));
    g.setColour (Colours::black);
    g.drawRect (getLocalBounds());

    if (editViewState.drawWaveforms)
        drawThumbnail (g, Colours::black.withAlpha (0.5f));
}

void RecordingClipComponent::drawThumbnail (Graphics& g, Colour waveformColour) const
{
    if (thumbnail == nullptr)
        return;

    Rectangle<int> bounds;
    tracktion::TimeRange times;
    getBoundsAndTime (bounds, times);
    auto w = bounds.getWidth();

    if (w > 0 && w < 10000)
    {
        g.setColour (waveformColour);
        thumbnail->thumb->drawChannels (g, bounds, times.getStart().inSeconds(), times.getEnd().inSeconds(), 1.0f);
    }
}

bool RecordingClipComponent::getBoundsAndTime (Rectangle<int>& bounds, tracktion::TimeRange& times) const
{
    auto editTimeToX = [this] (te::TimePosition t)
    {
        if (auto p = getParentComponent())
            return editViewState.timeToX (t, p->getWidth()) - getX();

        return 0;
    };

    auto xToEditTime = [this] (int x)
    {
        if (auto p = getParentComponent())
            return editViewState.xToTime (x + getX(), p->getWidth());

        return te::TimePosition();
    };

    bool hasLooped = false;
    auto& edit = track->edit;

    if (auto epc = edit.getTransport().getCurrentPlaybackContext())
    {
        auto localBounds = getLocalBounds();

        auto timeStarted = thumbnail->punchInTime;
        auto unloopedPos = timeStarted + te::TimeDuration::fromSeconds (thumbnail->thumb->getTotalLength());

        auto t1 = timeStarted;
        auto t2 = unloopedPos;

        if (epc->isLooping() && t2 >= epc->getLoopTimes().getEnd())
        {
            hasLooped = true;

            t1 = jmin (t1, epc->getLoopTimes().getStart());
            t2 = epc->getPosition();

            t1 = jmax (editViewState.viewX1.get(), t1);
            t2 = jmin (editViewState.viewX2.get(), t2);
        }
        else if (edit.recordingPunchInOut)
        {
            const auto in  = thumbnail->punchInTime;
            const auto out = edit.getTransport().getLoopRange().getEnd();

            t1 = jlimit (in, out, t1);
            t2 = jlimit (in, out, t2);
        }

        bounds = localBounds.withX (jmax (localBounds.getX(), editTimeToX (t1)))
                 .withRight (jmin (localBounds.getRight(), editTimeToX (t2)));

        auto loopRange = epc->getLoopTimes();
        const auto recordedTime = unloopedPos - toDuration (epc->getLoopTimes().getStart());
        const int numLoops = (int) (recordedTime / loopRange.getLength());

        const tracktion::TimeRange editTimes (xToEditTime (bounds.getX()),
                                              xToEditTime (bounds.getRight()));

        times = (editTimes + (loopRange.getLength() * numLoops)) - toDuration (timeStarted);
    }

    return hasLooped;
}

void RecordingClipComponent::timerCallback()
{
    updatePosition();
}

void RecordingClipComponent::updatePosition()
{
    auto& edit = track->edit;

    if (auto epc = edit.getTransport().getCurrentPlaybackContext())
    {
        auto t1 = punchInTime >= 0s ? punchInTime : edit.getTransport().getTimeWhenStarted();
        auto t2 = jmax (t1, epc->getUnloopedPosition());

        if (epc->isLooping())
        {
            auto loopTimes = epc->getLoopTimes();

            if (t2 >= loopTimes.getEnd())
            {
                t1 = jmin (t1, loopTimes.getStart());
                t2 = loopTimes.getEnd();
            }
        }
        else if (edit.recordingPunchInOut)
        {
            auto mr = edit.getTransport().getLoopRange();
            auto in  = mr.getStart();
            auto out = mr.getEnd();

            t1 = jlimit (in, out, t1);
            t2 = jlimit (in, out, t2);
        }

        t1 = jmax (t1, editViewState.viewX1.get());
        t2 = jmin (t2, editViewState.viewX2.get());

        if (auto p = getParentComponent())
        {
            int x1 = editViewState.timeToX (t1, p->getWidth());
            int x2 = editViewState.timeToX (t2, p->getWidth());

            setBounds (x1, 0, x2 - x1, p->getHeight());
            return;
        }
    }

    setBounds ({});
}

//==============================================================================
TrackHeaderComponent::TrackHeaderComponent (EditViewState& evs, te::Track::Ptr t)
    : editViewState (evs), track (t)
{
    Helpers::addAndMakeVisible (*this, { &trackName, &armButton, &muteButton, &soloButton,
                                         &inputButton, &autoButton, &fxButton });

    // FX: drop one of the user's saved MotiveScript effects onto this track
    fxButton.onClick = [this]
    {
        auto at = dynamic_cast<te::AudioTrack*> (track.get());
        if (at == nullptr)
            return;

        PopupMenu m;
        m.addSectionHeader ("Your effects");

        const auto files = MotiveScriptLibrary::getEffectFiles();

        int id = 1;
        for (const auto& f : files)
            m.addItem (id++, f.getFileNameWithoutExtension());

        if (files.isEmpty())
            m.addItem (999, "(none saved yet — use the Script panel)", false);

        m.addSeparator();
        m.addItem (1000, "New empty script");

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (fxButton),
                         [this, at, files] (int res)
        {
            if (res == 0)
                return;

            auto plugin = track->edit.getPluginCache().createNewPlugin (MotiveScriptPlugin::xmlTypeName, {});
            auto script = dynamic_cast<MotiveScriptPlugin*> (plugin.get());
            if (script == nullptr)
                return;

            if (res >= 1 && res <= files.size())
                script->setSource (files[res - 1].loadFileAsString());

            // after the instrument, before the volume plugin (same as the wah)
            auto plugins = at->pluginList.getPlugins();
            int insertIndex = plugins.size();

            for (int idx = 0; idx < plugins.size(); ++idx)
            {
                if (dynamic_cast<te::VolumeAndPanPlugin*> (plugins[idx].get()) != nullptr)
                {
                    insertIndex = idx;
                    break;
                }
            }

            at->pluginList.insertPlugin (plugin, insertIndex, nullptr);
            MotiveScriptEditor::show (*script);   // open it for live tweaking
        });
    };

    // AUT: toggle automation lanes (volume / pan / wah) shown under this track
    autoButton.onClick = [this]
    {
        auto at = dynamic_cast<te::AudioTrack*> (track.get());
        if (at == nullptr)
            return;

        auto lanes = MotiveAutoLanes::get (*track);

        PopupMenu m;
        m.addSectionHeader ("Automation lanes");
        m.addItem (1, "Volume",         true, lanes.contains ("volume"));
        m.addItem (2, "Pan",            true, lanes.contains ("pan"));
        m.addItem (3, "Wah (Crybaby)",  true, lanes.contains ("wah"));

        const int res = m.show();
        if (res == 0)
            return;

        const juce::String type = res == 1 ? "volume" : res == 2 ? "pan" : "wah";

        if (lanes.contains (type))
        {
            lanes.removeString (type);
        }
        else
        {
            lanes.add (type);

            // a wah lane needs the wah plugin on the track to have something to move
            if (type == "wah")
            {
                bool hasWah = false;
                for (auto p : at->pluginList)
                    if (dynamic_cast<WahPlugin*> (p) != nullptr)
                        hasWah = true;

                if (! hasWah)
                {
                    if (auto p = track->edit.getPluginCache().createNewPlugin (WahPlugin::xmlTypeName, {}))
                    {
                        // AFTER any instrument (so it filters real audio, not the
                        // silence before a synth) but before the volume plugin
                        auto plugins = at->pluginList.getPlugins();
                        int insertIndex = plugins.size();

                        for (int idx = 0; idx < plugins.size(); ++idx)
                        {
                            if (dynamic_cast<te::VolumeAndPanPlugin*> (plugins[idx].get()) != nullptr)
                            {
                                insertIndex = idx;
                                break;
                            }
                        }

                        at->pluginList.insertPlugin (p, insertIndex, nullptr);
                    }
                }
            }
        }

        MotiveAutoLanes::set (*track, lanes);
    };

    armButton.setColour (TextButton::buttonOnColourId, Colours::red);
    muteButton.setColour (TextButton::buttonOnColourId, Colours::red);
    soloButton.setColour (TextButton::buttonOnColourId, Colours::green);

    trackName.setText (t->getName(), dontSendNotification);

    // double-click to rename (disabled while recording)
    trackName.setEditable (false, true, false);
    trackName.onEditorShow = [this]
    {
        if (track->edit.getTransport().isRecording())
            trackName.hideEditor (true);
    };
    trackName.onTextChange = [this]
    {
        if (trackName.getText().trim().isNotEmpty())
            track->setName (trackName.getText().trim());
        else
            trackName.setText (track->getName(), dontSendNotification);
    };

    if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        inputButton.onClick = [this, at]
        {
            PopupMenu m;

            if (EngineHelpers::trackHasInput (*at))
            {
                bool ticked = EngineHelpers::isInputMonitoringEnabled (*at);
                m.addItem (1000, "Input Monitoring", true, ticked);
                m.addSeparator();
            }

            if (editViewState.showWaveDevices)
            {
                int id = 1;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        bool ticked = instance->getTargets().getFirst() == at->itemID;
                        m.addItem (id++, instance->getInputDevice().getName(), true, ticked);
                    }
                }
            }

            if (editViewState.showMidiDevices)
            {
                m.addSeparator();

                int id = 100;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    // include virtual devices so the Motive Keys piano is assignable
                    const auto type = instance->getInputDevice().getDeviceType();

                    if (type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice)
                    {
                        bool ticked = instance->getTargets().getFirst() == at->itemID;
                        m.addItem (id++, instance->getInputDevice().getName(), true, ticked);
                    }
                }
            }

            int res = m.show();

            if (res == 1000)
            {
                EngineHelpers::enableInputMonitoring (*at, ! EngineHelpers::isInputMonitoringEnabled (*at));
            }
            else if (res >= 100)
            {
                int id = 100;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    const auto type = instance->getInputDevice().getDeviceType();

                    if (type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice)
                    {
                        if (id == res)
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);

                        id++;
                    }
                }
            }
            else if (res >= 1)
            {
                int id = 1;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        if (id == res)
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);

                        id++;
                    }
                }
            }
        };
        // Clicking A opens a menu of every input: pick one to assign it to this
        // track AND arm it in a single step; "Not armed" disarms. This replaces
        // the old auto-arm, and the button is never disabled.
        armButton.onClick = [this, at]
        {
            PopupMenu m;
            m.addSectionHeader ("Record-arm from:");

            auto inputs = at->edit.getAllInputDevices();
            int id = 1;

            for (auto instance : inputs)
            {
                const bool armedHere = instance->getTargets().getFirst() == at->itemID
                                       && instance->isRecordingEnabled (at->itemID);
                m.addItem (id++, instance->getInputDevice().getName(), true, armedHere);
            }

            m.addSeparator();
            m.addItem (1000, "Not armed", true, ! EngineHelpers::isTrackArmed (*at));

            m.showMenuAsync (PopupMenu::Options().withTargetComponent (armButton),
                             [this, at, inputs] (int res)
            {
                auto& um = at->edit.getUndoManager();

                if (res == 1000)
                {
                    EngineHelpers::armTrack (*at, false);
                }
                else if (res >= 1 && res <= inputs.size())
                {
                    auto chosen = inputs[res - 1];
                    chosen->setTarget (at->itemID, true, &um, 0);
                    chosen->setRecordingEnabled (at->itemID, true);
                }

                armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
            });
        };
        muteButton.onClick = [at] { at->setMute (! at->isMuted (false)); };
        soloButton.onClick = [at] { at->setSolo (! at->isSolo (false)); };

        armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
    }
    else
    {
        armButton.setVisible (false);
        muteButton.setVisible (false);
        soloButton.setVisible (false);
    }

    track->state.addListener (this);
    inputsState = track->edit.state.getChildWithName (te::IDs::INPUTDEVICES);
    inputsState.addListener (this);

    valueTreePropertyChanged (track->state, te::IDs::mute);
    valueTreePropertyChanged (track->state, te::IDs::solo);
    valueTreePropertyChanged (inputsState, te::IDs::targetIndex);
}

TrackHeaderComponent::~TrackHeaderComponent()
{
    track->state.removeListener (this);
}

void TrackHeaderComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (te::TrackList::isTrack (v))
    {
        if (i == te::IDs::mute)
            muteButton.setToggleState ((bool)v[i], dontSendNotification);
        else if (i == te::IDs::solo)
            soloButton.setToggleState ((bool)v[i], dontSendNotification);
    }
    else if (v.hasType (te::IDs::INPUTDEVICES)
             || v.hasType (te::IDs::INPUTDEVICE)
             || v.hasType (te::IDs::INPUTDEVICEDESTINATION))
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
        {
            // never disable arming — the menu itself lets the user pick an input
            armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
        }
    }
}

void TrackHeaderComponent::paint (Graphics& g)
{
    g.setColour (Colours::grey);
    g.fillRect (getLocalBounds().withTrimmedRight (2));

    if (editViewState.selectionManager.isSelected (track.get()))
    {
        g.setColour (Colours::red);
        g.drawRect (getLocalBounds().withTrimmedRight (-4), 2);
    }
}

void TrackHeaderComponent::mouseDown (const MouseEvent& e)
{
    if (MotiveTrackHeight::isNearBottom (*this, e.y))
    {
        resizingHeight = true;
        resizeStartHeight = MotiveTrackHeight::get (*track);
        return;
    }

    editViewState.selectionManager.selectOnly (track.get());
}

void TrackHeaderComponent::mouseMove (const MouseEvent& e)
{
    setMouseCursor (MotiveTrackHeight::isNearBottom (*this, e.y) ? MouseCursor::UpDownResizeCursor
                                                                 : MouseCursor::NormalCursor);
}

void TrackHeaderComponent::mouseDrag (const MouseEvent& e)
{
    if (resizingHeight)
    {
        MotiveTrackHeight::set (*track, resizeStartHeight + e.getDistanceFromDragStartY());

        if (auto parent = getParentComponent())
            parent->resized();
    }
}

void TrackHeaderComponent::mouseUp (const MouseEvent&)
{
    resizingHeight = false;
}

void TrackHeaderComponent::resized()
{
    // fixed control sizes: taller tracks get more waveform, not bigger buttons
    auto r = getLocalBounds().reduced (4);
    trackName.setBounds (r.removeFromTop (jmin (20, jmax (14, r.getHeight() - 20))));

    auto buttonRow = r.removeFromTop (18);

    for (auto* b : { &inputButton, &armButton, &muteButton, &soloButton })
    {
        b->setBounds (buttonRow.removeFromLeft (19));
        buttonRow.removeFromLeft (2);
    }

    autoButton.setBounds (buttonRow.removeFromLeft (28));
    buttonRow.removeFromLeft (2);
    fxButton.setBounds (buttonRow.removeFromLeft (26));
}

//==============================================================================
AutomationLaneComponent::AutomationLaneComponent (EditViewState& evs, te::Track::Ptr t,
                                                  const juce::String& type)
    : editViewState (evs), track (t), laneType (type)
{
    parameter = resolveParameter();
    startTimerHz (20);
}

te::AutomatableParameter::Ptr AutomationLaneComponent::resolveParameter() const
{
    auto at = dynamic_cast<te::AudioTrack*> (track.get());
    if (at == nullptr)
        return {};

    if (laneType == "volume")
        if (auto vp = at->getVolumePlugin())
            return vp->volParam;

    if (laneType == "pan")
        if (auto vp = at->getVolumePlugin())
            return vp->panParam;

    if (laneType == "wah")
        for (auto p : at->pluginList)
            if (auto wah = dynamic_cast<WahPlugin*> (p))
                return wah->pedalParam;

    return {};
}

Colour AutomationLaneComponent::laneColour() const
{
    return laneType == "volume" ? Colour (0xff2ecc71)
         : laneType == "pan"    ? Colour (0xff42a5f5)
                                : Colour (0xffab47bc);   // wah
}

void AutomationLaneComponent::paint (Graphics& g)
{
    g.fillAll (Colour (0xff15171b));

    if (parameter == nullptr)
        return;

    auto& curve = parameter->getCurve();
    const auto colour = laneColour();

    // centre guide line
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (getHeight() / 2, 0.0f, (float) getWidth());

    // curve sampled across the visible width
    Path path;
    for (int x = 0; x <= getWidth(); x += 2)
    {
        const auto t = editViewState.xToTime (x, getWidth());
        const auto v = curve.getNumPoints() > 0 ? curve.getValueAt (t)
                                                : parameter->getCurrentValue();
        const auto y = valueToY (v);

        if (x == 0) path.startNewSubPath (0.0f, y);
        else        path.lineTo ((float) x, y);
    }

    g.setColour (colour);
    g.strokePath (path, PathStrokeType (2.0f));

    g.setColour (Colours::white);
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const auto x = (float) editViewState.timeToX (curve.getPointTime (i), getWidth());
        g.fillEllipse (x - 3.5f, valueToY (curve.getPointValue (i)) - 3.5f, 7.0f, 7.0f);
    }

    // lane label
    g.setColour (colour.withAlpha (0.9f));
    g.setFont (FontOptions (10.5f, Font::bold));
    g.drawText (laneType.toUpperCase(), 6, 2, 80, 12, Justification::centredLeft);
}

void AutomationLaneComponent::mouseDown (const MouseEvent& e)
{
    if (parameter == nullptr)
        return;

    auto& curve = parameter->getCurve();
    dragIndex = findPointNear (e.position);

    if (e.mods.isPopupMenu())
    {
        if (dragIndex >= 0)
            curve.removePoint (dragIndex);

        dragIndex = -1;
        repaint();
        return;
    }

    if (dragIndex < 0)
    {
        dragIndex = curve.addPoint (editViewState.xToTime (e.x, getWidth()),
                                    yToValue ((float) e.y), 0.0f);
        repaint();
    }
}

void AutomationLaneComponent::mouseDrag (const MouseEvent& e)
{
    if (parameter == nullptr || dragIndex < 0)
        return;

    dragIndex = parameter->getCurve().movePoint (dragIndex,
                                                 editViewState.xToTime (jlimit (0, getWidth(), e.x), getWidth()),
                                                 yToValue ((float) jlimit (0, getHeight(), e.y)),
                                                 false);
    repaint();
}

void AutomationLaneComponent::mouseUp (const MouseEvent&)
{
    dragIndex = -1;
}

void AutomationLaneComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (parameter == nullptr)
        return;

    if (auto i = findPointNear (e.position); i >= 0)
    {
        parameter->getCurve().removePoint (i);
        repaint();
    }
}

float AutomationLaneComponent::valueToY (float value) const
{
    const auto normalised = parameter->valueRange.convertTo0to1 (value);
    return 4.0f + (1.0f - normalised) * (float) (getHeight() - 8);
}

float AutomationLaneComponent::yToValue (float y) const
{
    const auto normalised = jlimit (0.0f, 1.0f, 1.0f - (y - 4.0f) / (float) (getHeight() - 8));
    return parameter->valueRange.convertFrom0to1 (normalised);
}

int AutomationLaneComponent::findPointNear (Point<float> pos) const
{
    if (parameter == nullptr)
        return -1;

    auto& curve = parameter->getCurve();

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const auto x = (float) editViewState.timeToX (curve.getPointTime (i), getWidth());
        const auto y = valueToY (curve.getPointValue (i));

        if (pos.getDistanceFrom ({ x, y }) < 8.0f)
            return i;
    }

    return -1;
}

void AutomationLaneComponent::timerCallback()
{
    if (track->edit.getTransport().isPlaying())
        repaint();
}

//==============================================================================
PluginComponent::PluginComponent (EditViewState& evs, te::Plugin::Ptr p)
    : editViewState (evs), plugin (p)
{
    setButtonText (plugin->getName().substring (0, 1));
}

PluginComponent::~PluginComponent()
{
}

void PluginComponent::clicked (const ModifierKeys& modifiers)
{
    editViewState.selectionManager.selectOnly (plugin.get());
    if (modifiers.isPopupMenu())
    {
        PopupMenu m;
        m.addItem ("Delete", [this] { plugin->deleteFromParent(); });
        m.showAt (this);
    }
    else if (auto script = dynamic_cast<MotiveScriptPlugin*> (plugin.get()))
    {
        MotiveScriptEditor::show (*script);   // our own live-coding window
    }
    else
    {
        plugin->showWindowExplicitly();
    }
}

//==============================================================================
TrackFooterComponent::TrackFooterComponent (EditViewState& evs, te::Track::Ptr t)
    : editViewState (evs), track (t)
{
    addAndMakeVisible (addButton);

    buildPlugins();

    track->state.addListener (this);

    addButton.onClick = [this]
    {
        if (auto plugin = showMenuAndCreatePlugin (track->edit))
            track->pluginList.insertPlugin (plugin, 0, &editViewState.selectionManager);
    };
}

TrackFooterComponent::~TrackFooterComponent()
{
    track->state.removeListener (this);
}

void TrackFooterComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)
{
    if (c.hasType (te::IDs::PLUGIN))
        markAndUpdate (updatePlugins);
}

void TrackFooterComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int)
{
    if (c.hasType (te::IDs::PLUGIN))
        markAndUpdate (updatePlugins);
}

void TrackFooterComponent::valueTreeChildOrderChanged (juce::ValueTree&, int, int)
{
    markAndUpdate (updatePlugins);
}

void TrackFooterComponent::paint (Graphics& g)
{
    g.setColour (Colours::grey);
    g.fillRect (getLocalBounds().withTrimmedLeft (2));

    if (editViewState.selectionManager.isSelected (track.get()))
    {
        g.setColour (Colours::red);
        g.drawRect (getLocalBounds().withTrimmedLeft (-4), 2);
    }
}

void TrackFooterComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (track.get());
}

void TrackFooterComponent::resized()
{
    auto r = getLocalBounds().reduced (4);
    const int cx = 21;

    addButton.setBounds (r.removeFromLeft (cx).withSizeKeepingCentre (cx, cx));
    r.removeFromLeft (6);

    for (auto p : plugins)
    {
        p->setBounds (r.removeFromLeft (cx).withSizeKeepingCentre (cx, cx));
        r.removeFromLeft (2);
    }
}

void TrackFooterComponent::handleAsyncUpdate()
{
    if (compareAndReset (updatePlugins))
        buildPlugins();
}

void TrackFooterComponent::buildPlugins()
{
    plugins.clear();

    for (auto plugin : track->pluginList)
    {
        auto p = new PluginComponent (editViewState, plugin);
        addAndMakeVisible (p);
        plugins.add (p);
    }
    resized();
}

//==============================================================================
TrackComponent::TrackComponent (EditViewState& evs, te::Track::Ptr t)
    : editViewState (evs), track (t)
{
    track->state.addListener (this);
    track->edit.getTransport().addChangeListener (this);

    markAndUpdate (updateClips);
}

TrackComponent::~TrackComponent()
{
    track->state.removeListener (this);
    track->edit.getTransport().removeChangeListener (this);
}

void TrackComponent::paint (Graphics& g)
{
    g.fillAll (Colours::grey);

    if (editViewState.selectionManager.isSelected (track.get()))
    {
        g.setColour (Colours::red);

        auto rc = getLocalBounds();
        if (editViewState.showHeaders) rc = rc.withTrimmedLeft (-4);
        if (editViewState.showFooters) rc = rc.withTrimmedRight (-4);

        g.drawRect (rc, 2);
    }
}

void TrackComponent::mouseDown (const MouseEvent& e)
{
    if (MotiveTrackHeight::isNearBottom (*this, e.y))
    {
        resizingHeight = true;
        resizeStartHeight = MotiveTrackHeight::get (*track);
        return;
    }

    editViewState.selectionManager.selectOnly (track.get());

    // clicking empty lane space drops the playhead there
    track->edit.getTransport().setPosition (editViewState.xToTime (e.x, getWidth()));
}

void TrackComponent::mouseMove (const MouseEvent& e)
{
    setMouseCursor (MotiveTrackHeight::isNearBottom (*this, e.y) ? MouseCursor::UpDownResizeCursor
                                                                 : MouseCursor::NormalCursor);
}

void TrackComponent::mouseDrag (const MouseEvent& e)
{
    if (resizingHeight)
    {
        MotiveTrackHeight::set (*track, resizeStartHeight + e.getDistanceFromDragStartY());

        if (auto parent = getParentComponent())
            parent->resized();
    }
}

void TrackComponent::mouseUp (const MouseEvent&)
{
    resizingHeight = false;
}

void TrackComponent::changeListenerCallback (ChangeBroadcaster*)
{
    markAndUpdate (updateRecordClips);
}

void TrackComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (te::Clip::isClipState (v))
    {
        if (i == te::IDs::start
            || i == te::IDs::length)
        {
            markAndUpdate (updatePositions);
        }
    }
}

void TrackComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)
{
    if (te::Clip::isClipState (c))
        markAndUpdate (updateClips);
}

void TrackComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int)
{
    if (te::Clip::isClipState (c))
        markAndUpdate (updateClips);
}

void TrackComponent::valueTreeChildOrderChanged (juce::ValueTree& v, int a, int b)
{
    if (te::Clip::isClipState (v.getChild (a)))
        markAndUpdate (updatePositions);
    else if (te::Clip::isClipState (v.getChild (b)))
        markAndUpdate (updatePositions);
}

void TrackComponent::handleAsyncUpdate()
{
    if (compareAndReset (updateClips))
        buildClips();
    if (compareAndReset (updatePositions))
        resized();
    if (compareAndReset (updateRecordClips))
        buildRecordClips();
}

void TrackComponent::resized()
{
    for (auto cc : clips)
    {
        auto& c = cc->getClip();
        auto pos = c.getPosition();
        int x1 = editViewState.timeToX (pos.getStart(), getWidth());
        int x2 = editViewState.timeToX (pos.getEnd(), getWidth());

        cc->setBounds (x1, 0, x2 - x1, getHeight());
    }
}

void TrackComponent::buildClips()
{
    clips.clear();

    if (auto ct = dynamic_cast<te::ClipTrack*> (track.get()))
    {
        for (auto c : ct->getClips())
        {
            ClipComponent* cc = nullptr;

            if (dynamic_cast<te::WaveAudioClip*> (c))
                cc = new AudioClipComponent (editViewState, c);
            else if (dynamic_cast<te::MidiClip*> (c))
                cc = new MidiClipComponent (editViewState, c);
            else
                cc = new ClipComponent (editViewState, c);

            clips.add (cc);
            addAndMakeVisible (cc);
        }
    }

    resized();
}

void TrackComponent::buildRecordClips()
{
    bool needed = false;

    if (track->edit.getTransport().isRecording())
    {
        for (auto in : track->edit.getAllInputDevices())
        {
            if (in->isRecordingActive() && track->itemID == in->getTargets().getFirst())
            {
                needed = true;
                break;
            }
        }
    }

    if (needed)
    {
        recordingClip = std::make_unique<RecordingClipComponent> (track, editViewState);
        addAndMakeVisible (*recordingClip);
    }
    else
    {
        recordingClip = nullptr;
    }
}

//==============================================================================
PlayheadComponent::PlayheadComponent (te::Edit& e , EditViewState& evs)
    : edit (e), editViewState (evs)
{
    startTimerHz (30);
}

void PlayheadComponent::paint (Graphics& g)
{
    // the In/Out loop region, shaded so it's obvious where it is
    if (const auto loop = edit.getTransport().getLoopRange(); loop.getLength() > te::TimeDuration())
    {
        const int a = editViewState.timeToX (loop.getStart(), getWidth());
        const int b = editViewState.timeToX (loop.getEnd(), getWidth());

        g.setColour (Colours::yellow.withAlpha (0.07f));
        g.fillRect (a, 0, b - a, getHeight());
        g.setColour (Colours::yellow.withAlpha (0.75f));
        g.fillRect (a, 0, b - a, 3);
    }

    g.setColour (Colours::yellow);
    g.drawRect (xPosition, 0, 2, getHeight());
}

bool PlayheadComponent::hitTest (int x, int)
{
    if (std::abs (x - xPosition) <= 3)
        return true;

    return false;
}

void PlayheadComponent::mouseDown (const MouseEvent&)
{
    edit.getTransport().setUserDragging (true);
}

void PlayheadComponent::mouseUp (const MouseEvent&)
{
    edit.getTransport().setUserDragging (false);
}

void PlayheadComponent::mouseDrag (const MouseEvent& e)
{
    auto t = editViewState.xToTime (e.x, getWidth());
    edit.getTransport().setPosition (t);
    timerCallback();
}

void PlayheadComponent::timerCallback()
{
    if (firstTimer)
    {
        // On Linux, don't set the mouse cursor until after the Component has appeared
        firstTimer = false;
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    }

    int newX = editViewState.timeToX (edit.getTransport().getPosition(), getWidth());
    if (newX != xPosition)
    {
        repaint (jmin (newX, xPosition) - 1, 0, jmax (newX, xPosition) - jmin (newX, xPosition) + 3, getHeight());
        xPosition = newX;
    }

    // repaint when the In/Out region changes so the shading follows immediately
    if (const auto loop = edit.getTransport().getLoopRange(); loop != lastLoopRange)
    {
        lastLoopRange = loop;
        repaint();
    }
}

//==============================================================================
EditComponent::EditComponent (te::Edit& e, te::SelectionManager& sm)
    : edit (e), editViewState (e, sm)
{
    edit.state.addListener (this);
    editViewState.selectionManager.addChangeListener (this);

    addAndMakeVisible (playhead);

    markAndUpdate (updateTracks);
}

EditComponent::~EditComponent()
{
    editViewState.selectionManager.removeChangeListener (this);
    edit.state.removeListener (this);
}

void EditComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    // our additions: lane list changes rebuild the rows, height changes relayout
    if (i == MotiveAutoLanes::lanesID)
    {
        markAndUpdate (updateTracks);
        return;
    }

    if (i == MotiveTrackHeight::heightID)
    {
        markAndUpdate (updateZoom);
        return;
    }

    if (v.hasType (IDs::EDITVIEWSTATE))
    {
        if (i == IDs::viewX1
            || i == IDs::viewX2
            || i == IDs::viewY)
        {
            markAndUpdate (updateZoom);
        }
        else if (i == IDs::showHeaders
                 || i == IDs::showFooters)
        {
            markAndUpdate (updateZoom);
        }
        else if (i == IDs::drawWaveforms)
        {
            repaint();
        }
    }
}

void EditComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)
{
    if (te::TrackList::isTrack (c))
        markAndUpdate (updateTracks);
}

void EditComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int)
{
    if (te::TrackList::isTrack (c))
        markAndUpdate (updateTracks);
}

void EditComponent::valueTreeChildOrderChanged (juce::ValueTree& v, int a, int b)
{
    if (te::TrackList::isTrack (v.getChild (a)))
        markAndUpdate (updateTracks);
    else if (te::TrackList::isTrack (v.getChild (b)))
        markAndUpdate (updateTracks);
}

void EditComponent::handleAsyncUpdate()
{
    if (compareAndReset (updateTracks))
        buildTracks();
    if (compareAndReset (updateZoom))
        resized();
}

void EditComponent::resized()
{
    jassert (headers.size() == tracks.size());

    const int trackGap = 2;
    const int headerWidth = editViewState.showHeaders ? 150 : 0;
    const int footerWidth = editViewState.showFooters ? 150 : 0;

    playhead.setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedRight (footerWidth));

    int y = roundToInt (editViewState.viewY.get());
    for (int i = 0; i < jmin (headers.size(), tracks.size()); i++)
    {
        auto h = headers[i];
        auto t = tracks[i];
        auto f = footers[i];

        // each lane has its own height — drag a lane's bottom edge to change it
        const int trackHeight = MotiveTrackHeight::get (*t->getTrack());

        h->setBounds (0, y, headerWidth, trackHeight);
        t->setBounds (headerWidth, y, getWidth() - headerWidth - footerWidth, trackHeight);
        f->setBounds (getWidth() - footerWidth, y, footerWidth, trackHeight);

        y += trackHeight + trackGap;

        // this track's automation lanes stack directly beneath it, time-aligned
        for (auto* lane : autoLanes)
        {
            if (lane->getTrack() == t->getTrack())
            {
                lane->setBounds (headerWidth, y,
                                 getWidth() - headerWidth - footerWidth,
                                 AutomationLaneComponent::laneHeight);
                y += AutomationLaneComponent::laneHeight + trackGap;
            }
        }
    }

    contentHeight = y - roundToInt (editViewState.viewY.get());

    for (auto t : tracks)
        t->resized();
}

// scroll wheel / trackpad moves vertically through the track list
void EditComponent::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel)
{
    const double minY = jmin (0.0, (double) (getHeight() - contentHeight));
    editViewState.viewY = jlimit (minY, 0.0, editViewState.viewY.get() + wheel.deltaY * 160.0);
}

void EditComponent::buildTracks()
{
    tracks.clear();
    headers.clear();
    footers.clear();
    autoLanes.clear();

    for (auto t : getAllTracks (edit))
    {
        TrackComponent* c = nullptr;

        if (t->isMasterTrack())
        {
            if (editViewState.showMasterTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isTempoTrack())
        {
            if (editViewState.showGlobalTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isMarkerTrack())
        {
            if (editViewState.showMarkerTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isChordTrack())
        {
            if (editViewState.showChordTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isArrangerTrack())
        {
            if (editViewState.showArrangerTrack)
                c = new TrackComponent (editViewState, t);
        }
        else
        {
            c = new TrackComponent (editViewState, t);
        }

        if (c != nullptr)
        {
            tracks.add (c);
            addAndMakeVisible (c);

            auto h = new TrackHeaderComponent (editViewState, t);
            headers.add (h);
            addAndMakeVisible (h);

            auto f = new TrackFooterComponent (editViewState, t);
            footers.add (f);
            addAndMakeVisible (f);

            for (const auto& laneType : MotiveAutoLanes::get (*t))
            {
                auto lane = new AutomationLaneComponent (editViewState, t, laneType);
                autoLanes.add (lane);
                addAndMakeVisible (lane);
            }
        }
    }

    playhead.toFront (false);
    resized();
}


