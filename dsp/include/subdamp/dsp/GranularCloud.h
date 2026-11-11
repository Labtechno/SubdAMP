#pragma once

#include "subdamp/dsp/StereoTexture.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace subdamp::dsp
{

/**
    GRANULAR : découpe les 8 dernières secondes jouées en grains et les rejoue transposés,
    inversés, dispersés dans l'espace. Deux modes :

      - nuage : grains déclenchés en continu (densité réglable), pris un peu partout dans le
        passé récent selon la DISPERSION, qui fait aussi varier hauteur et panoramique ;
      - stutter : découpage rythmique calé sur le tempo. À chaque division (1/16...), la
        dernière tranche jouée est capturée puis répétée N fois, façon « beat repeat » IDM.
        Quand le DAW est en lecture, les découpes tombent pile sur sa grille.

    La transposition des grains est quantifiée au demi-ton dans la dispersion : le nuage
    reste musical.
*/
class GranularCloud final : public StereoTexture
{
public:
    enum class Mode
    {
        cloud = 0,
        stutter = 1
    };

    void prepare (const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept override;

    void setMode (Mode newMode) noexcept { mode = newMode; }
    void setGrainMilliseconds (float ms) noexcept { grainMs = ms; }
    void setDensity (float grainsPerSecond) noexcept { density = grainsPerSecond; }
    void setPitchSemitones (float semitones) noexcept { pitch = semitones; }
    /** 0 à 1 : dispersion de position, de hauteur et d'espace. */
    void setSpread (float amount) noexcept { spread = amount; }
    /** Probabilité qu'un grain soit lu à l'envers (0 à 1). */
    void setReverseProbability (float probability) noexcept { reverseProbability = probability; }

    /** Mode stutter : durée d'une découpe en temps (0,25 = double croche), et nombre de répétitions. */
    void setDivisionBeats (double beats) noexcept { divisionBeats = beats; }
    void setRepeats (int count) noexcept { repeats = count; }

    /** Tempo courant, et position du DAW en temps au début du bloc s'il est en lecture. */
    void setTempo (double beatsPerMinute) noexcept { tempo = beatsPerMinute; }
    void setTransportPosition (std::optional<double> beatsAtBlockStart) noexcept { transport = beatsAtBlockStart; }

    // Pour les tests
    int grainsStarted() const noexcept { return started; }
    int slicesCaptured() const noexcept { return captured; }
    std::int64_t lastTriggerSample() const noexcept { return lastTrigger; }

private:
    struct Grain
    {
        double position = 0.0; // lecture dans l'historique (index absolu)
        double step = 1.0;     // vitesse de lecture, négative pour un grain inversé
        int length = 0;
        int age = 0;
        int fade = 0;          // 0 : fenêtre de Hann ; sinon fondus linéaires (stutter)
        float gainLeft = 0.0f, gainRight = 0.0f;
        bool active = false;
    };

    void spawnCloudGrain() noexcept;
    void triggerStutter() noexcept;
    Grain* freeGrain() noexcept;
    float random() noexcept; // 0 à 1
    float window (const Grain& grain) const noexcept;
    void setPan (Grain& grain, float pan) const noexcept;

    static constexpr int maxGrains = 64;
    static constexpr int hannSize = 1024;

    double sampleRate = 48000.0;
    std::vector<float> history;
    std::size_t historyMask = 0;
    std::int64_t written = 0;

    std::array<Grain, maxGrains> grains {};
    std::array<float, hannSize + 1> hann {};
    std::uint32_t randomState = 0x2545F491u;

    Mode mode = Mode::cloud;
    float grainMs = 120.0f, density = 12.0f, pitch = 0.0f, spread = 0.3f, reverseProbability = 0.25f;
    double divisionBeats = 0.25;
    int repeats = 4;
    double tempo = 120.0;
    std::optional<double> transport;

    // Ordonnancement
    double samplesToNextGrain = 0.0;
    double freeRunPhase = 0.0;
    std::int64_t lastSlot = -1;
    std::int64_t sliceStart = 0;
    int sliceLength = 0;
    int repeatsLeft = 0;

    int started = 0, captured = 0;
    std::int64_t lastTrigger = -1;
};

} // namespace subdamp::dsp
