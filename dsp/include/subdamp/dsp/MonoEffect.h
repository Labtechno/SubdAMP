#pragma once

#include "subdamp/dsp/ProcessSpec.h"

#include <span>

namespace subdamp::dsp
{

/**
    Catégorie de latence d'un module, affichée en badge dans l'interface.
      - live     : moins de 3 ms, jouable sans aucune gêne ;
      - playable : environ 5 à 25 ms, jouable mais perceptible sur les attaques ;
      - texture  : plus de 40 ms, uniquement en parallèle du son sec.
*/
enum class LatencyTier
{
    live,
    playable,
    texture
};

/**
    Module de la chaîne (gate, octaver, frequency shifter...) traitant un bloc mono sur place.

    Contrat temps réel : prepare() peut allouer ; reset() et process() jamais.
*/
class MonoEffect
{
public:
    virtual ~MonoEffect() = default;

    virtual void prepare (const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void process (std::span<float> block) noexcept = 0;
    virtual LatencyTier latencyTier() const noexcept = 0;
};

} // namespace subdamp::dsp
