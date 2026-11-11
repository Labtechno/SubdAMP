#pragma once

#include "subdamp/dsp/DelayLineShifter.h"
#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/MonoEffect.h"
#include "subdamp/dsp/OnsetDetector.h"

namespace subdamp::dsp
{

/**
    WHAMMY : transposition polyphonique jusqu'à ±2 octaves, en continu (catégorie JOUABLE,
    environ 15 ms).

    Trois modes, sans pédale d'expression ni MIDI :
      - fixe : la hauteur suit le potard (et son automation dans le DAW) ;
      - attaque, vers l'intervalle : chaque attaque repart de la note jouée et glisse vers
        l'intervalle (dive bomb, montée d'octave) ;
      - attaque, depuis l'intervalle : chaque attaque part de l'intervalle et glisse vers la
        note jouée (« scoop » d'arrivée).

    Le glissement est linéaire en demi-tons, comme une pédale enfoncée à vitesse constante.
    Près de l'unisson (moins d'un demi-ton), le module s'efface en fondu au profit du son
    direct : pas de filtrage en peigne ni de latence quand il ne transpose pas.
*/
class Whammy final : public MonoEffect
{
public:
    enum class Mode
    {
        fixed = 0,
        attackToInterval = 1,
        attackFromInterval = 2
    };

    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::playable; }

    void setInterval (float semitones) noexcept;
    void setMode (Mode newMode) noexcept;
    void setGlideMilliseconds (float milliseconds) noexcept;
    void setSensitivity (float amount) noexcept { onset.setSensitivity (amount); }
    void setMix (float amount) noexcept;

    /** Retard moyen du signal transposé, en échantillons. */
    int getLatencySamples() const noexcept { return shifter.averageLatencySamples(); }

    /** Transposition appliquée à l'instant présent (pour les tests et l'affichage). */
    float getCurrentSemitones() const noexcept { return currentSemitones; }

    /** En dessous de cet écart à l'unisson, le module laisse place au son direct. */
    static constexpr float unisonFadeSemitones = 0.5f;

private:
    double sampleRate = 48000.0;

    DelayLineShifter shifter;
    OnsetDetector onset;
    LinearSmoother interval, mix;

    Mode mode = Mode::fixed;
    float glideMs = 250.0f;
    float glideStep = 0.0f;
    float glideProgress = 1.0f;
    float currentSemitones = 0.0f;
};

} // namespace subdamp::dsp
