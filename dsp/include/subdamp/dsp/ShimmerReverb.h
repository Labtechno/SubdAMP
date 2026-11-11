#pragma once

#include "subdamp/dsp/Biquad.h"
#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/StereoTexture.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace subdamp::dsp
{

/**
    SHIMMER : réverbe dont la queue est transposée et réinjectée en boucle. Chaque passage
    dans la boucle monte d'un intervalle (octave par défaut) : la nappe « s'élève » en continu.

    Réverbe à réseau de retards (FDN) :
      - un passe-haut à 50 Hz protège la réverbe du continu (offset de la carte son) ;
      - 4 passe-tout en série diffusent l'attaque ;
      - 8 lignes à retard de longueurs premières entre elles, mélangées par une matrice de
        Hadamard (orthogonale : aucune énergie créée par le mélange) ;
      - chaque ligne est atténuée selon le temps de décroissance voulu, en deux bandes : sous
        TONE au temps réglé, au-dessus deux fois plus vite (amortissement de Jot) ;
      - une légère modulation des longueurs évite la sonorité métallique.
    Les sorties gauche et droite combinent les lignes avec des signes différents : la nappe
    est large et décorrélée.

    Boucle de transposition, traitée par petits blocs fixes de 32 échantillons :
      sortie de la réverbe → transposition (vocodeur de phase Signalsmith : pas de repliement,
      pas de hachures sur une matière dense) → passe-haut à 150 Hz (ordre 4) → limiteur doux
      au-delà de la pleine échelle → réinjection, avec l'entrée, à travers le passe-bas TONE.

    Pourquoi ces filtres sont indispensables : la transposition déplace toutes les fréquences
    sauf le continu et l'extrême grave, qui restent en bas à chaque tour. Sans passe-haut, ils
    s'accumulent dans la boucle jusqu'à la saturation (c'était le défaut de la première
    version). Avec, chaque génération finit soit trop aiguë (passe-bas), soit trop grave
    (passe-haut) : la queue s'éteint toujours, même au maximum.

    Le gain de boucle est calibré pour que, au maximum, chaque génération garde environ 80 %
    de l'énergie de la précédente : nappe longue, jamais d'emballement.
*/
class ShimmerReverb final : public StereoTexture
{
public:
    ShimmerReverb();
    ~ShimmerReverb() override;

    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept override;

    /** Temps de décroissance de 60 dB, en secondes (0,5 à 30). */
    void setDecaySeconds (float seconds) noexcept;
    /** Quantité de son transposé réinjecté (0 = réverbe simple, 1 = maximum). */
    void setShimmer (float amount) noexcept;
    /** Intervalle de transposition de la boucle, en demi-tons (-24 à +24). */
    void setInterval (float semitones) noexcept;
    /** Coupure des aigus de l'entrée et de la boucle, et fréquence de l'amortissement à deux bandes. */
    void setToneHz (float hz) noexcept;

    /** Retard de la boucle (transposition + bloc), en échantillons : délai entre deux générations. */
    int loopDelaySamples() const noexcept;

    /** Au maximum, part d'énergie gardée par chaque génération transposée (calibrage de la boucle). */
    static constexpr float energyPerGenerationAtMaximum = 0.8f;

    /** Les aigus (au-dessus de TONE) décroissent deux fois plus vite que les graves. */
    static constexpr double highDecayRatio = 0.5;

private:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;
    static constexpr std::size_t loopBlock = 32;

    struct Line
    {
        std::vector<float> buffer;
        std::size_t mask = 0;
        double length = 1000.0;
        float decayGain = 0.9f;  // graves (sous TONE)
        float highGain = 0.8f;   // aigus (au-dessus de TONE)
        float lowBand = 0.0f;
    };

    struct Diffuser
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    struct LoopShifter; // moteur de transposition (Signalsmith), caché de l'en-tête public

    void updateDecayGains() noexcept;
    void runLoop() noexcept;

    double sampleRate = 48000.0;

    std::array<Line, numLines> lines;
    std::array<Diffuser, numDiffusers> diffusers;
    std::int64_t written = 0;

    Biquad inputHighPass, inputLowPass;

    // Boucle de transposition
    std::unique_ptr<LoopShifter> loopShifter;
    std::array<float, loopBlock> loopInput {}, loopOutput {};
    std::size_t loopPosition = 0;
    std::array<Biquad, 2> loopHighPass;
    float intervalSemitones = 12.0f;
    bool loopRunning = false;

    LinearSmoother shimmer;
    float decaySeconds = 6.0f;
    float inputScale = 1.0f; // normalisation d'énergie des longues décroissances
    float toneHz = -1.0f;
    float dampingCoefficient = 0.5f;

    // Oscillateur de modulation (rotation, pas de sin par échantillon)
    double modulationCos = 1.0, modulationSin = 0.0, stepCos = 1.0, stepSin = 0.0;
    double modulationDepth = 6.0;
};

} // namespace subdamp::dsp
