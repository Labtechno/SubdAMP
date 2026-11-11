#include "plugin/StateVersioning.h"

#include "plugin/ChainOrder.h"

#include <vector>

namespace subdamp::state
{

namespace
{
    // Noms utilisés par AudioProcessorValueTreeState pour stocker chaque paramètre
    // (<PARAM id="..." value="..."/>). Ils sont privés dans JUCE, d'où cette copie.
    const juce::Identifier parameterType { "PARAM" };
    const juce::Identifier parameterIdProperty { "id" };
    const juce::Identifier parameterValueProperty { "value" };

    float parameterValue (const juce::ValueTree& state, const char* parameterId, float fallback)
    {
        const auto child = state.getChildWithProperty (parameterIdProperty, juce::String (parameterId));
        return child.isValid() ? static_cast<float> (child.getProperty (parameterValueProperty, fallback)) : fallback;
    }

    void setParameterValue (juce::ValueTree& state, const char* parameterId, float value)
    {
        auto child = state.getChildWithProperty (parameterIdProperty, juce::String (parameterId));

        if (! child.isValid())
        {
            child = juce::ValueTree (parameterType);
            child.setProperty (parameterIdProperty, juce::String (parameterId), nullptr);
            state.appendChild (child, nullptr);
        }

        child.setProperty (parameterValueProperty, value, nullptr);
    }

    void removeParameter (juce::ValueTree& state, const char* parameterId)
    {
        const auto child = state.getChildWithProperty (parameterIdProperty, juce::String (parameterId));

        if (child.isValid())
            state.removeChild (child, nullptr);
    }

    void migrateFrom1To2 (juce::ValueTree& state)
    {
        // Valeurs par défaut de la version 1 (figées : ce sont celles des anciens presets).
        //   positions : SUB, WHAMMY avant l'ampli ; HARMONIZER, FREQ SHIFT après.
        struct Movable { ModuleId module; const char* positionId; float defaultPosition; };
        const std::array<Movable, 4> movables {{
            { ModuleId::sub, "subPosition", 0.0f },
            { ModuleId::whammy, "whPosition", 0.0f },
            { ModuleId::harmonizer, "hmPosition", 1.0f },
            { ModuleId::freqShift, "fsPosition", 1.0f }
        }};

        const auto isAfterAmp = [&] (const Movable& movable)
        {
            return parameterValue (state, movable.positionId, movable.defaultPosition) > 0.5f;
        };

        std::vector<ModuleId> order { ModuleId::gate };

        for (const auto& movable : movables)
            if (! isAfterAmp (movable))
                order.push_back (movable.module);

        order.push_back (ModuleId::amp);
        order.push_back (ModuleId::cab);

        for (const auto& movable : movables)
            if (isAfterAmp (movable))
                order.push_back (movable.module);

        for (const auto texture : { ModuleId::shimmer, ModuleId::poly, ModuleId::freeze, ModuleId::granular })
            order.push_back (texture);

        ChainOrder chain {};
        std::copy (order.begin(), order.end(), chain.begin());
        state.setProperty (chainOrderProperty, juce::String (formatChainOrder (chain)), nullptr);

        // MIX qui garde l'équilibre direct / effet de l'ancien réglage (en %).
        const auto mixFromDirect = [] (float directPercent) { return 100.0f / (1.0f + directPercent * 0.01f); };
        const auto mixFromLevel = [] (float levelPercent) { const auto l = levelPercent * 0.01f; return 100.0f * l / (1.0f + l); };

        setParameterValue (state, "subMix", mixFromDirect (parameterValue (state, "subDry", 100.0f)));
        setParameterValue (state, "hmMix", mixFromDirect (parameterValue (state, "hmDirect", 100.0f)));
        setParameterValue (state, "shMix", mixFromLevel (parameterValue (state, "shLevel", 40.0f)));
        setParameterValue (state, "grMix", mixFromLevel (parameterValue (state, "grLevel", 50.0f)));
        setParameterValue (state, "frMix", parameterValue (state, "frLevel", 60.0f));
        setParameterValue (state, "poMix", 50.0f);

        for (const auto* retired : { "subPosition", "whPosition", "hmPosition", "fsPosition",
                                     "shSource", "poSource", "frSource", "grSource",
                                     "subDry", "hmDirect", "shLevel", "frLevel", "grLevel" })
            removeParameter (state, retired);
    }
}

MigrationResult migrateToCurrentVersion (juce::ValueTree& state)
{
    const int version = state.getProperty (versionProperty, 1);

    if (version > currentVersion)
        return MigrationResult::fromNewerVersion;

    if (version == currentVersion)
        return MigrationResult::upToDate;

    // Une étape par version, dans l'ordre.
    if (version < 2)
        migrateFrom1To2 (state);

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
