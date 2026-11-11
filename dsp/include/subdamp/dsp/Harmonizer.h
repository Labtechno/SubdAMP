#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/MonoEffect.h"
#include "subdamp/dsp/PitchTracker.h"
#include "subdamp/dsp/ScaleHarmony.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

namespace subdamp::dsp
{

/**
    HARMONIZER : deux voix d'harmonie diatoniques (tierce, quinte... dans la tonalité et la
    gamme choisies), monophonique, catégorie JOUABLE.

    Synthèse TD-PSOLA :
      1. le PitchTracker donne la période P de la note jouée ;
      2. des « marques » sont posées toutes les P sur le signal, recalées par corrélation
         pour suivre la même phase de l'onde d'une période à l'autre ;
      3. chaque voix émet des grains de deux périodes (fenêtre de Hann) prélevés autour de
         la dernière marque, espacés de P / rapport : la hauteur change, pas le timbre.

    Les grains sont lus à vitesse 1 : l'enveloppe spectrale (les formants) est conservée.
    Le réglage FORMANTS lit les grains plus vite ou plus lentement pour la déplacer.

    Le son direct n'est jamais retardé. Les voix d'harmonie arrivent avec 1,5 à 2,5 périodes
    de retard : ~5 ms sur le Mi aigu, ~25 ms sur un Do grave (drop C).
*/
class Harmonizer final : public MonoEffect
{
public:
    static constexpr int numVoices = 2;

    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<float> block) noexcept override;
    LatencyTier latencyTier() const noexcept override { return LatencyTier::playable; }

    /** Tonique : 0 = Do (C), 1 = Do♯... 11 = Si. */
    void setKey (int pitchClass) noexcept;
    void setScale (Scale newScale) noexcept;

    /** Intervalle d'une voix en degrés de la gamme (2 = tierce, 4 = quinte...), ou aucune voix. */
    void setVoiceInterval (int voice, std::optional<int> steps) noexcept;
    void setVoiceLevel (int voice, float level) noexcept;
    void setDirectLevel (float level) noexcept;
    void setFormantSemitones (float semitones) noexcept;

    /** Retard moyen des voix d'harmonie, en échantillons (0 tant qu'aucune note n'est suivie). */
    int getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }

    // Pour les tests
    bool isTracking() const noexcept { return tracker.isVoiced(); }
    int getVoiceShiftSemitones (int voice) const noexcept;

private:
    static constexpr int maxGrains = 24;
    static constexpr int hannSize = 1024;

    struct Grain
    {
        double source = 0.0;  // position de lecture (index absolu dans l'historique)
        double step = 1.0;    // vitesse de lecture (1 = formants conservés)
        float phase = 0.0f;   // position dans la fenêtre de Hann, de 0 à 1
        float phaseStep = 0.0f;
        bool active = false;
    };

    struct Voice
    {
        std::array<Grain, maxGrains> grains {};
        std::optional<int> steps;
        int shiftSemitones = 0;
        double ratio = 1.0;
        double nextGrainTime = -1.0;
        LinearSmoother level;
        float envelope = 0.0f;
        float energy = 0.0f;       // puissance moyenne de la voix
        float compensation = 1.0f; // gain d'égalisation de niveau
    };

    void onNewEstimate() noexcept;
    void updateShifts() noexcept;
    void placeMarks (std::int64_t now) noexcept;
    void emitGrain (Voice& voice, std::int64_t now) noexcept;
    float renderVoice (Voice& voice, std::int64_t now) noexcept;
    float hann (float phase) const noexcept;

    double sampleRate = 48000.0;
    PitchTracker tracker;

    std::vector<float> history;
    std::size_t historyMask = 0;
    std::int64_t written = 0;

    // Marques de période
    bool haveMark = false;
    std::int64_t lastMark = 0;
    float lastMarkPeriod = 0.0f;
    double nextMarkExact = -1.0;

    // Harmonie
    int key = 4;
    Scale scale = Scale::naturalMinor;
    int currentNote = -1; // note MIDI détectée (avec hystérésis)
    float formantFactor = 1.0f;

    std::array<Voice, numVoices> voices;
    LinearSmoother direct;
    float envelopeAttack = 0.0f, envelopeRelease = 0.0f;
    float levelFollow = 0.0f;
    float inputEnergy = 0.0f;

    std::array<float, hannSize + 1> hannTable {};

    double latencyAverage = 0.0;
    std::atomic<int> latency { 0 };
};

} // namespace subdamp::dsp
