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
    freqShift,
    shimmer,
    poly,
    freeze,
    granular
};

struct ModuleInfo
{
    ModuleId id;
    const char* name;              // titre du panneau
    const char* pedalName;         // nom court sur la pédale
    dsp::LatencyTier tier;
    const char* enabledParameter;
    const char* positionParameter; // série : avant / après l'ampli ; texture : source. Nul : fixe.
    bool isTexture;                // sur le bus parallèle
};

inline constexpr std::array<ModuleInfo, 11> moduleInfos {{
    { ModuleId::gate,       "GATE",       "GATE",    dsp::LatencyTier::live,     params::id::gateEnabled, nullptr,                 false },
    { ModuleId::sub,        "SUB",        "SUB",     dsp::LatencyTier::live,     params::id::subEnabled,  params::id::subPosition, false },
    { ModuleId::whammy,     "WHAMMY",     "WHAMMY",  dsp::LatencyTier::playable, params::id::whEnabled,   params::id::whPosition,  false },
    { ModuleId::harmonizer, "HARMONIZER", "HARMO",   dsp::LatencyTier::playable, params::id::hmEnabled,   params::id::hmPosition,  false },
    { ModuleId::amp,        "AMPLI",      "AMPLI",   dsp::LatencyTier::live,     params::id::ampEnabled,  nullptr,                 false },
    { ModuleId::cab,        "CAB",        "CAB",     dsp::LatencyTier::live,     params::id::cabEnabled,  nullptr,                 false },
    { ModuleId::freqShift,  "FREQ SHIFT", "FREQ",    dsp::LatencyTier::live,     params::id::fsEnabled,   params::id::fsPosition,  false },
    { ModuleId::shimmer,    "SHIMMER",    "SHIMMER", dsp::LatencyTier::texture,  params::id::shEnabled,   params::id::shSource,    true },
    { ModuleId::poly,       "POLY",       "POLY",    dsp::LatencyTier::texture,  params::id::poEnabled,   params::id::poSource,    true },
    { ModuleId::freeze,     "FREEZE",     "FREEZE",  dsp::LatencyTier::texture,  params::id::frEnabled,   params::id::frSource,    true },
    { ModuleId::granular,   "GRANULAR",   "GRAIN",   dsp::LatencyTier::texture,  params::id::grEnabled,   params::id::grSource,    true }
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

/** Textures du bus parallèle, dans l'ordre d'affichage. */
inline constexpr std::array<ModuleId, 4> textureOrder { ModuleId::shimmer, ModuleId::poly, ModuleId::freeze, ModuleId::granular };

} // namespace subdamp
