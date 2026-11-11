# Bibliothèque DSP de SubdAmp : C++ pur, aucune dépendance à JUCE (seulement NAM pour les captures d'amplis).
# Tout ce qui est ici doit pouvoir être testé unitairement sans hôte audio.

add_library(subdamp_dsp STATIC
    src/AmpModel.cpp
    src/AmpSection.cpp
    src/Biquad.cpp
    src/BypassCrossfade.cpp
    src/DelayLineShifter.cpp
    src/FractionalResampler.cpp
    src/FrequencyShifter.cpp
    src/GainStage.cpp
    src/Harmonizer.cpp
    src/LinearSmoother.cpp
    src/NoiseGate.cpp
    src/OctaveDivider.cpp
    src/OnsetDetector.cpp
    src/PitchTracker.cpp
    src/RateBridge.cpp
    src/ScaleHarmony.cpp
    src/SwitchableEffect.cpp
    src/ToneStack.cpp
    src/Whammy.cpp)

target_include_directories(subdamp_dsp PUBLIC include)
target_compile_features(subdamp_dsp PUBLIC cxx_std_20)
set_target_properties(subdamp_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Moteur NAM : utilisé seulement dans AmpModel.cpp, absent des en-têtes publics.
target_link_libraries(subdamp_dsp PRIVATE nam_core)

if(MSVC)
    target_compile_options(subdamp_dsp PRIVATE /W4 /utf-8)
else()
    target_compile_options(subdamp_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()
