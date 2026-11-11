#pragma once

#include "subdamp/dsp/StereoTexture.h"

#include <memory>

namespace subdamp::dsp
{

/**
    POLY : transposition polyphonique de haute qualité (accords compris), deux voix placées
    dans l'espace stéréo. Catégorie TEXTURE (~150 ms) : à mélanger au son direct, pas à jouer
    en remplacement.

    Moteur : Signalsmith Stretch (vocodeur de phase, licence MIT). Les formants suivent la
    transposition (son d'octaver classique) ; le réglage FORMANTS les déplace à part. Le calcul
    est étalé sur tous les blocs (splitComputation) pour éviter les pics de charge avec de
    petits buffers.
*/
class PolyShifter final : public StereoTexture
{
public:
    static constexpr int numVoices = 2;

    PolyShifter();
    ~PolyShifter() override;

    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept override;
    int latencySamples() const noexcept override;

    void setInterval (int voice, float semitones) noexcept;
    void setVoiceLevel (int voice, float level) noexcept;
    /** Déplacement des formants par rapport à la transposition (0 = ils suivent la hauteur). */
    void setFormantSemitones (float semitones) noexcept;
    /** Écart stéréo entre les deux voix (0 = centre, 1 = extrêmes gauche et droite). */
    void setWidth (float amount) noexcept { width = amount; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    float width = 0.6f;
};

} // namespace subdamp::dsp
