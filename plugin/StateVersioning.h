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
inline constexpr int currentVersion = 2;

/** Propriété portant la version, posée sur la racine de l'état. */
inline const juce::Identifier versionProperty { "stateVersion" };

/** Ordre de la chaîne en série (« gate sub amp… », voir ChainOrder.h), posé sur la racine. */
inline const juce::Identifier chainOrderProperty { "chainOrder" };

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

    Version 1 → 2 (chaîne en série) :
      - l'ordre est reconstruit depuis les anciennes positions : gate, modules placés avant
        l'ampli, ampli, cab, modules placés après, puis les textures (autrefois en parallèle) ;
      - les réglages DIRECT (SUB, HARMONIZER) et NIVEAU (textures) deviennent un MIX qui garde
        le même équilibre entre son direct et effet : MIX = 1 / (1 + direct) pour le SUB et
        l'HARMONIZER, MIX = niveau / (1 + niveau) pour SHIMMER et GRANULAR, MIX = niveau pour
        le FREEZE (qui s'ajoute au jeu), 50 % pour le POLY (voix au niveau du direct) ;
      - les paramètres retirés (positions, sources, DIRECT, NIVEAU) sont supprimés.
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

/**
    Retire de l'état chargé les paramètres qui n'existent plus (identifiants retirés, voir
    Parameters.h). Sans cela, ils resteraient dans chaque session réenregistrée. Les états
    d'une version plus récente ayant été refusés avant, un inconnu est forcément un retiré.
*/
void removeUnknownParameters (juce::ValueTree& state, std::span<const ParameterDefault> knownParameters);

} // namespace subdamp::state
