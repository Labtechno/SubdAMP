FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

# Tests du DSP pur : aucune dépendance à JUCE.
add_executable(SubdAmpDspTests
    dsp/BypassCrossfadeTests.cpp
    dsp/DecibelsTests.cpp
    dsp/GainStageTests.cpp
    dsp/LinearSmootherTests.cpp)

target_link_libraries(SubdAmpDspTests PRIVATE subdamp_dsp Catch2::Catch2WithMain)
target_compile_options(SubdAmpDspTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_test(NAME SubdAmpDspTests COMMAND SubdAmpDspTests)

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
