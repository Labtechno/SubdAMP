# Bibliothèque DSP de SubdAmp : C++ pur, aucune dépendance à JUCE.
# Tout ce qui est ici doit pouvoir être testé unitairement sans hôte audio.

add_library(subdamp_dsp STATIC
    src/BypassCrossfade.cpp
    src/GainStage.cpp
    src/LinearSmoother.cpp)

target_include_directories(subdamp_dsp PUBLIC include)
target_compile_features(subdamp_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(subdamp_dsp PRIVATE /W4 /utf-8)
else()
    target_compile_options(subdamp_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
