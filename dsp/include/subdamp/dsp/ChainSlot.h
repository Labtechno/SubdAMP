#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/MonoEffect.h"
#include "subdamp/dsp/StereoTexture.h"

#include <span>
#include <utility>
#include <vector>

namespace subdamp::dsp
{

/**
    Emplacement d'un module dans la chaîne en série : interrupteur, MIX dry/wet, stéréo.

    Le module reçoit la somme mono (L + R) / 2 ; avant toute texture, L = R et la somme est
    exactement le signal d'origine. Le mélange se fait canal par canal, ce qui garde la stéréo
    de la partie sèche quand un module mono suit une texture. Deux lois :

      - linéaire          : sortie = sec × (1 − MIX) + traité × MIX
        pour un traité en phase avec le sec (ampli, cab) : pas de bosse de niveau ;
      - puissance constante : sortie = sec × cos(MIX·90°) + traité × sin(MIX·90°)
        pour un traité d'une autre hauteur ou d'une autre texture (SUB, HARMONIZER,
        textures) : le niveau ne baisse pas au milieu de la course. En linéaire, chaque module
        dosé à 50 % faisait perdre 3 à 6 dB, et une chaîne de plusieurs modules s'effondrait.

    Interrupteur (fondu de 15 ms, e = 0 à 1) :
      - module mono   : sortie = sec × (1 − e) + e × (sec' × (1 − MIX) + traité × MIX)
        où sec' est le sec éventuellement retardé du retard propre du module (setDryDelaySamples),
        pour qu'un mélange partiel ne filtre pas en peigne. À l'arrêt complet : pas de calcul.
      - texture       : l'entrée se ferme en fondu mais la queue continue (mode « trails »),
        sortie = sec × (1 − e × MIX) + traité × MIX. Une fois arrêtée et silencieuse depuis
        0,5 s, la texture n'est plus calculée. Sa latence propre n'est pas compensée : c'est
        le principe d'une texture (le sec, lui, reste immédiat).

    Contrat temps réel : prepare() peut allouer ; reset(), les réglages et process() jamais.
*/
class ChainSlot
{
public:
    explicit ChainSlot (MonoEffect& effectToHost) noexcept : mono (&effectToHost) {}
    explicit ChainSlot (StereoTexture& textureToHost) noexcept : texture (&textureToHost) {}

    /** Prépare aussi le module hébergé. Peut allouer. */
    void prepare (const ProcessSpec& spec);
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    void setMix (float newMix) noexcept { mix.setTarget (newMix < 0.0f ? 0.0f : (newMix > 1.0f ? 1.0f : newMix)); }

    enum class MixLaw
    {
        linear,
        constantPower
    };

    void setMixLaw (MixLaw newLaw) noexcept { law = newLaw; }

    /** Gains du sec et du traité pour un MIX donné, selon la loi. */
    static std::pair<float, float> mixGains (float mixAmount, MixLaw law) noexcept;

    /**
        Le traité contient déjà le son direct : la sortie devient sec + MIX × traité. Pour le
        FREEZE, dont la nappe s'ajoute au jeu sans jamais l'atténuer, même quand rien n'est figé.
    */
    void setWetAddsToDry (bool shouldAdd) noexcept { wetAddsToDry = shouldAdd; }

    /** Retard appliqué au sec d'un module mono (compensation de sa latence propre). */
    void setDryDelaySamples (int samples) noexcept;

    bool isTexture() const noexcept { return texture != nullptr; }

    /** Vrai quand le module n'est plus calculé du tout (arrêté, queue éteinte). */
    bool isIdle() const noexcept { return idle; }

    /** Traite le bloc stéréo sur place. */
    void process (std::span<float> left, std::span<float> right) noexcept;

private:
    static constexpr float silence = 1.0e-5f; // -100 dBFS

    void processMono (std::span<float> left, std::span<float> right) noexcept;
    void processTexture (std::span<float> left, std::span<float> right) noexcept;

    MonoEffect* mono = nullptr;
    StereoTexture* texture = nullptr;

    LinearSmoother enable, mix;
    bool enabled = false;
    bool idle = true;
    bool wetAddsToDry = false;
    MixLaw law = MixLaw::linear;

    std::vector<float> monoBuffer, fadeBuffer, wetLeft, wetRight;

    // Retard du sec (modules mono)
    std::vector<float> dryRingLeft, dryRingRight;
    std::size_t dryMask = 0, dryWrite = 0;
    int dryDelay = 0;

    int silentSamples = 0;
    int sleepAfter = 24000;
};

} // namespace subdamp::dsp
