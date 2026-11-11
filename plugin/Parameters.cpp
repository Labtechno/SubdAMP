#include "plugin/Parameters.h"

namespace subdamp::params
{

namespace
{
    juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    juce::String formatDecibels (float value, int)     { return juce::String (value, 1) + " dB"; }
    juce::String formatPercent (float value, int)      { return juce::String (juce::roundToInt (value)) + " %"; }
    juce::String formatMilliseconds (float value, int) { return juce::String (juce::roundToInt (value)) + " ms"; }

    juce::String formatHertz (float value, int)
    {
        return std::abs (value) >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                           : juce::String (value, std::abs (value) < 100.0f ? 1 : 0) + " Hz";
    }

    using Formatter = juce::String (*) (float, int);

    std::unique_ptr<juce::AudioParameterFloat> makeFloat (const char* parameterId, int version, const juce::String& name,
                                                          juce::NormalisableRange<float> range, float defaultValue,
                                                          const char* label, Formatter formatter)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { parameterId, version }, name, range, defaultValue,
            juce::AudioParameterFloatAttributes{}.withLabel (label).withStringFromValueFunction (formatter));
    }

    std::unique_ptr<juce::AudioParameterFloat> makeDecibels (const char* parameterId, int version, const juce::String& name,
                                                             float minDb, float maxDb, float defaultDb)
    {
        return makeFloat (parameterId, version, name, { minDb, maxDb, 0.1f }, defaultDb, "dB", formatDecibels);
    }

    std::unique_ptr<juce::AudioParameterFloat> makePercent (const char* parameterId, const juce::String& name,
                                                            float defaultPercent, float maxPercent = 100.0f,
                                                            int version = addedInVersion2)
    {
        return makeFloat (parameterId, version, name, { 0.0f, maxPercent, 1.0f }, defaultPercent, "%", formatPercent);
    }

    /** Demi-tons : « +1 oct » pour les octaves justes, sinon « +7 dt » (une décimale si besoin). */
    juce::String formatSemitones (float value, int)
    {
        const auto rounded = std::round (value);
        const auto sign = value > 0.0f ? juce::String ("+") : juce::String();

        if (std::abs (value - rounded) < 0.05f)
        {
            const auto whole = static_cast<int> (rounded);

            if (whole != 0 && whole % 12 == 0)
                return sign + juce::String (whole / 12) + " oct";

            return sign + juce::String (whole) + " dt";
        }

        return sign + juce::String (value, 1) + " dt";
    }

    std::unique_ptr<juce::AudioParameterChoice> makeChoice (const char* parameterId, const juce::String& name,
                                                            const juce::StringArray& choices, int defaultIndex)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { parameterId, addedInVersion3 }, name, choices, defaultIndex);
    }

    // Choix d'intervalle des voix d'harmonie, et leur valeur en degrés (voir harmonyVoiceSteps).
    struct VoiceChoice
    {
        const char* label;
        std::optional<int> steps;
    };

    const std::array<VoiceChoice, 17> voiceChoices {{
        { "Aucune", std::nullopt },
        { "-2 octaves", -14 }, { "-octave", -7 }, { "-7e", -6 }, { "-6te", -5 }, { "-5te", -4 },
        { "-4te", -3 }, { "-3ce", -2 }, { "-2de", -1 },
        { "+2de", 1 }, { "+3ce", 2 }, { "+4te", 3 }, { "+5te", 4 }, { "+6te", 5 },
        { "+7e", 6 }, { "+octave", 7 }, { "+2 octaves", 14 }
    }};

    juce::StringArray voiceLabels()
    {
        juce::StringArray labels;

        for (const auto& choice : voiceChoices)
            labels.add (choice.label);

        return labels;
    }

    int voiceIndexFor (int steps)
    {
        for (std::size_t i = 0; i < voiceChoices.size(); ++i)
            if (voiceChoices[i].steps == steps)
                return static_cast<int> (i);

        return 0;
    }
}

std::optional<int> harmonyVoiceSteps (int choiceIndex) noexcept
{
    if (choiceIndex < 0 || choiceIndex >= static_cast<int> (voiceChoices.size()))
        return std::nullopt;

    return voiceChoices[static_cast<std::size_t> (choiceIndex)].steps;
}

namespace
{

    std::unique_ptr<juce::AudioParameterBool> makeSwitch (const char* parameterId, int version, const juce::String& name, bool defaultValue)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { parameterId, version }, name, defaultValue);
    }

    std::unique_ptr<juce::AudioParameterChoice> makePosition (const char* parameterId, const juce::String& name,
                                                              Position defaultPosition, int version = addedInVersion2)
    {
        return std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { parameterId, version }, name,
            juce::StringArray { utf8 ("Avant l'ampli"), utf8 ("Après l'ampli") },
            static_cast<int> (defaultPosition));
    }

    template <typename... Parameters>
    std::unique_ptr<juce::AudioProcessorParameterGroup> group (const char* groupId, const juce::String& name, Parameters... parameters)
    {
        return std::make_unique<juce::AudioProcessorParameterGroup> (groupId, name, " | ", std::move (parameters)...);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Les groupes structurent la liste des paramètres dans le DAW (automation).
    layout.add (group ("input", utf8 ("Entrée"),
        makeDecibels (id::inputGain, addedInVersion1, utf8 ("Gain d'entrée"), -24.0f, 24.0f, 0.0f)));

    layout.add (group ("gate", "Gate",
        makeSwitch (id::gateEnabled, addedInVersion2, "Gate", true),
        makeDecibels (id::gateThreshold, addedInVersion2, "Gate seuil", -90.0f, -20.0f, -65.0f),
        makeFloat (id::gateRelease, addedInVersion2, utf8 ("Gate relâchement"),
                   { 10.0f, 500.0f, 1.0f, 0.5f }, 80.0f, "ms", formatMilliseconds)));

    layout.add (group ("sub", "SUB",
        makeSwitch (id::subEnabled, addedInVersion2, "SUB", false),
        makePosition (id::subPosition, "SUB position", Position::beforeAmp),
        makePercent (id::subDry, "SUB direct", 100.0f),
        makePercent (id::subDown1, "SUB -1 octave", 70.0f),
        makePercent (id::subDown2, "SUB -2 octaves", 0.0f),
        makePercent (id::subUp, "SUB +1 octave", 0.0f),
        makeFloat (id::subTone, addedInVersion2, utf8 ("SUB tonalité"),
                   { 100.0f, 4000.0f, 1.0f, 0.3f }, 800.0f, "Hz", formatHertz)));

    layout.add (group ("whammy", "WHAMMY",
        makeSwitch (id::whEnabled, addedInVersion3, "WHAMMY", false),
        makePosition (id::whPosition, "WHAMMY position", Position::beforeAmp, addedInVersion3),
        makeFloat (id::whInterval, addedInVersion3, "WHAMMY intervalle",
                   { -24.0f, 24.0f, 0.01f }, -12.0f, "dt", formatSemitones),
        makeChoice (id::whMode, "WHAMMY mode",
                    { "Fixe", utf8 ("Attaque : vers l'intervalle"), utf8 ("Attaque : depuis l'intervalle") }, 1),
        makeFloat (id::whGlide, addedInVersion3, "WHAMMY glissement",
                   { 10.0f, 3000.0f, 1.0f, 0.35f }, 400.0f, "ms", formatMilliseconds),
        makePercent (id::whSensitivity, utf8 ("WHAMMY sensibilité"), 50.0f, 100.0f, addedInVersion3),
        makePercent (id::whMix, "WHAMMY mix", 100.0f, 100.0f, addedInVersion3)));

    layout.add (group ("harmonizer", "HARMONIZER",
        makeSwitch (id::hmEnabled, addedInVersion3, "HARMONIZER", false),
        makePosition (id::hmPosition, "HARMONIZER position", Position::afterAmp, addedInVersion3),
        makeChoice (id::hmKey, utf8 ("HARMONIZER tonalité"),
                    { "C", utf8 ("C♯ / D♭"), "D", utf8 ("D♯ / E♭"), "E", "F", utf8 ("F♯ / G♭"), "G",
                      utf8 ("G♯ / A♭"), "A", utf8 ("A♯ / B♭"), "B" }, 4),
        makeChoice (id::hmScale, "HARMONIZER gamme",
                    { "Majeur", "Mineur naturel", "Mineur harmonique", "Dorien", "Phrygien",
                      "Phrygien dominant", "Lydien", "Mixolydien", utf8 ("Parallèle (sans gamme)") }, 1),
        makeChoice (id::hmVoice1, "HARMONIZER voix 1", voiceLabels(), voiceIndexFor (2)),
        makePercent (id::hmLevel1, "HARMONIZER niveau 1", 80.0f, 100.0f, addedInVersion3),
        makeChoice (id::hmVoice2, "HARMONIZER voix 2", voiceLabels(), 0),
        makePercent (id::hmLevel2, "HARMONIZER niveau 2", 80.0f, 100.0f, addedInVersion3),
        makeFloat (id::hmFormant, addedInVersion3, "HARMONIZER formants",
                   { -12.0f, 12.0f, 0.1f }, 0.0f, "dt", formatSemitones),
        makePercent (id::hmDirect, "HARMONIZER direct", 100.0f, 100.0f, addedInVersion3)));

    layout.add (group ("amp", "Ampli",
        makeSwitch (id::ampEnabled, addedInVersion2, "Ampli", true),
        makeDecibels (id::ampGain, addedInVersion2, "Ampli gain", -24.0f, 24.0f, 0.0f),
        makeDecibels (id::ampBass, addedInVersion2, "Ampli graves", -12.0f, 12.0f, 0.0f),
        makeDecibels (id::ampMid, addedInVersion2, utf8 ("Ampli médiums"), -12.0f, 12.0f, 0.0f),
        makeDecibels (id::ampTreble, addedInVersion2, "Ampli aigus", -12.0f, 12.0f, 0.0f),
        makeDecibels (id::ampLevel, addedInVersion2, "Ampli niveau", -24.0f, 24.0f, 0.0f)));

    layout.add (group ("cab", "Cab",
        makeSwitch (id::cabEnabled, addedInVersion2, "Cab", true)));

    layout.add (group ("fs", "FREQ SHIFT",
        makeSwitch (id::fsEnabled, addedInVersion2, "FREQ SHIFT", false),
        makePosition (id::fsPosition, "FREQ SHIFT position", Position::afterAmp),
        makeFloat (id::fsShift, addedInVersion2, utf8 ("FREQ SHIFT décalage"),
                   { -2000.0f, 2000.0f, 0.1f, 0.35f, true }, 120.0f, "Hz", formatHertz),
        makePercent (id::fsMix, "FREQ SHIFT mix", 50.0f),
        makePercent (id::fsFeedback, utf8 ("FREQ SHIFT réinjection"), 0.0f, 95.0f)));

    layout.add (group ("output", "Sortie",
        makeDecibels (id::outputGain, addedInVersion1, "Volume de sortie", -60.0f, 12.0f, 0.0f),
        makeSwitch (id::bypass, addedInVersion1, "Bypass", false)));

    return layout;
}

} // namespace subdamp::params
