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

    Chemin du signal :
      entrée → pré-délai ─┐
                          + → coupe-bas → coupe-haut → diffusion → réseau de retards → sortie
      boucle transposée ──┘                                                  │
            ↑                                                                │
            └─ passe-haut ← transposition(s) ←───────────────────────────────┘

    Réverbe à réseau de retards (FDN) :
      - un passe-haut (COUPE-BAS, 50 Hz par défaut) protège la réverbe du continu et dégage
        le grave ; un passe-bas (COUPE-HAUT) borne les aigus. Les deux sont aussi dans la
        boucle de transposition, puisque la boucle repasse par l'entrée ;
      - 4 passe-tout en série diffusent l'attaque (DIFFUSION : 0 = échos nets, 1 = attaque
        fondue) ;
      - 8 lignes à retard de longueurs premières entre elles (TAILLE : de 0,5 à 2 fois les
        longueurs de base, 30 à 75 ms), mélangées par une matrice de Hadamard (orthogonale :
        aucune énergie créée par le mélange) ;
      - chaque ligne est atténuée selon le temps de décroissance voulu, en deux bandes : sous
        COUPE-HAUT au temps réglé, au-dessus deux fois plus vite (amortissement de Jot). Les
        gains suivent la TAILLE : la durée réglée ne dépend pas de la taille ;
      - deux oscillateurs lents modulent les longueurs (VITESSE, PROFONDEUR) contre la
        sonorité métallique ; une forte profondeur donne un chorus.
    Les sorties gauche et droite combinent les lignes avec des signes différents : la nappe
    est large et décorrélée. LARGEUR la resserre jusqu'au mono (milieu / côtés).

    Boucle de transposition, traitée par petits blocs fixes de 32 échantillons :
      sortie de la réverbe → transposition (vocodeur de phase Signalsmith : pas de repliement,
      pas de hachures sur une matière dense) → passe-haut (ordre 4, au moins 150 Hz) →
      limiteur doux au-delà de la pleine échelle → réinjection, avec l'entrée.
    En mode double, un second transpositeur descend du même intervalle pendant que le premier
    monte ; leurs sorties sont sommées à puissance constante (÷ √2), et la boucle est un peu
    moins réinjectée (voir energyPerGenerationAtMaximumDual).

    Pourquoi ces filtres sont indispensables : la transposition déplace toutes les fréquences
    sauf le continu et l'extrême grave, qui restent en bas à chaque tour. Sans passe-haut, ils
    s'accumulent dans la boucle jusqu'à la saturation (c'était le défaut de la première
    version). Avec, chaque génération finit soit trop aiguë (passe-bas), soit trop grave
    (passe-haut) : la queue s'éteint toujours, même au maximum.

    Le gain de boucle est calibré pour que, au maximum, chaque génération garde environ 80 %
    de l'énergie de la précédente : nappe longue, jamais d'emballement. Aucun réglage ne
    change ce calibrage (la normalisation d'énergie suit la taille et la durée).

    Contrat temps réel : prepare() alloue tout pour les valeurs maximales (taille ×2,
    pré-délai 500 ms, profondeur maximale, deux transpositeurs) ; les réglages et process()
    n'allouent jamais. Tout réglage qui déplacerait le signal (taille, pré-délai, diffusion,
    profondeur, largeur, mode) est lissé ; le pré-délai passe par un fondu enchaîné.
*/
class ShimmerReverb final : public StereoTexture
{
public:
    ShimmerReverb();
    ~ShimmerReverb() override;

    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept override;

    enum class PitchMode
    {
        single, // la boucle monte (ou descend) de l'intervalle
        dual    // la boucle monte ET descend de l'intervalle, à parts égales
    };

    /** Temps de décroissance de 60 dB, en secondes (0,5 à 30). */
    void setDecaySeconds (float seconds) noexcept;
    /** Quantité de son transposé réinjecté (0 = réverbe simple, 1 = maximum). */
    void setShimmer (float amount) noexcept;
    /** Intervalle de transposition de la boucle, en demi-tons (-24 à +24). */
    void setInterval (float semitones) noexcept;
    void setPitchMode (PitchMode mode) noexcept;
    /** COUPE-HAUT : coupure des aigus de l'entrée et de la boucle, et fréquence de l'amortissement à deux bandes. */
    void setToneHz (float hz) noexcept;
    /** COUPE-BAS : passe-haut de l'entrée et de la boucle, de 20 à 1000 Hz. */
    void setLowCutHz (float hz) noexcept;
    /** TAILLE, de 0 à 1 : longueurs des lignes de 0,5 à 2 fois la base (1 au milieu). */
    void setSize (float amount) noexcept;
    /** DIFFUSION, de 0 à 1 : coefficient des passe-tout d'entrée, de 0 à 0,8125 (0,65 d'origine à 0,8). */
    void setDiffusion (float amount) noexcept;
    /** Pré-délai de l'entrée, en millisecondes (0 à 500). */
    void setPreDelayMs (float milliseconds) noexcept;
    /** Vitesse de modulation des lignes, en hertz (0,05 à 5). */
    void setModulationRate (float hz) noexcept;
    /** Profondeur de modulation, de 0 à 1 (jusqu'à 20 échantillons à 48 kHz). */
    void setModulationDepth (float amount) noexcept;
    /** Largeur stéréo, de 0 (mono) à 1 (pleine largeur). */
    void setWidth (float amount) noexcept;

    /** Retard de la boucle (transposition + bloc), en échantillons : délai entre deux générations. */
    int loopDelaySamples() const noexcept;

    /** Au maximum, part d'énergie gardée par chaque génération transposée (calibrage de la boucle). */
    static constexpr float energyPerGenerationAtMaximum = 0.8f;

    /**
        Même calibrage en mode double, plus bas : la moitié de chaque génération redescend, et
        une montée suivie d'une descente revient à la note de départ. La matière ne quitte
        donc plus la bande par le haut, comme en mode simple : seul le gain de boucle la fait
        s'éteindre. À 0,8, la queue durait plus de dix fois la DURÉE ; à 0,5, environ cinq fois.
    */
    static constexpr float energyPerGenerationAtMaximumDual = 0.5f;

    /** Les aigus (au-dessus de COUPE-HAUT) décroissent deux fois plus vite que les graves. */
    static constexpr double highDecayRatio = 0.5;

    static constexpr float maxPreDelayMs = 500.0f;
    static constexpr float maxDiffusionCoefficient = 0.8125f;
    static constexpr double maxModulationSamplesAt48k = 20.0;

    /** Facteur appliqué aux longueurs des lignes pour une TAILLE donnée (0,5 à 2). */
    static double sizeFactorFor (float amount) noexcept;

private:
    static constexpr int numLines = 8;
    static constexpr int numDiffusers = 4;
    static constexpr std::size_t loopBlock = 32;

    struct Line
    {
        std::vector<float> buffer;
        std::size_t mask = 0;
        double baseLength = 1000.0; // à la taille 1
        float decayGain = 0.9f;     // graves (sous COUPE-HAUT)
        float highGain = 0.8f;      // aigus (au-dessus de COUPE-HAUT)
        float lowBand = 0.0f;
    };

    struct Diffuser
    {
        std::vector<float> buffer;
        std::size_t index = 0;
    };

    struct LoopShifter; // moteur de transposition (Signalsmith), caché de l'en-tête public

    /** Gains de décroissance pour la taille courante (appelé au plus tous les 32 échantillons). */
    void updateDecayGains() noexcept;
    void updateLowCut() noexcept;
    void runLoop() noexcept;

    double sampleRate = 48000.0;

    std::array<Line, numLines> lines;
    std::array<Diffuser, numDiffusers> diffusers;
    std::int64_t written = 0;

    Biquad inputHighPass, inputLowPass;

    // Pré-délai (sur l'entrée seule : la boucle n'est pas retardée). Un changement passe par
    // un fondu enchaîné entre l'ancienne et la nouvelle lecture : pas de glissement de hauteur.
    std::vector<float> preDelayBuffer;
    std::size_t preDelayMask = 0;
    int preDelayTarget = 0, preDelayCurrent = 0, preDelayNext = 0; // en échantillons
    int preDelayFadeLength = 1440, preDelayFadeRemaining = 0;

    // Boucle de transposition : un transpositeur vers le haut, un second vers le bas (mode double)
    std::unique_ptr<LoopShifter> upShifter, downShifter;
    std::array<float, loopBlock> loopInput {}, loopOutput {}, downOutput {};
    std::size_t loopPosition = 0;
    std::array<Biquad, 2> loopHighPass;
    float intervalSemitones = 12.0f;
    bool loopRunning = false, downRunning = false;
    LinearSmoother dual; // 0 = simple, 1 = double

    LinearSmoother shimmer;
    float decaySeconds = 6.0f;
    float inputScale = 1.0f; // normalisation d'énergie des longues décroissances
    float toneHz = -1.0f;
    float lowCutHz = 50.0f;
    float appliedLowCutHz = -1.0f;
    float dampingCoefficient = 0.5f;

    LinearSmoother size;      // facteur de longueur des lignes
    double gainsSize = -1.0;  // taille pour laquelle les gains ont été calculés
    LinearSmoother diffusion; // coefficient des passe-tout
    LinearSmoother width;

    // Deux oscillateurs de modulation (rotation, pas de sin par échantillon), en quadrature
    struct Rotor
    {
        double cos = 1.0, sin = 0.0, stepCos = 1.0, stepSin = 0.0;
    };

    std::array<Rotor, 2> rotors;
    float modulationRateHz = 0.4f;
    LinearSmoother modulationDepth; // en échantillons
};

} // namespace subdamp::dsp
