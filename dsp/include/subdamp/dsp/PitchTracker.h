#pragma once

#include "subdamp/dsp/Biquad.h"

#include <array>
#include <cstdint>
#include <vector>

namespace subdamp::dsp
{

/**
    Détecteur de hauteur monophonique (algorithme YIN) réglé pour la guitare 6 cordes :
    de 60 Hz (drop C et un peu en dessous) à 1400 Hz (24e case de la corde de Mi aigu).

    Pour tenir le temps réel à faible coût :
      1. l'analyse YIN tourne sur un signal décimé vers ~12 kHz, toutes les 4 ms ;
      2. la période trouvée est ensuite affinée à pleine résolution, dans une plage étroite
         autour de l'estimation (précision de l'ordre du centième de demi-ton).

    Le voisement (note tenue ou non) a une hystérésis : la détection ne bascule pas sans
    arrêt sur une note qui s'éteint ou pendant un bend. Juste après une note perdue (attaque de
    la note suivante, legato), le seuil reste celui du maintien : le suivi raccroche vite.

    Contre les erreurs d'octave (attaques bruitées, notes qui se chevauchent) :
      - si l'estimation est un multiple (×2, ×3, ×4) de la période suivie et que cette
        dernière colle encore au signal, on garde la période courte (la note jouée) ;
      - un changement de note (plus de 4 %) n'est adopté que s'il se confirme sur deux
        analyses consécutives (8 ms) ; en attendant, la note en cours est conservée.
*/
class PitchTracker
{
public:
    /** Peut allouer. */
    void prepare (double sampleRate, double minHz = 60.0, double maxHz = 1400.0);
    void reset() noexcept;

    /** Ajoute un échantillon ; l'estimation est mise à jour toutes les 4 ms. */
    void push (float sample) noexcept;

    bool isVoiced() const noexcept { return voiced; }

    /** Période en échantillons (pleine résolution), valide si isVoiced(). */
    float getPeriod() const noexcept { return period; }

    /** Indique qu'une nouvelle estimation vient d'être calculée (remis à faux à la lecture). */
    bool takeNewEstimate() noexcept;

private:
    void analyse() noexcept;
    float refineAtFullRate (float coarsePeriod) noexcept;
    int localMinimumNear (int tau) const noexcept;

    static constexpr float enterThreshold = 0.2f;   // CMND sous laquelle une note devient détectée
    static constexpr float stayThreshold = 0.35f;   // ... et reste détectée
    static constexpr float firstDipThreshold = 0.15f;
    static constexpr float silenceRms = 0.0003f;    // ≈ -70 dBFS : le jeu doux reste suivi
    static constexpr double reacquireSeconds = 0.25; // après une note perdue, la suivante est acceptée au seuil « stay »

    double sampleRate = 48000.0;
    int decimation = 1;
    std::array<Biquad, 2> antiAlias;
    int decimationCounter = 0;

    // Signal décimé
    std::vector<float> decimated;
    std::size_t decimatedMask = 0;
    std::int64_t decimatedWritten = 0;
    int tauMin = 2, tauMax = 200, window = 200, hop = 48, hopCounter = 0;
    std::vector<float> frame, difference, normalised;

    // Signal pleine résolution (affinage)
    std::vector<float> full, fullFrame;
    std::size_t fullMask = 0;
    std::int64_t fullWritten = 0;
    int fullTauMax = 800;

    bool voiced = false;
    bool hasNewEstimate = false;
    float period = 0.0f;

    // Analyses restantes pendant lesquelles une nouvelle note se raccroche au seuil « stay » :
    // l'attaque d'une note qui suit est bruitée, elle ne doit pas couper le suivi longtemps.
    int reacquireFrames = 0, reacquireFramesAfterLoss = 60;

    // Changement de note en attente de confirmation
    float pendingPeriod = 0.0f;   // pleine résolution ; 0 : rien en attente
    float lastGoodPeriod = 0.0f;  // dernière période adoptée (même après une perte passagère)
};

} // namespace subdamp::dsp
