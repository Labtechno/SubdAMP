#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace subdamp::params
{

/**
    Identifiants des paramètres.

    Ils sont enregistrés dans les sessions DAW et les presets, et servent de clés
    d'automation : ne JAMAIS les renommer ni les réutiliser pour autre chose.
    Pour changer le sens d'un paramètre, en créer un nouveau et migrer l'ancien
    (voir StateVersioning.h).
*/
namespace id
{
    // Entrée / sortie
    inline constexpr const char* inputGain  = "inputGain";
    inline constexpr const char* outputGain = "outputGain";
    inline constexpr const char* bypass     = "bypass";

    // Gate
    inline constexpr const char* gateEnabled   = "gateEnabled";
    inline constexpr const char* gateThreshold = "gateThreshold";
    inline constexpr const char* gateRelease   = "gateRelease";

    // Ampli (capture NAM + égaliseur)
    inline constexpr const char* ampEnabled = "ampEnabled";
    inline constexpr const char* ampGain    = "ampGain";
    inline constexpr const char* ampBass    = "ampBass";
    inline constexpr const char* ampMid     = "ampMid";
    inline constexpr const char* ampTreble  = "ampTreble";
    inline constexpr const char* ampLevel   = "ampLevel";

    // Cab (réponse impulsionnelle)
    inline constexpr const char* cabEnabled = "cabEnabled";

    // SUB (octaver analogique)
    inline constexpr const char* subEnabled  = "subEnabled";
    inline constexpr const char* subPosition = "subPosition";
    inline constexpr const char* subDry      = "subDry";
    inline constexpr const char* subDown1    = "subDown1";
    inline constexpr const char* subDown2    = "subDown2";
    inline constexpr const char* subUp       = "subUp";
    inline constexpr const char* subTone     = "subTone";

    // WHAMMY (transposition continue)
    inline constexpr const char* whEnabled     = "whEnabled";
    inline constexpr const char* whPosition    = "whPosition";
    inline constexpr const char* whInterval    = "whInterval";
    inline constexpr const char* whMode        = "whMode";
    inline constexpr const char* whGlide       = "whGlide";
    inline constexpr const char* whSensitivity = "whSensitivity";
    inline constexpr const char* whMix         = "whMix";

    // HARMONIZER (harmonies diatoniques)
    inline constexpr const char* hmEnabled  = "hmEnabled";
    inline constexpr const char* hmPosition = "hmPosition";
    inline constexpr const char* hmKey      = "hmKey";
    inline constexpr const char* hmScale    = "hmScale";
    inline constexpr const char* hmVoice1   = "hmVoice1";
    inline constexpr const char* hmLevel1   = "hmLevel1";
    inline constexpr const char* hmVoice2   = "hmVoice2";
    inline constexpr const char* hmLevel2   = "hmLevel2";
    inline constexpr const char* hmFormant  = "hmFormant";
    inline constexpr const char* hmDirect   = "hmDirect";

    // FREQ SHIFT (décaleur de fréquence)
    inline constexpr const char* fsEnabled  = "fsEnabled";
    inline constexpr const char* fsPosition = "fsPosition";
    inline constexpr const char* fsShift    = "fsShift";
    inline constexpr const char* fsMix      = "fsMix";
    inline constexpr const char* fsFeedback = "fsFeedback";
}

/** Valeurs du paramètre de position des modules de pitch. */
enum class Position
{
    beforeAmp = 0,
    afterAmp = 1
};

/** Version d'ajout des paramètres (exigée par JUCE pour les hôtes AU, ignorée en VST3). */
inline constexpr int addedInVersion1 = 1;
inline constexpr int addedInVersion2 = 2;
inline constexpr int addedInVersion3 = 3;

/**
    Intervalle d'une voix d'harmonie, en degrés de la gamme, pour chaque choix du paramètre
    hmVoice1 / hmVoice2 (même ordre). Aucune valeur : voix coupée.
*/
std::optional<int> harmonyVoiceSteps (int choiceIndex) noexcept;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace subdamp::params
