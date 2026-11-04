#pragma once

namespace subdamp::dsp
{

/**
    Lissage linéaire d'une valeur de contrôle (gain, mix, etc.) pour éviter les clics.

    Chaque changement de cible démarre une rampe de durée fixe depuis la valeur courante.
    La dernière valeur de la rampe est exactement la cible : aucune dérive due aux erreurs
    d'arrondi ne s'accumule.

    Sûr en temps réel : aucune allocation, aucun verrou. Toutes les méthodes sauf prepare()
    sont destinées au thread audio.
*/
class LinearSmoother
{
public:
    /** Calcule la longueur de rampe. Annule une éventuelle rampe en cours. */
    void prepare (double sampleRate, double rampSeconds) noexcept;

    /** Saute directement à une valeur, sans rampe. */
    void setCurrentAndTarget (float value) noexcept;

    /** Démarre une rampe vers une nouvelle cible. Ne fait rien si la cible est inchangée. */
    void setTarget (float newTarget) noexcept;

    /** Avance d'un échantillon et renvoie la nouvelle valeur. */
    float getNext() noexcept;

    /** Avance de plusieurs échantillons d'un coup (utile quand un bloc n'a pas besoin des valeurs intermédiaires). */
    void skip (int numSamples) noexcept;

    float getCurrent() const noexcept  { return current; }
    float getTarget() const noexcept   { return target; }
    bool isSmoothing() const noexcept  { return remaining > 0; }

private:
    int rampLength = 1;
    int remaining = 0;
    float current = 0.0f;
    float target = 0.0f;
    float step = 0.0f;
};

} // namespace subdamp::dsp
