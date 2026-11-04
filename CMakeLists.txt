cmake_minimum_required(VERSION 3.22)

# Doit être défini avant project() pour être pris en compte.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Version minimale de macOS")

project(SubdAmp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SUBDAMP_BUILD_TESTS "Compiler les tests unitaires" ON)

include(FetchContent)

# JUCE est récupéré automatiquement. Pour utiliser une copie locale :
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

add_subdirectory(dsp)

#==============================================================================
# Plugin : VST3 + application standalone
#==============================================================================
juce_add_plugin(SubdAmp
    PRODUCT_NAME                  "SubdAmp"
    COMPANY_NAME                  "SubdAmp"
    BUNDLE_ID                     com.subdamp.subdamp
    PLUGIN_MANUFACTURER_CODE      Sbdm
    PLUGIN_CODE                   Sdp1
    FORMATS                       Standalone VST3
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE
    VST3_CATEGORIES               Fx Distortion "Pitch Shift"
    # macOS demande l'autorisation micro pour lire l'entrée de la carte son.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT    "SubdAmp a besoin de l'entrée audio pour traiter le signal de ta guitare."
    COPY_PLUGIN_AFTER_BUILD       FALSE)

target_sources(SubdAmp
    PRIVATE
        plugin/Parameters.cpp
        plugin/PluginProcessor.cpp
        plugin/StateVersioning.cpp
        gui/PluginEditor.cpp
        gui/LevelMeter.cpp
        standalone/AudioSettingsPanel.cpp
        standalone/StandaloneApp.cpp
        standalone/StandaloneHost.cpp)

target_include_directories(SubdAmp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Les sources sont en UTF-8 (textes de l'interface avec accents).
target_compile_options(SubdAmp PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(SubdAmp
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Fenêtre standalone maison (voir standalone/) au lieu de celle de JUCE,
        # qui coupe l'entrée audio par défaut.
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        # Pilotes ASIO sous Windows (en-têtes du SDK fournis avec JUCE).
        $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>)

target_link_libraries(SubdAmp
    PRIVATE
        subdamp_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Tests
#==============================================================================
if(SUBDAMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
