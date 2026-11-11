#include "subdamp/dsp/ChainSlot.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>

namespace subdamp::dsp
{

std::pair<float, float> ChainSlot::mixGains (float mixAmount, MixLaw mixLaw) noexcept
{
    if (mixLaw == MixLaw::linear)
        return { 1.0f - mixAmount, mixAmount };

    const auto angle = mixAmount * 1.57079632679f; // MIX × 90°
    return { std::cos (angle), std::sin (angle) };
}

void ChainSlot::prepare (const ProcessSpec& spec)
{
    if (mono != nullptr)
        mono->prepare (spec);
    else
        texture->prepare (spec);

    enable.prepare (spec.sampleRate, 0.015);
    mix.prepare (spec.sampleRate, 0.02);

    const auto size = static_cast<std::size_t> (spec.maxBlockSize);
    monoBuffer.assign (size, 0.0f);
    fadeBuffer.assign (texture != nullptr ? size : 0, 0.0f);
    wetLeft.assign (texture != nullptr ? size : 0, 0.0f);
    wetRight.assign (texture != nullptr ? size : 0, 0.0f);

    // Retard du sec : jusqu'à 50 ms (le retard propre d'un module « live » reste très en dessous).
    const auto ringSize = mono != nullptr ? std::bit_ceil (static_cast<std::size_t> (0.05 * spec.sampleRate) + 1) : 0;
    dryRingLeft.assign (ringSize, 0.0f);
    dryRingRight.assign (ringSize, 0.0f);
    dryMask = ringSize > 0 ? ringSize - 1 : 0;
    dryDelay = std::min (dryDelay, static_cast<int> (dryMask));

    sleepAfter = static_cast<int> (0.5 * spec.sampleRate);
    reset();
}

void ChainSlot::reset() noexcept
{
    if (mono != nullptr)
        mono->reset();
    else
        texture->reset();

    enable.setCurrentAndTarget (enabled ? 1.0f : 0.0f);
    mix.setCurrentAndTarget (mix.getTarget());
    std::fill (dryRingLeft.begin(), dryRingLeft.end(), 0.0f);
    std::fill (dryRingRight.begin(), dryRingRight.end(), 0.0f);
    dryWrite = 0;
    idle = ! enabled;
    silentSamples = 0;
}

void ChainSlot::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled = shouldBeEnabled;
    enable.setTarget (enabled ? 1.0f : 0.0f);
}

void ChainSlot::setDryDelaySamples (int samples) noexcept
{
    dryDelay = std::clamp (samples, 0, static_cast<int> (dryMask));
}

void ChainSlot::process (std::span<float> left, std::span<float> right) noexcept
{
    assert (left.size() == right.size());
    assert (left.size() <= monoBuffer.size());

    if (mono != nullptr)
        processMono (left, right);
    else
        processTexture (left, right);
}

void ChainSlot::processMono (std::span<float> left, std::span<float> right) noexcept
{
    // Arrêt complet : le signal passe tel quel, sans aucun calcul.
    if (! enabled && ! enable.isSmoothing())
    {
        idle = true;
        return;
    }

    // Remise en marche : on repart d'un module vierge (filtres, enveloppes, retards).
    if (idle)
    {
        mono->reset();
        std::fill (dryRingLeft.begin(), dryRingLeft.end(), 0.0f);
        std::fill (dryRingRight.begin(), dryRingRight.end(), 0.0f);
        idle = false;
    }

    const auto size = left.size();
    const std::span<float> processed { monoBuffer.data(), size };

    for (std::size_t i = 0; i < size; ++i)
        processed[i] = 0.5f * (left[i] + right[i]);

    mono->process (processed);

    for (std::size_t i = 0; i < size; ++i)
    {
        const auto e = enable.getNext();
        const auto m = mix.getNext();

        // Sec aligné sur le traité (retard propre du module), pour la partie mélangée.
        auto alignedLeft = left[i], alignedRight = right[i];

        if (! dryRingLeft.empty())
        {
            dryRingLeft[dryWrite] = left[i];
            dryRingRight[dryWrite] = right[i];
            const auto read = (dryWrite - static_cast<std::size_t> (dryDelay)) & dryMask;
            alignedLeft = dryRingLeft[read];
            alignedRight = dryRingRight[read];
            dryWrite = (dryWrite + 1) & dryMask;
        }

        const auto [dryGain, wetGain] = mixGains (m, law);
        const auto wet = processed[i] * wetGain;
        left[i] = left[i] * (1.0f - e) + e * (alignedLeft * dryGain + wet);
        right[i] = right[i] * (1.0f - e) + e * (alignedRight * dryGain + wet);
    }
}

void ChainSlot::processTexture (std::span<float> left, std::span<float> right) noexcept
{
    if (idle)
    {
        if (! enabled)
            return;

        // Réveil : on repart d'une texture vierge, sans reste de la dernière utilisation.
        texture->reset();
        idle = false;
        silentSamples = 0;
    }

    const auto size = left.size();
    const std::span<float> input { monoBuffer.data(), size };
    const std::span<float> fade { fadeBuffer.data(), size };
    const std::span<float> wetL { wetLeft.data(), size };
    const std::span<float> wetR { wetRight.data(), size };

    // L'entrée suit l'interrupteur ; la sortie, elle, continue (queue de réverbe, derniers grains).
    for (std::size_t i = 0; i < size; ++i)
    {
        fade[i] = enable.getNext();
        input[i] = 0.5f * (left[i] + right[i]) * fade[i];
    }

    texture->process (input, wetL, wetR);

    float peak = 0.0f;

    for (std::size_t i = 0; i < size; ++i)
    {
        peak = std::max (peak, std::max (std::abs (wetL[i]), std::abs (wetR[i])));

        // Interrupteur coupé (fade = 0) : le sec repasse en entier, la queue continue au MIX.
        const auto [dryAtMix, wetGain] = mixGains (mix.getNext(), law);
        const auto dryGain = wetAddsToDry ? 1.0f : 1.0f - fade[i] * (1.0f - dryAtMix);
        left[i] = left[i] * dryGain + wetL[i] * wetGain;
        right[i] = right[i] * dryGain + wetR[i] * wetGain;
    }

    if (enabled || enable.isSmoothing())
    {
        silentSamples = 0;
        return;
    }

    silentSamples = peak < silence ? silentSamples + static_cast<int> (size) : 0;

    if (silentSamples >= sleepAfter)
        idle = true;
}

} // namespace subdamp::dsp
