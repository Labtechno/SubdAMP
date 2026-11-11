#include "subdamp/dsp/Harmonizer.h"

#include "subdamp/dsp/Interpolation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace subdamp::dsp
{

void Harmonizer::prepare (const ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    tracker.prepare (sampleRate);

    // Historique : 3,5 périodes de la note la plus grave, plus la tenue (45 ms) pendant
    // laquelle on relit la dernière période suivie.
    holdSamples = static_cast<int> (std::lround (0.045 * sampleRate));
    history.assign (std::bit_ceil (static_cast<std::size_t> (4.0 * sampleRate / 60.0) + static_cast<std::size_t> (holdSamples) + 64), 0.0f);
    historyMask = history.size() - 1;

    for (int i = 0; i <= hannSize; ++i)
        hannTable[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / hannSize));

    for (auto& voice : voices)
        voice.level.prepare (sampleRate, 0.02);

    direct.prepare (sampleRate, 0.02);

    envelopeAttack = static_cast<float> (std::exp (-1.0 / (0.005 * sampleRate)));
    envelopeRelease = static_cast<float> (std::exp (-1.0 / (0.04 * sampleRate)));
    levelFollow = static_cast<float> (1.0 - std::exp (-1.0 / (0.08 * sampleRate)));

    reset();
}

void Harmonizer::reset() noexcept
{
    tracker.reset();
    std::fill (history.begin(), history.end(), 0.0f);
    written = 0;

    haveMark = false;
    marksStale = false;
    nextMarkExact = -1.0;
    holdRemaining = 0;
    currentNote = -1;

    for (auto& voice : voices)
    {
        for (auto& grain : voice.grains)
            grain.active = false;

        voice.nextGrainTime = -1.0;
        voice.envelope = 0.0f;
        voice.energy = 0.0f;
        voice.compensation = 1.0f;
        voice.level.setCurrentAndTarget (voice.level.getTarget());
    }

    inputEnergy = 0.0f;

    direct.setCurrentAndTarget (direct.getTarget());
    latencyAverage = 0.0;
    latency.store (0, std::memory_order_relaxed);
}

//==============================================================================
void Harmonizer::setKey (int pitchClass) noexcept
{
    const auto newKey = ((pitchClass % 12) + 12) % 12;

    if (newKey != key)
    {
        key = newKey;
        updateShifts();
    }
}

void Harmonizer::setScale (Scale newScale) noexcept
{
    if (newScale != scale)
    {
        scale = newScale;
        updateShifts();
    }
}

void Harmonizer::setVoiceInterval (int voice, std::optional<int> steps) noexcept
{
    auto& v = voices[static_cast<std::size_t> (voice)];

    if (v.steps != steps)
    {
        v.steps = steps;
        updateShifts();
    }
}

void Harmonizer::setVoiceLevel (int voice, float level) noexcept
{
    voices[static_cast<std::size_t> (voice)].level.setTarget (std::max (0.0f, level));
}

void Harmonizer::setDirectLevel (float level) noexcept
{
    direct.setTarget (std::max (0.0f, level));
}

void Harmonizer::setFormantSemitones (float semitones) noexcept
{
    formantFactor = semitonesToRatio (std::clamp (semitones, -12.0f, 12.0f));
}

int Harmonizer::getVoiceShiftSemitones (int voice) const noexcept
{
    return voices[static_cast<std::size_t> (voice)].shiftSemitones;
}

void Harmonizer::updateShifts() noexcept
{
    const auto noteInKey = currentNote >= 0 ? currentNote - key : 0;

    for (auto& voice : voices)
    {
        voice.shiftSemitones = voice.steps.has_value() ? diatonicShift (noteInKey, scale, *voice.steps) : 0;
        voice.ratio = std::exp2 (voice.shiftSemitones / 12.0);
    }
}

//==============================================================================
void Harmonizer::onNewEstimate() noexcept
{
    if (! tracker.isVoiced())
    {
        // Suivi perdu : on garde la dernière marque, relue pendant la tenue.
        if (haveMark && ! marksStale)
        {
            marksStale = true;
            holdRemaining = holdSamples;
        }

        return;
    }

    // Suivi retrouvé : même note (période à 3 % près, dans la tenue) → les marques continuent ;
    // sinon nouvelle note → nouvelles marques, les grains en cours finissent en fondu.
    if (marksStale)
    {
        const auto samePeriod = std::abs (tracker.getPeriod() - lastMarkPeriod) < 0.03f * lastMarkPeriod;

        if (! samePeriod || holdRemaining <= 0)
        {
            haveMark = false;
            nextMarkExact = -1.0;
        }

        marksStale = false;
        holdRemaining = 0;
    }

    // Note la plus proche, avec 0,1 demi-ton d'hystérésis : un vibrato ou un bend léger
    // autour d'une limite ne fait pas alterner l'intervalle d'harmonie.
    const auto frequency = sampleRate / tracker.getPeriod();
    const auto midi = 69.0 + 12.0 * std::log2 (frequency / 440.0);

    if (currentNote < 0 || std::abs (midi - currentNote) > 0.6)
    {
        currentNote = static_cast<int> (std::lround (midi));
        updateShifts();
    }
}

void Harmonizer::placeMarks (std::int64_t now) noexcept
{
    if (! tracker.isVoiced())
        return;

    const auto period = tracker.getPeriod();
    const auto half = static_cast<int> (period * 0.5f);
    const auto search = std::max (1, static_cast<int> (period / 16.0f));

    if (! haveMark || nextMarkExact < 0.0)
    {
        // Première marque d'une note : sur le pic de la dernière période complète. Les suivantes
        // s'y alignent par corrélation : chaque grain est centré sur la même phase de l'onde,
        // là où l'énergie est concentrée (condition d'un PSOLA propre).
        const auto latest = now - (half + search + 2);
        auto peak = latest;

        for (auto i = latest - static_cast<std::int64_t> (period); i < latest; ++i)
            if (history[static_cast<std::size_t> (i) & historyMask] > history[static_cast<std::size_t> (peak) & historyMask])
                peak = i;

        lastMark = peak;
        lastMarkPeriod = period;
        nextMarkExact = static_cast<double> (lastMark) + period;
        haveMark = true;
        return;
    }

    while (static_cast<double> (now) >= nextMarkExact + search + half + 2)
    {
        auto mark = static_cast<std::int64_t> (std::llround (nextMarkExact));

        // Recalage : la position qui ressemble le plus à la période précédente (même phase de l'onde).
        if (std::abs (period - lastMarkPeriod) < 0.1f * period)
        {
            const auto at = [this] (std::int64_t index) { return history[static_cast<std::size_t> (index) & historyMask]; };
            auto bestScore = -1.0e30f;
            auto best = mark;

            for (auto candidate = mark - search; candidate <= mark + search; ++candidate)
            {
                float score = 0.0f;

                for (int j = -half; j <= half; ++j)
                    score += at (lastMark + j) * at (candidate + j);

                if (score > bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }

            mark = best;
        }

        lastMark = mark;
        lastMarkPeriod = period;
        nextMarkExact = static_cast<double> (mark) + period;
    }
}

void Harmonizer::emitGrain (Voice& voice, std::int64_t now) noexcept
{
    auto slot = std::find_if (voice.grains.begin(), voice.grains.end(), [] (const Grain& g) { return ! g.active; });

    if (slot == voice.grains.end())
        return;

    const auto period = static_cast<double> (lastMarkPeriod);
    const auto length = 2.0 * period / formantFactor;

    // Le grain aurait dû commencer à nextGrainTime (fractionnaire) : on compense le décalage
    // dans la lecture, pour une hauteur exacte au centième de demi-ton.
    const auto offset = std::clamp (static_cast<double> (now) - voice.nextGrainTime, 0.0, length - 1.0);

    slot->source = static_cast<double> (lastMark) - period + offset * formantFactor;
    slot->step = formantFactor;
    slot->phaseStep = static_cast<float> (1.0 / length);
    slot->phase = static_cast<float> (offset / length);
    slot->active = true;

    // Retard des voix : centre du grain en sortie moins centre de sa source.
    const auto delay = static_cast<double> (now) - offset + 0.5 * length - static_cast<double> (lastMark);
    latencyAverage += 0.05 * (delay - latencyAverage);
}

float Harmonizer::hann (float phase) const noexcept
{
    const auto position = phase * static_cast<float> (hannSize);
    const auto index = std::min (static_cast<int> (position), hannSize - 1);
    const auto fraction = position - static_cast<float> (index);
    const auto a = hannTable[static_cast<std::size_t> (index)];
    return a + fraction * (hannTable[static_cast<std::size_t> (index) + 1] - a);
}

bool Harmonizer::canReadFrozenPeriod (std::int64_t now) const noexcept
{
    // La période relue (deux périodes autour de la dernière marque) doit encore être dans l'historique.
    const auto oldest = static_cast<double> (lastMark) - 2.0 * static_cast<double> (lastMarkPeriod);
    return static_cast<double> (now) - oldest < static_cast<double> (history.size()) - 8.0;
}

float Harmonizer::renderVoice (Voice& voice, std::int64_t now) noexcept
{
    // Note suivie, ou tenue après une perte passagère : la voix continue.
    const auto tracking = tracker.isVoiced() && haveMark && ! marksStale;
    const auto holding = marksStale && holdRemaining > 0 && haveMark && canReadFrozenPeriod (now);
    const auto active = voice.steps.has_value() && (tracking || holding);

    if (active)
    {
        // Reprise après un silence : on repart de maintenant plutôt que de rattraper le retard.
        if (voice.nextGrainTime < 0.0 || voice.nextGrainTime < static_cast<double> (now) - 4.0 * lastMarkPeriod)
            voice.nextGrainTime = static_cast<double> (now);

        while (static_cast<double> (now) >= voice.nextGrainTime)
        {
            emitGrain (voice, now);
            voice.nextGrainTime += static_cast<double> (lastMarkPeriod) / voice.ratio;
        }
    }
    else
    {
        voice.nextGrainTime = -1.0;
    }

    const auto target = active ? 1.0f : 0.0f;
    voice.envelope = target + (voice.envelope - target) * (target > voice.envelope ? envelopeAttack : envelopeRelease);

    float sum = 0.0f;

    for (auto& grain : voice.grains)
    {
        if (! grain.active)
            continue;

        sum += hann (grain.phase) * readHermite (history.data(), historyMask, grain.source);

        grain.source += grain.step;
        grain.phase += grain.phaseStep;

        if (grain.phase >= 1.0f)
            grain.active = false;
    }

    // Égalisation du niveau : selon l'intervalle et la forme d'onde, le PSOLA peut perdre
    // quelques dB (octave en dessous surtout). La voix est ramenée au niveau de la note jouée,
    // dans une plage de ±6 dB, avec un temps de réponse lent (pas de pompage audible).
    // Figée pendant la tenue : l'entrée est alors l'attaque de la note suivante.
    if (tracking)
    {
        voice.energy += levelFollow * (sum * sum - voice.energy);
        const auto wanted = std::sqrt (inputEnergy / std::max (voice.energy, 1.0e-12f));
        voice.compensation += levelFollow * (std::clamp (wanted, 0.5f, 2.0f) - voice.compensation);
    }

    return sum * voice.compensation * voice.envelope * voice.level.getNext();
}

void Harmonizer::process (std::span<float> block) noexcept
{
    for (auto& sample : block)
    {
        const auto input = sample;
        const auto now = written;

        history[static_cast<std::size_t> (now) & historyMask] = input;
        ++written;

        tracker.push (input);
        inputEnergy += levelFollow * (input * input - inputEnergy);

        if (tracker.takeNewEstimate())
            onNewEstimate();

        if (holdRemaining > 0)
            --holdRemaining;

        if (! marksStale)
            placeMarks (now);

        float harmony = 0.0f;

        for (auto& voice : voices)
            harmony += renderVoice (voice, now);

        sample = direct.getNext() * input + harmony;
    }

    latency.store (haveMark ? static_cast<int> (latencyAverage) : 0, std::memory_order_relaxed);
}

} // namespace subdamp::dsp
