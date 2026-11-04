# Dépendances tierces compilées par SubdAmp lui-même (et non via leur propre CMake,
# qui construit des outils et des tests dont on n'a pas besoin).

#==============================================================================
# NeuralAmpModelerCore — moteur des captures d'amplis .nam (licence MIT)
# Ses sous-modules (Eigen sur GitLab, AudioDSPTools) ne sont pas récupérés :
# Eigen est pris ci-dessous, au même commit, et AudioDSPTools n'est pas utilisé.
#==============================================================================
FetchContent_Declare(nam_core
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  not-a-cmake-project)

# Eigen — algèbre linéaire utilisée par NAM (licence MPL2, en-têtes seulement).
# Tag 5.0.1 = commit exact épinglé par NeuralAmpModelerCore v0.6.0.
FetchContent_Declare(eigen
    GIT_REPOSITORY https://github.com/eigen-mirror/eigen.git
    GIT_TAG        5.0.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  not-a-cmake-project)

FetchContent_MakeAvailable(nam_core eigen)

# Captures d'exemple fournies avec NeuralAmpModelerCore, utilisées par les tests.
set(SUBDAMP_NAM_EXAMPLE_MODELS "${nam_core_SOURCE_DIR}/example_models" CACHE INTERNAL "")

file(GLOB NAM_SOURCES CONFIGURE_DEPENDS
    "${nam_core_SOURCE_DIR}/NAM/*.cpp"
    "${nam_core_SOURCE_DIR}/NAM/*/*.cpp")

add_library(nam_core STATIC ${NAM_SOURCES})

target_include_directories(nam_core SYSTEM
    PUBLIC
        "${nam_core_SOURCE_DIR}"
        "${nam_core_SOURCE_DIR}/Dependencies/nlohmann"
        "${eigen_SOURCE_DIR}")

target_compile_features(nam_core PUBLIC cxx_std_20)

target_compile_definitions(nam_core
    PUBLIC
        # Calcul en float, comme le reste de la chaîne audio.
        NAM_SAMPLE_FLOAT
        # Chemin optimisé pour les modèles WaveNet « A2 ».
        NAM_ENABLE_A2_FAST
        $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN>)

set_target_properties(nam_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Code tiers : on ne corrige pas ses avertissements.
if(MSVC)
    target_compile_options(nam_core PRIVATE /W0 /utf-8 /bigobj)
else()
    target_compile_options(nam_core PRIVATE -w)
endif()

# Instructions AVX2/FMA sur x86 : environ 30 % de CPU en moins pour les captures WaveNet.
# Tous les processeurs x86 depuis 2013 (Intel Haswell, AMD Zen) les ont ; à désactiver pour
# une machine plus ancienne. PUBLIC : tout fichier incluant les en-têtes NAM (donc Eigen)
# doit être compilé avec les mêmes options, sinon l'alignement mémoire d'Eigen diffère.
option(SUBDAMP_NAM_AVX2 "Optimiser le moteur NAM pour AVX2/FMA sur x86-64" ON)

if(SUBDAMP_NAM_AVX2)
    if(APPLE)
        # Binaire universel : seule la tranche Intel est concernée.
        target_compile_options(nam_core PUBLIC -Xarch_x86_64 -mavx2 -Xarch_x86_64 -mfma)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$")
        if(MSVC)
            target_compile_options(nam_core PUBLIC /arch:AVX2)
        else()
            target_compile_options(nam_core PUBLIC -mavx2 -mfma)
        endif()
    endif()
endif()

# Optimisation même en Debug : un réseau de neurones non optimisé ne tient pas le temps réel.
if(NOT MSVC)
    target_compile_options(nam_core PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
