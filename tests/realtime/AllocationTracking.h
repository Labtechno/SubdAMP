#pragma once

#include <atomic>

/**
    Compte les allocations mémoire faites pendant une portée donnée, pour vérifier que le
    code audio n'alloue jamais. operator new / delete sont remplacés sur toutes les plateformes ;
    sous Linux (glibc), malloc / free le sont aussi, car Eigen (utilisé par NAM) alloue avec malloc.
*/
namespace subdamp::test
{

extern std::atomic<bool> trackAllocations;
extern std::atomic<int> allocationCount;

/** Compte les allocations entre sa construction et sa destruction. */
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter()
    {
        allocationCount = 0;
        trackAllocations = true;
    }

    ~ScopedAllocationCounter() { trackAllocations = false; }

    int count() const { return allocationCount.load(); }
};

} // namespace subdamp::test
