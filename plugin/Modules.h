#pragma once

#include "plugin/ChainOrder.h"
#include "plugin/Parameters.h"
#include "subdamp/dsp/MonoEffect.h"

#include <array>

namespace subdamp
{

struct ModuleInfo
{
    ModuleId id;
    const char* name;              // titre du panneau
    const char* pedalName;         // nom court sur la pédale
    dsp::LatencyTier tier;
    const char* enabledParameter;
    const char* mixParameter;      // potard MIX (dry / wet) ; nul : pas de MIX (GATE), toujours 100 %
    bool isTexture;                // sortie stéréo, latence propre non compensée
};

/** Modules de la chaîne, partagés par le processeur et l'interface (dans l'ordre de ModuleId). */
inline constexpr std::array<ModuleInfo, numModules> moduleInfos {{
    { ModuleId::gate,       "GATE",       "GATE",    dsp::LatencyTier::live,     params::id::gateEnabled, nullptr,             false },
    { ModuleId::sub,        "SUB",        "SUB",     dsp::LatencyTier::live,     params::id::subEnabled,  params::id::subMix,  false },
    { ModuleId::whammy,     "WHAMMY",     "WHAMMY",  dsp::LatencyTier::playable, params::id::whEnabled,   params::id::whMix,   false },
    { ModuleId::harmonizer, "HARMONIZER", "HARMO",   dsp::LatencyTier::playable, params::id::hmEnabled,   params::id::hmMix,   false },
    { ModuleId::amp,        "AMPLI",      "AMPLI",   dsp::LatencyTier::live,     params::id::ampEnabled,  params::id::ampMix,  false },
    { ModuleId::cab,        "CAB",        "CAB",     dsp::LatencyTier::live,     params::id::cabEnabled,  params::id::cabMix,  false },
    { ModuleId::freqShift,  "FREQ SHIFT", "FREQ",    dsp::LatencyTier::live,     params::id::fsEnabled,   params::id::fsMix,   false },
    { ModuleId::shimmer,    "SHIMMER",    "SHIMMER", dsp::LatencyTier::texture,  params::id::shEnabled,   params::id::shMix,   true },
    { ModuleId::poly,       "POLY",       "POLY",    dsp::LatencyTier::texture,  params::id::poEnabled,   params::id::poMix,   true },
    { ModuleId::freeze,     "FREEZE",     "FREEZE",  dsp::LatencyTier::texture,  params::id::frEnabled,   params::id::frMix,   true },
    { ModuleId::granular,   "GRANULAR",   "GRAIN",   dsp::LatencyTier::texture,  params::id::grEnabled,   params::id::grMix,   true }
}};

inline const ModuleInfo& infoFor (ModuleId id) noexcept
{
    return moduleInfos[indexOf (id)];
}

} // namespace subdamp
