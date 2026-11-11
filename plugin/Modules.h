#pragma once

#include "plugin/Parameters.h"
#include "subdamp/dsp/MonoEffect.h"

#include <array>

namespace subdamp
{

/** Modules de la chaîne, partagés par le processeur et l'interface. */
enum class ModuleId
{
    gate,
    sub,
    whammy,
    harmonizer,
    amp,
    cab,
    freqShift
};

struct ModuleInfo
{
    ModuleId id;
    const char* name;            // titre du panneau (UTF-8)
    const char* pedalName;       // nom court sur la pédale
    dsp::LatencyTier tier;
    const char* enabledParameter;
    const char* positionParameter; // nul : module fixe dans la chaîne
};

inline constexpr std::array<ModuleInfo, 7> moduleInfos {{
    { ModuleId::gate,       "GATE",       "GATE",   dsp::LatencyTier::live,     params::id::gateEnabled, nullptr },
    { ModuleId::sub,        "SUB",        "SUB",    dsp::LatencyTier::live,     params::id::subEnabled,  params::id::subPosition },
    { ModuleId::whammy,     "WHAMMY",     "WHAMMY", dsp::LatencyTier::playable, params::id::whEnabled,   params::id::whPosition },
    { ModuleId::harmonizer, "HARMONIZER", "HARMO",  dsp::LatencyTier::playable, params::id::hmEnabled,   params::id::hmPosition },
    { ModuleId::amp,        "AMPLI",      "AMPLI",  dsp::LatencyTier::live,     params::id::ampEnabled,  nullptr },
    { ModuleId::cab,        "CAB",        "CAB",    dsp::LatencyTier::live,     params::id::cabEnabled,  nullptr },
    { ModuleId::freqShift,  "FREQ SHIFT", "FREQ",   dsp::LatencyTier::live,     params::id::fsEnabled,   params::id::fsPosition }
}};

inline const ModuleInfo& infoFor (ModuleId id) noexcept
{
    return moduleInfos[static_cast<std::size_t> (id)];
}

/**
    Ordre de traitement des modules déplaçables à l'intérieur d'un même emplacement
    (avant ou après l'ampli) : octaver d'abord (suivi le plus propre), FREQ SHIFT en dernier
    (ses sons inharmoniques gêneraient la détection de hauteur de l'HARMONIZER).
*/
inline constexpr std::array<ModuleId, 4> movableOrder { ModuleId::sub, ModuleId::whammy, ModuleId::harmonizer, ModuleId::freqShift };

} // namespace subdamp
