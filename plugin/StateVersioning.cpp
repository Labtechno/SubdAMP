#include "plugin/StateVersioning.h"

namespace subdamp::state
{

namespace
{
    // Noms utilisés par AudioProcessorValueTreeState pour stocker chaque paramètre
    // (<PARAM id="..." value="..."/>). Ils sont privés dans JUCE, d'où cette copie.
    const juce::Identifier parameterType { "PARAM" };
    const juce::Identifier parameterIdProperty { "id" };
    const juce::Identifier parameterValueProperty { "value" };
}

MigrationResult migrateToCurrentVersion (juce::ValueTree& state)
{
    const int version = state.getProperty (versionProperty, 1);

    if (version > currentVersion)
        return MigrationResult::fromNewerVersion;

    if (version == currentVersion)
        return MigrationResult::upToDate;

    // Les étapes de migration s'ajouteront ici, une par version, dans l'ordre :
    //     if (version < 2) migrateFrom1To2 (state);
    //     if (version < 3) migrateFrom2To3 (state);
    state.setProperty (versionProperty, currentVersion, nullptr);
    return MigrationResult::migrated;
}

void addMissingParameters (juce::ValueTree& state, std::span<const ParameterDefault> defaults)
{
    for (const auto& parameter : defaults)
    {
        if (state.getChildWithProperty (parameterIdProperty, parameter.id).isValid())
            continue;

        juce::ValueTree child (parameterType);
        child.setProperty (parameterIdProperty, parameter.id, nullptr);
        child.setProperty (parameterValueProperty, parameter.value, nullptr);
        state.appendChild (child, nullptr);
    }
}

} // namespace subdamp::state
