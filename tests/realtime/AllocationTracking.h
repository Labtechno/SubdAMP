#pragma once

#include <atomic>

/**
    Compte les allocations mémoire faites pendant une portée donnée, pour vérifier que le
    code audio n'alloue jamais. operator new / delete sont remplacés sur toutes les plateformes ;
    sous Linux (glibc), malloc / free le sont aussi, car Eigen (utilisé par NAM) alloue avec malloc.

    Seules les allocations du thread qui compte sont prises en compte : les threads d'arrière-plan
    (chargement d'une IR par la convolution, d'une capture...) ont le droit d'allouer.
*/
namespace subdamp::test
{

extern thread_local bool trackAllocationsOnThisThread;
extern std::atomic<int> allocationCount;

/** Compte les allocations du thread courant entre sa construction et sa destruction. */
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter()
    {
        allocationCount = 0;
        trackAllocationsOnThisThread = true;
    }

    ~ScopedAllocationCounter() { trackAllocationsOnThisThread = false; }

    int count() const { return allocationCount.load(); }
};

} // namespace subdamp::test
