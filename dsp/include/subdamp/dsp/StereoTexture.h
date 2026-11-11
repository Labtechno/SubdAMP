#pragma once

#include "subdamp/dsp/LinearSmoother.h"
#include "subdamp/dsp/ProcessSpec.h"

#include <span>
#include <vector>

namespace subdamp::dsp
{

/**
    Module TEXTURE : reçoit le signal mono et produit une nappe stéréo, mélangée en parallèle
    du son principal (qui, lui, n'attend jamais).

    Contrat temps réel : prepare() peut allouer ; reset() et process() jamais.
*/
class StereoTexture
{
public:
    virtual ~StereoTexture() = default;

    virtual void prepare (const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;

    /** Écrit la texture seule (sans le son d'entrée) dans left et right, qui sont écrasés. */
    virtual void process (std::span<const float> input, std::span<float> left, std::span<float> right) noexcept = 0;

    /** Retard propre de la texture, en échantillons (0 si non significatif). */
    virtual int latencySamples() const noexcept { return 0; }
};

/**
    Emplacement d'une texture sur le bus parallèle : niveau, marche / arrêt avec traînes.

    À l'arrêt, l'entrée de la texture se ferme en fondu mais la texture continue de sonner
    jusqu'à extinction naturelle (queue de réverbe, derniers grains), comme une pédale en mode
    « trails ». Une fois silencieuse depuis 0,5 s, elle n'est plus calculée du tout.
*/
class TextureSlot
{
public:
    explicit TextureSlot (StereoTexture& textureToHost) noexcept : texture (textureToHost) {}

    /** Peut allouer. */
    void prepare (const ProcessSpec& spec);
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    void setLevel (float newLevel) noexcept { level.setTarget (newLevel); }

    /** Vrai quand la texture est arrêtée et éteinte : elle ne coûte plus rien. */
    bool isSleeping() const noexcept { return sleeping; }

    /** Ajoute la texture, au niveau réglé, à left et right. */
    void process (std::span<const float> source, std::span<float> left, std::span<float> right) noexcept;

private:
    static constexpr float silence = 1.0e-5f; // -100 dBFS

    StereoTexture& texture;
    LinearSmoother inputGate, level;
    std::vector<float> input, wetLeft, wetRight;

    bool enabled = false;
    bool sleeping = true;
    int silentSamples = 0;
    int sleepAfter = 24000;
};

} // namespace subdamp::dsp
