#include "subdamp/dsp/PolyShifter.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace subdamp::dsp
{

struct PolyShifter::Impl
{
    struct Voice
    {
        signalsmith::stretch::SignalsmithStretch<float> stretch { 1234 }; // graine fixe : rendu reproductible
        std::vector<float> output;
        LinearSmoother level;
        float semitones = 0.0f;
        float appliedSemitones = 1000.0f; // valeur impossible : force la configuration
        float appliedFormant = 1000.0f;
        bool running = false;
    };

    std::array<Voice, numVoices> voices;
    double sampleRate = 48000.0;
    float formantSemitones = 0.0f;
};

PolyShifter::PolyShifter() : impl (std::make_unique<Impl>()) {}
PolyShifter::~PolyShifter() = default;

void PolyShifter::prepare (const ProcessSpec& spec)
{
    impl->sampleRate = spec.sampleRate;

    for (auto& voice : impl->voices)
    {
        voice.stretch.presetDefault (1, static_cast<float> (spec.sampleRate), true);
        voice.output.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);
        voice.level.prepare (spec.sampleRate, 0.05);
        voice.appliedSemitones = 1000.0f;
        voice.appliedFormant = 1000.0f;
    }

    reset();
}

void PolyShifter::reset() noexcept
{
    for (auto& voice : impl->voices)
    {
        voice.stretch.reset();
        voice.level.setCurrentAndTarget (voice.level.getTarget());
        voice.running = false;
    }
}

int PolyShifter::latencySamples() const noexcept
{
    const auto& stretch = impl->voices[0].stretch;
    return stretch.inputLatency() + stretch.outputLatency();
}

void PolyShifter::setInterval (int voice, float semitones) noexcept
{
    impl->voices[static_cast<std::size_t> (voice)].semitones = std::clamp (semitones, -24.0f, 24.0f);
}

void PolyShifter::setVoiceLevel (int voice, float level) noexcept
{
    impl->voices[static_cast<std::size_t> (voice)].level.setTarget (std::max (0.0f, level));
}

void PolyShifter::setFormantSemitones (float semitones) noexcept
{
    impl->formantSemitones = std::clamp (semitones, -12.0f, 12.0f);
}

void PolyShifter::process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept
{
    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);

    const auto numSamples = static_cast<int> (input.size());

    for (std::size_t v = 0; v < impl->voices.size(); ++v)
    {
        auto& voice = impl->voices[v];

        // Voix coupée et silencieuse : pas de calcul. Elle repartira d'un état propre.
        if (voice.level.getTarget() <= 0.0f && ! voice.level.isSmoothing())
        {
            if (voice.running)
            {
                voice.stretch.reset();
                voice.running = false;
            }

            continue;
        }

        voice.running = true;

        if (voice.semitones != voice.appliedSemitones)
        {
            // Limite de tonalité à 8 kHz : au-dessus, le moteur privilégie le naturel au tonal.
            voice.stretch.setTransposeSemitones (voice.semitones, static_cast<float> (8000.0 / impl->sampleRate));
            voice.appliedSemitones = voice.semitones;
        }

        if (impl->formantSemitones != voice.appliedFormant)
        {
            // Formants déplacés par rapport à la transposition : à 0, ils suivent la hauteur
            // (son d'octaver classique, façon 12 cordes) ; -12 sur une octave haute les ramène en place.
            voice.stretch.setFormantSemitones (impl->formantSemitones, false);
            voice.appliedFormant = impl->formantSemitones;
        }

        const std::array<const float*, 1> inputs { input.data() };
        const std::array<float*, 1> outputs { voice.output.data() };
        voice.stretch.process (inputs, numSamples, outputs, numSamples);

        // Placement : voix 1 à gauche, voix 2 à droite, loi à puissance constante.
        const auto pan = (v == 0 ? -width : width);
        const auto angle = (pan + 1.0f) * 0.25f * std::numbers::pi_v<float>;
        const auto gainLeft = std::cos (angle);
        const auto gainRight = std::sin (angle);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto sample = voice.output[static_cast<std::size_t> (i)] * voice.level.getNext();
            left[static_cast<std::size_t> (i)] += gainLeft * sample;
            right[static_cast<std::size_t> (i)] += gainRight * sample;
        }
    }
}

} // namespace subdamp::dsp
