#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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
    inline constexpr const char* inputGain  = "inputGain";
    inline constexpr const char* outputGain = "outputGain";
    inline constexpr const char* bypass     = "bypass";
}

/** Version d'ajout des paramètres (exigée par JUCE pour les hôtes AU, ignorée en VST3). */
inline constexpr int addedInVersion1 = 1;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace subdamp::params
