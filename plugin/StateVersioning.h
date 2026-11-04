#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <span>

namespace subdamp::state
{

/**
    Version du format de l'état sauvegardé (sessions DAW, presets).

    À incrémenter à chaque changement incompatible (paramètre renommé, plage modifiée,
    structure réorganisée), en ajoutant l'étape correspondante dans migrateToCurrentVersion().
    Ajouter un paramètre ne nécessite PAS de nouvelle version : addMissingParameters() s'en charge.
*/
inline constexpr int currentVersion = 1;

/** Propriété portant la version, posée sur la racine de l'état. */
inline const juce::Identifier versionProperty { "stateVersion" };

struct ParameterDefault
{
    juce::String id;
    float value; // valeur dénormalisée (dans l'unité du paramètre)
};

enum class MigrationResult
{
    upToDate,
    migrated,
    fromNewerVersion // état produit par une version plus récente de SubdAmp : à ne pas charger
};

/**
    Fait évoluer un état chargé jusqu'à la version courante, sur place.
    Un état sans numéro de version est considéré comme de version 1 (première version publiée).
*/
MigrationResult migrateToCurrentVersion (juce::ValueTree& state);

/**
    Ajoute, avec leur valeur par défaut, les paramètres absents de l'état chargé.

    Indispensable avec AudioProcessorValueTreeState : un paramètre absent de l'état
    chargé garde sa valeur COURANTE au lieu de revenir à sa valeur par défaut. Charger un
    ancien preset après avoir touché un paramètre ajouté depuis donnerait alors un son
    différent à chaque fois.
*/
void addMissingParameters (juce::ValueTree& state, std::span<const ParameterDefault> defaults);

} // namespace subdamp::state
