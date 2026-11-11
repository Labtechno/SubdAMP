#pragma once

#include "subdamp/dsp/AmpModel.h"
#include "subdamp/dsp/GainStage.h"
#include "subdamp/dsp/ToneStack.h"

namespace subdamp::dsp
{

/** Bloc ampli complet : gain d'attaque → capture NAM → égaliseur 3 bandes → niveau. */
class AmpSection final : public MonoEffect
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

    void setGainDecibels (float decibels) noexcept         { drive.setGainDecibels (decibels); }
    void setToneGains (float bass, float mid, float treble) noexcept { tone.setGains (bass, mid, treble); }
    void setLevelDecibels (float decibels) noexcept        { level.setGainDecibels (decibels); }

    AmpModel& getModel() noexcept { return model; }
    const AmpModel& getModel() const noexcept { return model; }

private:
    GainStage drive;
    AmpModel model;
    ToneStack tone;
    GainStage level;
};

} // namespace subdamp::dsp
