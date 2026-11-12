#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Board presets.
// A preset holds the pedal arrangement (which pedals, left to right), every setting of those pedals
// (knobs, footswitches, mini toggles) and the Semitone Transposer ("gx_" parameters).
// The board INPUT/OUTPUT levels are NOT part of a preset: they belong to the player's rig, so changing
// presets never jumps the volume.
// Files: <Documents>/Pedal Factory/Presets/<name>.pfpreset (XML, values in plain units), shared by the
// Standalone and the VST3. Loading sets parameters missing from a file (pedals not in the preset, or
// controls added in a later version) to their defaults, so a preset always sounds the same.
// Message thread only.
class PresetManager
{
public:
    using BoardGetter = std::function<juce::StringArray()>;
    using BoardSetter = std::function<void(const juce::StringArray&)>;

    PresetManager(juce::AudioProcessorValueTreeState& s, BoardGetter get, BoardSetter set, juce::StringArray defaults)
        : state(s), getBoard(std::move(get)), setBoard(std::move(set)), defaultBoard(std::move(defaults))
    {
        baseline = defaultSnapshot();
    }

    static constexpr const char* extension = ".pfpreset";
    static constexpr const char* defaultName = "Default Board";

    static juce::File folder()
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("Pedal Factory").getChildFile("Presets");
    }
    // User presets, sorted by name (case-insensitive).
    juce::Array<juce::File> list() const
    {
        auto files = folder().findChildFiles(juce::File::findFiles, false, juce::String("*") + extension);
        struct ByName { static int compareElements(const juce::File& a, const juce::File& b)
            { return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()); } } cmp;
        files.sort(cmp);
        return files;
    }

    // Name of the loaded preset ("" = the default board). Saved with the session.
    juce::String currentName() const { return state.state.getProperty(propName).toString(); }
    juce::String displayName() const
    {
        if (isUserPreset()) return currentName();
        return baseline.isValid() ? juce::String(defaultName) : juce::String("Unsaved Board");   // after Delete
    }
    bool isUserPreset() const { return currentName().isNotEmpty(); }
    juce::File currentFile() const { return isUserPreset() ? fileFor(currentName()) : juce::File(); }
    static juce::File fileFor(const juce::String& name) { return folder().getChildFile(name + extension); }
    static juce::String legalName(const juce::String& name)
    {
        auto n = juce::File::createLegalFileName(name.trim()).trim();
        while (n.contains("  ")) n = n.replace("  ", " ");
        return n;
    }

    // True when the board differs from the loaded preset (pedals, order or any setting).
    bool isModified() const
    {
        if (! baseline.isValid()) return false;            // preset file gone: nothing to compare with
        const auto board = getBoard();
        if (baseline.getChildWithName(tagBoard).getProperty(propPedals).toString() != board.joinIntoString(","))
            return true;
        for (auto* p : presetParams())
            if (inPreset(idOf(*p), board) && std::abs(p->getValue() - normalisedIn(baseline, *p)) > 1.0e-4f) return true;
        return false;
    }

    void loadDefault()
    {
        apply(defaultSnapshot());
        setCurrent({}, defaultSnapshot());
    }
    bool load(const juce::File& f)
    {
        auto t = read(f);
        if (! t.isValid()) return false;
        apply(t);
        setCurrent(f.getFileNameWithoutExtension(), t);
        return true;
    }
    // Writes the current board as <name>.pfpreset (replacing a file of that name) and makes it current.
    juce::Result save(const juce::String& rawName)
    {
        const auto name = legalName(rawName);
        if (name.isEmpty()) return juce::Result::fail("Please enter a name.");
        if (auto r = folder().createDirectory(); r.failed()) return r;
        auto t = capture();
        t.setProperty(propName, name, nullptr);
        auto xml = t.createXml();
        if (xml == nullptr || ! xml->writeTo(fileFor(name)))
            return juce::Result::fail("Couldn't write " + fileFor(name).getFullPathName());
        setCurrent(name, t);
        return juce::Result::ok();
    }
    // Renames the current user preset's file (the board itself is unchanged).
    juce::Result renameCurrent(const juce::String& rawName)
    {
        const auto name = legalName(rawName);
        if (! isUserPreset()) return juce::Result::fail("Only saved presets can be renamed.");
        if (name.isEmpty()) return juce::Result::fail("Please enter a name.");
        auto from = currentFile(), to = fileFor(name);
        if (from != to && to.exists()) return juce::Result::fail("A preset called \"" + name + "\" already exists.");
        if (! from.moveFileTo(to)) return juce::Result::fail("Couldn't rename the preset file.");
        auto t = read(to);
        if (t.isValid()) { t.setProperty(propName, name, nullptr); if (auto xml = t.createXml()) xml->writeTo(to); }
        setCurrent(name, t.isValid() ? t : baseline);
        return juce::Result::ok();
    }
    // Deletes the current user preset's file. The board keeps sounding the same; it becomes unsaved.
    bool deleteCurrent()
    {
        if (! isUserPreset() || ! currentFile().deleteFile()) return false;
        setCurrent({}, juce::ValueTree());               // no baseline: shown as "Unsaved Board"
        return true;
    }
    // Previous / next user preset (wraps around).
    void step(int delta)
    {
        auto files = list();
        if (files.isEmpty()) return;
        int i = files.indexOf(currentFile());
        i = (i < 0) ? (delta > 0 ? 0 : files.size() - 1) : (i + delta + files.size()) % files.size();
        load(files[i]);
    }
    // After the host restores a session (setStateInformation): re-read the preset to compare against.
    void restoredFromState()
    {
        if (! isUserPreset()) { baseline = defaultSnapshot(); return; }
        baseline = read(currentFile());                  // invalid if the file was deleted / moved
    }

private:
    static constexpr const char* propName = "preset";
    static constexpr const char* propPedals = "pedals";
    static inline const juce::Identifier tagRoot { "PedalFactoryPreset" }, tagBoard { "Board" }, tagParam { "Param" };

    juce::AudioProcessorValueTreeState& state;
    BoardGetter getBoard;
    BoardSetter setBoard;
    juce::StringArray defaultBoard;
    juce::ValueTree baseline;                            // the loaded preset, for isModified()

    static juce::String idOf(const juce::RangedAudioParameter& p) { return p.getParameterID(); }
    // Every parameter a preset can set: all but the board INPUT/OUTPUT levels.
    // (The list never changes after the plugin is built, and isModified() asks for it four times a second.)
    const std::vector<juce::RangedAudioParameter*>& presetParams() const
    {
        if (paramList.empty())
            for (auto* ap : state.processor.getParameters())
                if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(ap))
                    if (! idOf(*p).startsWith("board_")) paramList.push_back(p);
        return paramList;
    }
    mutable std::vector<juce::RangedAudioParameter*> paramList;
    // Parameter ids are "<pedal id>_<control>"; "gx_" = global section.
    // A preset stores the transposer and the pedals on the board (settings of pedals off the board don't matter).
    static bool inPreset(const juce::String& paramId, const juce::StringArray& board)
    {
        const auto owner = paramId.upToFirstOccurrenceOf("_", false, false);
        return owner == "gx" || board.contains(owner);
    }

    // Normalised value of p in a preset tree (its default when the tree doesn't list it).
    static float normalisedIn(const juce::ValueTree& t, const juce::RangedAudioParameter& p)
    {
        auto node = t.getChildWithProperty("id", idOf(p));
        if (! node.isValid() || ! node.hasProperty("value")) return p.getDefaultValue();
        return p.convertTo0to1(p.getNormalisableRange().snapToLegalValue((float) node.getProperty("value")));
    }

    juce::ValueTree capture() const
    {
        juce::ValueTree t(tagRoot);
        t.setProperty("version", 1, nullptr);
        const auto board = getBoard();
        t.appendChild(juce::ValueTree(tagBoard).setProperty(propPedals, board.joinIntoString(","), nullptr), nullptr);
        for (auto* p : presetParams())
        {
            const auto id = idOf(*p);
            if (! inPreset(id, board)) continue;
            t.appendChild(juce::ValueTree(tagParam).setProperty("id", id, nullptr)
                              .setProperty("value", p->convertFrom0to1(p->getValue()), nullptr), nullptr);
        }
        return t;
    }
    juce::ValueTree defaultSnapshot() const
    {
        juce::ValueTree t(tagRoot);                       // no Param nodes = everything at its default
        t.appendChild(juce::ValueTree(tagBoard).setProperty(propPedals, defaultBoard.joinIntoString(","), nullptr), nullptr);
        return t;
    }
    static juce::ValueTree read(const juce::File& f)
    {
        if (auto xml = juce::XmlDocument::parse(f))
        {
            auto t = juce::ValueTree::fromXml(*xml);
            if (t.hasType(tagRoot) && t.getChildWithName(tagBoard).isValid()) return t;
        }
        return {};
    }
    void apply(const juce::ValueTree& t)
    {
        for (auto* p : presetParams())
        {
            const float v = normalisedIn(t, *p);
            if (std::abs(p->getValue() - v) < 1.0e-6f) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost(v);
            p->endChangeGesture();
        }
        juce::StringArray ids;
        ids.addTokens(t.getChildWithName(tagBoard).getProperty(propPedals).toString(), ",", "");
        ids.removeEmptyStrings();
        setBoard(ids);
    }
    void setCurrent(const juce::String& name, const juce::ValueTree& t)
    {
        state.state.setProperty(propName, name, nullptr);
        baseline = t.createCopy();
        // compare against the board as it was actually built (unknown pedal ids in a file are skipped)
        if (baseline.isValid()) baseline.getChildWithName(tagBoard).setProperty(propPedals, getBoard().joinIntoString(","), nullptr);
    }
};
