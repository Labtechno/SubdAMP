#pragma once

#include "subdamp/dsp/MonoEffect.h"
#include "subdamp/dsp/ToneStack.h"

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <vector>

namespace subdamp
{

/**
    Bloc cab : convolution par une réponse impulsionnelle (IR) chargée par l'utilisateur,
    ou cabinet de secours par filtres tant qu'aucune IR n'est prête.

    La convolution de JUCE est sans latence (partitionnement non uniforme), charge et
    rééchantillonne l'IR sur son propre thread, et fait un fondu enchaîné entre deux IR.
*/
class CabSection final : public dsp::MonoEffect
{
public:
    void prepare (const dsp::ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    dsp::LatencyTier latencyTier() const noexcept override { return dsp::LatencyTier::live; }

    /** Depuis n'importe quel thread hors audio. Le fichier doit avoir été validé. */
    void loadImpulseResponse (const juce::File& file);

    /** Revient au cabinet de secours. */
    void clearImpulseResponse() noexcept;

private:
    juce::dsp::Convolution convolution;
    dsp::FallbackCabinet fallback;
    std::vector<float> scratch;
    std::atomic<bool> useImpulseResponse { false };
};

} // namespace subdamp
