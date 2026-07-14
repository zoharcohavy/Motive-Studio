#pragma once

// Included from MainComponent.cpp after the tracktion example UI kit.
// Persisted drum configuration, mirroring the React app's model: a "kit" bank
// (badges over the drum photo) and a "pads" bank (the 4x4 grid). Each pad has
// a name, a trigger key, a MIDI note, and a sound (built-in name or file path).

struct DrumPad
{
    String name;
    String key;      // single lowercase character
    String sound;    // a DrumSounds name, or an absolute path to a user file
    int note = 36;

    // relative position over the drum photo (kit bank only)
    float x = 0.5f, y = 0.5f;

    File resolveFile() const
    {
        if (File::isAbsolutePath (sound))
            return File (sound);

        return DrumSounds::ensure (sound);
    }
};

struct DrumBank
{
    static constexpr int numPads = 16;
    std::array<DrumPad, numPads> pads;

    //==============================================================================
    // defaults — anchor positions and grid keys taken from the React app
    static DrumBank kitDefaults()
    {
        DrumBank b;

        const struct { const char* name; const char* sound; const char* key; float x, y; } defs[numPads] =
        {
            { "Crash",    "crash",      "1", 0.22f, 0.18f },  { "Hat",     "hat closed", "2", 0.34f, 0.18f },
            { "Open Hat", "hat open",   "3", 0.46f, 0.18f },  { "Ride",    "ride",       "4", 0.58f, 0.18f },
            { "Cowbell",  "cowbell",    "5", 0.70f, 0.18f },
            { "Hi Tom",   "tom high",   "q", 0.34f, 0.36f },  { "Mid Tom", "tom mid",    "w", 0.46f, 0.36f },
            { "Low Tom",  "tom low",    "e", 0.58f, 0.36f },
            { "Snare",    "snare",      "s", 0.40f, 0.53f },  { "Clap",    "clap",       "d", 0.52f, 0.53f },
            { "Kick",     "kick",       "f", 0.42f, 0.72f },  { "Boom",    "boom",       "g", 0.54f, 0.72f },
            { "Rim",      "rim",        "z", 0.90f, 0.20f },  { "Shaker",  "shaker",     "x", 0.90f, 0.36f },
            { "Block",    "block",      "c", 0.90f, 0.52f },  { "Perc",    "perc",       "v", 0.90f, 0.68f },
        };

        for (int i = 0; i < numPads; ++i)
        {
            b.pads[(size_t) i] = { defs[i].name, defs[i].key, defs[i].sound,
                                   36 + i, defs[i].x, defs[i].y };
        }

        return b;
    }

    static DrumBank padsDefaults()
    {
        DrumBank b;
        const char* gridKeys[numPads] = { "1","2","3","4", "q","w","e","r",
                                          "a","s","d","f", "z","x","c","v" };
        const auto& sounds = DrumSounds::getAllNames();

        for (int i = 0; i < numPads; ++i)
        {
            b.pads[(size_t) i] = { sounds[i].toUpperCase().substring (0, 1) + sounds[i].substring (1),
                                   gridKeys[i], sounds[i], 60 + i, 0.0f, 0.0f };
        }

        return b;
    }

    //==============================================================================
    static File getConfigFile()
    {
        return DrumSounds::getSamplesDirectory().getSiblingFile ("drum-config.json");
    }

    static DrumBank load (const String& bankName)
    {
        auto fallback = bankName == "kit" ? kitDefaults() : padsDefaults();
        auto parsed = JSON::parse (getConfigFile());
        auto* bank = parsed.getProperty (bankName, {}).getArray();

        if (bank == nullptr || bank->size() != numPads)
            return fallback;

        for (int i = 0; i < numPads; ++i)
        {
            const auto& v = bank->getReference (i);
            auto& pad = fallback.pads[(size_t) i];

            pad.name  = v.getProperty ("name", pad.name).toString();
            pad.key   = v.getProperty ("key", pad.key).toString().toLowerCase().substring (0, 1);
            pad.sound = v.getProperty ("sound", pad.sound).toString();
        }

        return fallback;
    }

    void save (const String& bankName) const
    {
        auto root = JSON::parse (getConfigFile());
        if (! root.isObject())
            root = var (new DynamicObject());

        Array<var> bank;
        for (const auto& pad : pads)
        {
            auto* o = new DynamicObject();
            o->setProperty ("name", pad.name);
            o->setProperty ("key", pad.key);
            o->setProperty ("sound", pad.sound);
            bank.add (var (o));
        }

        root.getDynamicObject()->setProperty (bankName, bank);
        getConfigFile().replaceWithText (JSON::toString (root));
    }
};
