#pragma once

#include "subdamp/dsp/ProcessSpec.h"

#include <span>

namespace subdamp::dsp
{

/**
    Module TEXTURE : reçoit le signal mono et produit une nappe stéréo. Dans la chaîne, son
    emplacement (ChainSlot) la mélange au son direct, qui n'attend pas sa latence.

    Contrat temps réel : prepare() peut allouer ; reset() et process() jamais.
*/
class StereoTexture
{
public:
    virtual ~StereoTexture() = default;

    virtual void prepare (const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;

    /** Écrit la texture seule (sans le son d'entrée) dans left et right, qui sont écrasés. */
    virtual void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept = 0;

    /** Retard propre de la texture, en échantillons (0 si non significatif). */
    virtual int latencySamples() const noexcept { return 0; }
};

} // namespace subdamp::dsp
