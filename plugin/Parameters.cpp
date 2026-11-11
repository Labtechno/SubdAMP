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

    /** Potard MIX d'un module : sortie = sec × (1 − MIX) + traité × MIX. */
    std::unique_ptr<juce::AudioParameterFloat> makeMix (const char* parameterId, const juce::String& moduleName, float defaultPercent)
    {
        return makePercent (parameterId, moduleName + " mix", defaultPercent, 100.0f, addedInVersion5);
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
                                                            const juce::StringArray& choices, int defaultIndex,
                                                            int version = addedInVersion3)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { parameterId, version }, name, choices, defaultIndex);
    }

    juce::String formatSeconds (float value, int)
    {
        return juce::String (value, value < 10.0f ? 1 : 0) + " s";
    }

    juce::String formatBpm (float value, int)
    {
        return juce::String (juce::roundToInt (value)) + " BPM";
    }

    juce::String formatPerSecond (float value, int)
    {
        return juce::String (value, value < 10.0f ? 1 : 0) + " /s";
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

namespace
{
    struct ShimmerChoice { const char* label; float semitones; };
    const std::array<ShimmerChoice, 5> shimmerChoices {{
        { "+octave", 12.0f }, { "+octave et quinte", 19.0f }, { "+2 octaves", 24.0f }, { "+quinte", 7.0f }, { "-octave", -12.0f }
    }};

    struct DivisionChoice { const char* label; double beats; };
    const std::array<DivisionChoice, 6> divisionChoices {{
        { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 }, { "1/8 triolet", 1.0 / 3.0 }, { "1/16 triolet", 1.0 / 6.0 }
    }};

    template <typename Choices>
    juce::StringArray labelsOf (const Choices& choices)
    {
        juce::StringArray labels;

        for (const auto& choice : choices)
            labels.add (juce::String::fromUTF8 (choice.label));

        return labels;
    }
}

float shimmerIntervalSemitones (int choiceIndex) noexcept
{
    return shimmerChoices[static_cast<std::size_t> (std::clamp (choiceIndex, 0, static_cast<int> (shimmerChoices.size()) - 1))].semitones;
}

double granularDivisionBeats (int choiceIndex) noexcept
{
    return divisionChoices[static_cast<std::size_t> (std::clamp (choiceIndex, 0, static_cast<int> (divisionChoices.size()) - 1))].beats;
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
                   { 10.0f, 500.0f, 1.0f, 0.5f }, 80.0f, "ms", formatMilliseconds),
        makeMix (id::gateMix, "Gate", 100.0f)));

    // SUB, HARMONIZER : MIX 100 % = les voix seules, sans le son direct.
    layout.add (group ("sub", "SUB",
        makeSwitch (id::subEnabled, addedInVersion2, "SUB", false),
        makePercent (id::subDown1, "SUB -1 octave", 70.0f),
        makePercent (id::subDown2, "SUB -2 octaves", 0.0f),
        makePercent (id::subUp, "SUB +1 octave", 0.0f),
        makeFloat (id::subTone, addedInVersion2, utf8 ("SUB tonalité"),
                   { 100.0f, 4000.0f, 1.0f, 0.3f }, 800.0f, "Hz", formatHertz),
        makeMix (id::subMix, "SUB", 50.0f)));

    layout.add (group ("whammy", "WHAMMY",
        makeSwitch (id::whEnabled, addedInVersion3, "WHAMMY", false),
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
        makeMix (id::hmMix, "HARMONIZER", 50.0f)));

    layout.add (group ("amp", "Ampli",
        makeSwitch (id::ampEnabled, addedInVersion2, "Ampli", true),
        makeDecibels (id::ampGain, addedInVersion2, "Ampli gain", -24.0f, 24.0f, 0.0f),
        makeDecibels (id::ampBass, addedInVersion2, "Ampli graves", -12.0f, 12.0f, 0.0f),
        makeDecibels (id::ampMid, addedInVersion2, utf8 ("Ampli médiums"), -12.0f, 12.0f, 0.0f),
        makeDecibels (id::ampTreble, addedInVersion2, "Ampli aigus", -12.0f, 12.0f, 0.0f),
        makeDecibels (id::ampLevel, addedInVersion2, "Ampli niveau", -24.0f, 24.0f, 0.0f),
        makeMix (id::ampMix, "Ampli", 100.0f)));

    layout.add (group ("cab", "Cab",
        makeSwitch (id::cabEnabled, addedInVersion2, "Cab", true),
        makeMix (id::cabMix, "Cab", 100.0f)));

    layout.add (group ("shimmer", "SHIMMER",
        makeSwitch (id::shEnabled, addedInVersion4, "SHIMMER", false),
        makeChoice (id::shInterval, "SHIMMER intervalle", labelsOf (shimmerChoices), 0, addedInVersion4),
        makeFloat (id::shDecay, addedInVersion4, utf8 ("SHIMMER durée"),
                   { 0.5f, 30.0f, 0.1f, 0.4f }, 6.0f, "s", formatSeconds),
        makePercent (id::shAmount, "SHIMMER shimmer", 50.0f, 100.0f, addedInVersion4),
        makeFloat (id::shTone, addedInVersion4, utf8 ("SHIMMER tonalité"),
                   { 500.0f, 16000.0f, 1.0f, 0.3f }, 6000.0f, "Hz", formatHertz),
        makeMix (id::shMix, "SHIMMER", 30.0f)));

    layout.add (group ("poly", "POLY",
        makeSwitch (id::poEnabled, addedInVersion4, "POLY", false),
        makeFloat (id::poInterval1, addedInVersion4, "POLY intervalle 1",
                   { -24.0f, 24.0f, 0.01f }, 12.0f, "dt", formatSemitones),
        makePercent (id::poLevel1, "POLY niveau 1", 60.0f, 100.0f, addedInVersion4),
        makeFloat (id::poInterval2, addedInVersion4, "POLY intervalle 2",
                   { -24.0f, 24.0f, 0.01f }, -12.0f, "dt", formatSemitones),
        makePercent (id::poLevel2, "POLY niveau 2", 0.0f, 100.0f, addedInVersion4),
        makeFloat (id::poFormant, addedInVersion4, "POLY formants",
                   { -12.0f, 12.0f, 0.1f }, 0.0f, "dt", formatSemitones),
        makePercent (id::poWidth, "POLY largeur", 60.0f, 100.0f, addedInVersion4),
        makeMix (id::poMix, "POLY", 50.0f)));

    layout.add (group ("freeze", "FREEZE",
        makeSwitch (id::frEnabled, addedInVersion4, "FREEZE", false),
        makeSwitch (id::frHold, addedInVersion4, "FREEZE figer", false),
        makeFloat (id::frAttack, addedInVersion4, "FREEZE attaque",
                   { 10.0f, 5000.0f, 1.0f, 0.3f }, 300.0f, "ms", formatMilliseconds),
        makeFloat (id::frRelease, addedInVersion4, utf8 ("FREEZE relâche"),
                   { 10.0f, 10000.0f, 1.0f, 0.3f }, 2000.0f, "ms", formatMilliseconds),
        makeFloat (id::frTone, addedInVersion4, utf8 ("FREEZE tonalité"),
                   { 500.0f, 16000.0f, 1.0f, 0.3f }, 8000.0f, "Hz", formatHertz),
        // FREEZE : la nappe s'ajoute au jeu, qui n'est jamais atténué. MIX = niveau de la nappe.
        makeMix (id::frMix, "FREEZE", 60.0f)));

    layout.add (group ("granular", "GRANULAR",
        makeSwitch (id::grEnabled, addedInVersion4, "GRANULAR", false),
        makeChoice (id::grMode, "GRANULAR mode", { "Nuage", "Stutter" }, 0, addedInVersion4),
        makeFloat (id::grSize, addedInVersion4, "GRANULAR taille",
                   { 10.0f, 1000.0f, 1.0f, 0.4f }, 120.0f, "ms", formatMilliseconds),
        makeFloat (id::grDensity, addedInVersion4, utf8 ("GRANULAR densité"),
                   { 1.0f, 60.0f, 0.1f, 0.5f }, 12.0f, "/s", formatPerSecond),
        makeFloat (id::grPitch, addedInVersion4, "GRANULAR hauteur",
                   { -24.0f, 24.0f, 1.0f }, 0.0f, "dt", formatSemitones),
        makePercent (id::grSpread, "GRANULAR dispersion", 30.0f, 100.0f, addedInVersion4),
        makePercent (id::grReverse, utf8 ("GRANULAR inversés"), 25.0f, 100.0f, addedInVersion4),
        makeChoice (id::grDivision, "GRANULAR division", labelsOf (divisionChoices), 2, addedInVersion4),
        std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id::grRepeats, addedInVersion4 },
                                                   utf8 ("GRANULAR répétitions"), 1, 8, 4),
        makeMix (id::grMix, "GRANULAR", 35.0f),
        makeFloat (id::tempo, addedInVersion4, "Tempo (standalone)",
                   { 40.0f, 240.0f, 1.0f }, 120.0f, "BPM", formatBpm)));

    layout.add (group ("fs", "FREQ SHIFT",
        makeSwitch (id::fsEnabled, addedInVersion2, "FREQ SHIFT", false),
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
