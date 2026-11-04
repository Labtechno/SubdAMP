#pragma once

#include <atomic>
#include <memory>

namespace subdamp::dsp
{

/**
    Transmet un objet lourd (modèle d'ampli...) préparé hors du thread audio au thread audio,
    sans verrou ni allocation ni libération côté audio.

    Trois emplacements atomiques :
      - pending : déposé par le thread de chargement, en attente de prise en compte ;
      - current : détenu par le thread audio (pointeur simple, accès exclusif) ;
      - retired : l'ancien objet, rendu par le thread audio, détruit plus tard par collect().

    Le thread audio ne prend un nouvel objet que si l'emplacement « retired » est libre :
    il n'a ainsi jamais à libérer de mémoire lui-même.
*/
template <typename T>
class RealtimeHandoff
{
public:
    RealtimeHandoff() = default;
    RealtimeHandoff (const RealtimeHandoff&) = delete;
    RealtimeHandoff& operator= (const RealtimeHandoff&) = delete;

    ~RealtimeHandoff()
    {
        delete pending.load();
        delete retired.load();
        delete current;
    }

    /** Hors thread audio : dépose un nouvel objet (remplace un éventuel objet encore en attente). */
    void publish (std::unique_ptr<T> next)
    {
        collect();
        delete pending.exchange (next.release(), std::memory_order_acq_rel);
    }

    /** Hors thread audio : détruit l'objet que le thread audio a rendu. */
    void collect()
    {
        delete retired.exchange (nullptr, std::memory_order_acq_rel);
    }

    /** Thread audio : prend en compte un objet en attente. Renvoie l'objet courant (peut être nul). */
    T* acquire() noexcept
    {
        if (retired.load (std::memory_order_acquire) == nullptr)
        {
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                retired.store (current, std::memory_order_release);
                current = next;
                ++generation;
            }
        }

        return current;
    }

    /** Objet courant, sans prise en compte d'un objet en attente (thread audio, ou audio arrêté). */
    T* getCurrent() const noexcept { return current; }

    /**
        Objet en attente, pour le préparer à nouveau quand le thread audio est arrêté
        (changement de fréquence d'échantillonnage). Ne jamais l'utiliser pendant que l'audio tourne.
    */
    T* getPendingWhileAudioStopped() const noexcept { return pending.load (std::memory_order_acquire); }

    /** S'incrémente à chaque changement d'objet courant (pour déclencher un fondu, par exemple). */
    unsigned getGeneration() const noexcept { return generation; }

private:
    std::atomic<T*> pending { nullptr };
    std::atomic<T*> retired { nullptr };
    T* current = nullptr;
    unsigned generation = 0;
};

} // namespace subdamp::dsp
