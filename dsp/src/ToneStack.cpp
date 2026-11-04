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
    bass.setCoefficients (BiquadCoefficients::lowShelf (sampleRate, 120.0, bassDb));
    mid.setCoefficients (BiquadCoefficients::peak (sampleRate, 750.0, 0.8, midDb));
    treble.setCoefficients (BiquadCoefficients::highShelf (sampleRate, 3000.0, trebleDb));
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
    const auto sr = spec.sampleRate;
    const auto lowPassHz = std::min (5000.0, sr * 0.45);

    stages[0].setCoefficients (BiquadCoefficients::highPass (sr, 70.0, butterworthQ));
    stages[1].setCoefficients (BiquadCoefficients::peak (sr, 110.0, 1.0, 3.0));
    stages[2].setCoefficients (BiquadCoefficients::peak (sr, 500.0, 0.8, -4.0));
    stages[3].setCoefficients (BiquadCoefficients::peak (sr, 2500.0, 1.5, 3.0));
    stages[4].setCoefficients (BiquadCoefficients::lowPass (sr, lowPassHz, butterworth4Q1));
    stages[5].setCoefficients (BiquadCoefficients::lowPass (sr, lowPassHz, butterworth4Q2));
    stages[6].setCoefficients (BiquadCoefficients::lowPass (sr, std::min (7500.0, sr * 0.45), butterworthQ));

    reset();
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
