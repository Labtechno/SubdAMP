FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

#==============================================================================
# Tests du DSP pur : aucune dépendance à JUCE.
add_executable(SubdAmpDspTests
    dsp/AmpModelTests.cpp
    dsp/BypassCrossfadeTests.cpp
    dsp/DecibelsTests.cpp
    dsp/FilterTests.cpp
    dsp/GainStageTests.cpp
    dsp/HarmonizerTests.cpp
    dsp/LinearSmootherTests.cpp
    dsp/ModuleTests.cpp
    dsp/ResamplingTests.cpp
    dsp/WhammyTests.cpp)

target_link_libraries(SubdAmpDspTests PRIVATE subdamp_dsp Catch2::Catch2WithMain)
target_compile_definitions(SubdAmpDspTests PRIVATE SUBDAMP_NAM_EXAMPLE_MODELS="${SUBDAMP_NAM_EXAMPLE_MODELS}")
target_compile_options(SubdAmpDspTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME SubdAmpDspTests COMMAND SubdAmpDspTests)

#==============================================================================
# Sûreté temps réel : aucune allocation pendant le traitement (exécutable séparé,
# car il remplace les fonctions d'allocation du programme).
add_executable(SubdAmpRealtimeTests
    realtime/AllocationTracking.cpp
    realtime/RealtimeSafetyTests.cpp)

target_link_libraries(SubdAmpRealtimeTests PRIVATE subdamp_dsp Catch2::Catch2WithMain)
target_compile_definitions(SubdAmpRealtimeTests PRIVATE SUBDAMP_NAM_EXAMPLE_MODELS="${SUBDAMP_NAM_EXAMPLE_MODELS}")
target_compile_options(SubdAmpRealtimeTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME SubdAmpRealtimeTests COMMAND SubdAmpRealtimeTests)

#==============================================================================
# Processeur complet (chaîne, fichiers, presets), lié au code partagé du plugin.
add_executable(SubdAmpProcessorTests
    integration/ProcessorTests.cpp
    realtime/AllocationTracking.cpp)

# Mêmes en-têtes et définitions que le code du plugin (modules JUCE, macros JucePlugin_*).
target_include_directories(SubdAmpProcessorTests PRIVATE ${PROJECT_SOURCE_DIR} $<TARGET_PROPERTY:SubdAmp,INCLUDE_DIRECTORIES>)
target_compile_definitions(SubdAmpProcessorTests PRIVATE $<TARGET_PROPERTY:SubdAmp,COMPILE_DEFINITIONS>)
target_link_libraries(SubdAmpProcessorTests PRIVATE SubdAmp Catch2::Catch2WithMain)
target_compile_definitions(SubdAmpProcessorTests PRIVATE SUBDAMP_NAM_EXAMPLE_MODELS="${SUBDAMP_NAM_EXAMPLE_MODELS}")
target_compile_options(SubdAmpProcessorTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME SubdAmpProcessorTests COMMAND SubdAmpProcessorTests)

#==============================================================================
# Mesure de charge CPU des captures NAM (pas un test : à lancer à la main).
add_executable(SubdAmpBenchmark benchmark/Benchmark.cpp)
target_link_libraries(SubdAmpBenchmark PRIVATE subdamp_dsp)
target_compile_definitions(SubdAmpBenchmark PRIVATE SUBDAMP_NAM_EXAMPLE_MODELS="${SUBDAMP_NAM_EXAMPLE_MODELS}")
target_compile_options(SubdAmpBenchmark PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

#==============================================================================
# Tests de la migration de l'état sauvegardé (presets) : seul juce_data_structures est requis.
juce_add_console_app(SubdAmpStateTests PRODUCT_NAME "SubdAmpStateTests")

target_sources(SubdAmpStateTests
    PRIVATE
        state/StateVersioningTests.cpp
        ${PROJECT_SOURCE_DIR}/plugin/StateVersioning.cpp)

target_include_directories(SubdAmpStateTests PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_options(SubdAmpStateTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(SubdAmpStateTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(SubdAmpStateTests
    PRIVATE
        juce::juce_data_structures
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags)

add_test(NAME SubdAmpStateTests COMMAND SubdAmpStateTests)
