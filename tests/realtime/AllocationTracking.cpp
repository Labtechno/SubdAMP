#include "AllocationTracking.h"

#include <cstdlib>
#include <new>

namespace subdamp::test
{
thread_local bool trackAllocationsOnThisThread = false;
std::atomic<int> allocationCount { 0 };
}

namespace
{
    void noteAllocation() noexcept
    {
        if (subdamp::test::trackAllocationsOnThisThread)
            subdamp::test::allocationCount.fetch_add (1, std::memory_order_relaxed);
    }
}

//==============================================================================
#if defined (__GLIBC__)
// glibc expose ses implémentations sous ces noms : on peut donc intercepter malloc sans dlsym.
extern "C"
{
    void* __libc_malloc (std::size_t);
    void* __libc_calloc (std::size_t, std::size_t);
    void* __libc_realloc (void*, std::size_t);
    void __libc_free (void*);

    void* malloc (std::size_t size)
    {
        noteAllocation();
        return __libc_malloc (size);
    }

    void* calloc (std::size_t count, std::size_t size)
    {
        noteAllocation();
        return __libc_calloc (count, size);
    }

    void* realloc (void* pointer, std::size_t size)
    {
        noteAllocation();
        return __libc_realloc (pointer, size);
    }

    void free (void* pointer)
    {
        if (pointer != nullptr)
            noteAllocation(); // une libération dans le thread audio est aussi interdite
        __libc_free (pointer);
    }
}

void* operator new (std::size_t size)
{
    if (auto* pointer = std::malloc (size == 0 ? 1 : size))
        return pointer;

    throw std::bad_alloc();
}

#else

void* operator new (std::size_t size)
{
    noteAllocation();

    if (auto* pointer = std::malloc (size == 0 ? 1 : size))
        return pointer;

    throw std::bad_alloc();
}

#endif

void* operator new[] (std::size_t size)
{
    return operator new (size);
}

void operator delete (void* pointer) noexcept
{
   #if ! defined (__GLIBC__)
    if (pointer != nullptr)
        noteAllocation();
   #endif

    std::free (pointer);
}

void operator delete[] (void* pointer) noexcept
{
    operator delete (pointer);
}

void operator delete (void* pointer, std::size_t) noexcept
{
    operator delete (pointer);
}

void operator delete[] (void* pointer, std::size_t) noexcept
{
    operator delete (pointer);
}
