#pragma once

#include "subdamp/dsp/Biquad.h"
#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/MonoEffect.h"

#include <array>

namespace subdamp::dsp
{

/**
    Octaver analogique façon Boss OC-2 : module SUB, latence nulle, monophonique.

    Principe (aucune détection de hauteur, donc aucune attente) :
      1. un filtre passe-bas isole la fondamentale de la note jouée ;
      2. à chaque passage par zéro montant, une bascule change d'état : elle produit un
         carré à la moitié de la fréquence (-1 octave), une seconde bascule à son tour donne -2 octaves ;
      3. la fondamentale filtrée est multipliée par ces carrés. Comme le changement de signe
         a lieu exactement au passage par zéro, le signal reste continu : pas de clic,
         et l'enveloppe de la note jouée est conservée naturellement ;
      4. l'octave haute est obtenue par redressement double alternance (façon fuzz octave).

    Une hystérésis relative à l'enveloppe évite les faux déclenchements dus au bruit ou aux
    harmoniques. Sous un niveau plancher, les bascules sont gelées et les octaves coupées.
    Fonctionne au mieux sur des notes isolées, comme ses modèles analogiques.
*/
class OctaveDivider final : public MonoEffect
{
public:
    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::live; }

    /** Niveaux linéaires (0 à 1) du son direct, de -1 octave, de -2 octaves et de +1 octave. */
    void setLevels (float dry, float octaveDown, float twoOctavesDown, float octaveUp) noexcept;

    /** Fréquence de coupure du filtre de tonalité appliqué aux octaves (Hz). */
    void setToneFrequency (float hz) noexcept;

private:
    void updateToneFilter() noexcept;

    static constexpr double trackingHz = 350.0;
    static constexpr double octaveUpBandHz = 1200.0;
    static constexpr double dcBlockHz = 30.0;
    static constexpr float hysteresisRatio = 0.1f;
    static constexpr float floorLevel = 0.0005f; // ≈ -66 dBFS

    double sampleRate = 48000.0;

    std::array<Biquad, 2> trackingFilter; // Butterworth ordre 4
    Biquad octaveUpBand, octaveUpDcBlock;
    std::array<Biquad, 2> toneFilter;
    float toneHz = 800.0f;
    float appliedToneHz = 0.0f;

    LinearSmoother dryLevel, downLevel, twoDownLevel, upLevel;
    LinearSmoother octaveGate;

    float envelope = 0.0f;
    float envelopeAttack = 0.0f, envelopeRelease = 0.0f;
    float previous = 0.0f;
    bool armed = false;
    float flipFlop1 = 1.0f, flipFlop2 = 1.0f;
};

} // namespace subdamp::dsp
