#include "subdamp/dsp/ToneStack.h"

#include <algorithm>

namespace subdamp::dsp
{

//==============================================================================
void ToneStack::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    needsUpdate = true;
    updateCoefficients();
    reset();
}

void ToneStack::reset() noexcept
{
    bass.reset();
    mid.reset();
    treble.reset();
}

void ToneStack::setGains (float newBassDb, float newMidDb, float newTrebleDb) noexcept
{
    if (newBassDb == bassDb && newMidDb == midDb && newTrebleDb == trebleDb)
        return;

    bassDb = newBassDb;
    midDb = newMidDb;
    trebleDb = newTrebleDb;
    needsUpdate = true;
}

void ToneStack::updateCoefficients() noexcept
{
    if (! needsUpdate)
        return;

    needsUpdate = false;
    const auto coefficients = coefficientsFor (sampleRate, bassDb, midDb, trebleDb);
    bass.setCoefficients (coefficients[0]);
    mid.setCoefficients (coefficients[1]);
    treble.setCoefficients (coefficients[2]);
}

std::array<BiquadCoefficients, 3> ToneStack::coefficientsFor (double sr, float bassGain, float midGain, float trebleGain) noexcept
{
    return { BiquadCoefficients::lowShelf (sr, 120.0, bassGain),
             BiquadCoefficients::peak (sr, 750.0, 0.8, midGain),
             BiquadCoefficients::highShelf (sr, 3000.0, trebleGain) };
}

void ToneStack::process (std::span<float> block) noexcept
{
    updateCoefficients();

    // Toujours calculé, même à plat : les filtres restent « chauds » et un réglage ne fait pas de clic.
    for (auto& sample : block)
        sample = treble.processSample (mid.processSample (bass.processSample (sample)));
}

//==============================================================================
void FallbackCabinet::prepare (const ProcessSpec& spec)
{
    const auto coefficients = coefficientsFor (spec.sampleRate);

    for (std::size_t i = 0; i < stages.size(); ++i)
        stages[i].setCoefficients (coefficients[i]);

    reset();
}

std::array<BiquadCoefficients, 7> FallbackCabinet::coefficientsFor (double sr) noexcept
{
    const auto lowPassHz = std::min (5000.0, sr * 0.45);

    return { BiquadCoefficients::highPass (sr, 70.0, butterworthQ),
             BiquadCoefficients::peak (sr, 110.0, 1.0, 3.0),
             BiquadCoefficients::peak (sr, 500.0, 0.8, -4.0),
             BiquadCoefficients::peak (sr, 2500.0, 1.5, 3.0),
             BiquadCoefficients::lowPass (sr, lowPassHz, butterworth4Q1),
             BiquadCoefficients::lowPass (sr, lowPassHz, butterworth4Q2),
             BiquadCoefficients::lowPass (sr, std::min (7500.0, sr * 0.45), butterworthQ) };
}

void FallbackCabinet::reset() noexcept
{
    for (auto& stage : stages)
        stage.reset();
}

void FallbackCabinet::process (std::span<float> block) noexcept
{
    for (auto& sample : block)
        for (auto& stage : stages)
            sample = stage.processSample (sample);
}

} // namespace subdamp::dsp
