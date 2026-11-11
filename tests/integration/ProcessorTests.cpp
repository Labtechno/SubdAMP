// Tests du processeur complet (chaîne, fichiers, état sauvegardé), sans hôte audio.

#include "../dsp/TestSignals.h"
#include "../realtime/AllocationTracking.h"

#include "gui/PedalBoard.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "subdamp/dsp/PitchTracker.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <random>

using namespace subdamp;

namespace
{
    const juce::File exampleModels { juce::String::fromUTF8 (SUBDAMP_NAM_EXAMPLE_MODELS) };

    /** Environnement JUCE minimal (gestionnaire de messages) pour chaque test. */
    struct JuceEnvironment
    {
        juce::ScopedJuceInitialiser_GUI initialiser;
    };

    std::unique_ptr<SubdAmpProcessor> createProcessor()
    {
        return std::unique_ptr<SubdAmpProcessor> (dynamic_cast<SubdAmpProcessor*> (createPluginFilter()));
    }

    void setParameter (SubdAmpProcessor& processor, const char* id, float value)
    {
        auto* parameter = processor.getParameters().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    float parameterValue (SubdAmpProcessor& processor, const char* id)
    {
        return processor.getParameters().getRawParameterValue (id)->load();
    }

    /** IR de test : bruit à décroissance exponentielle de 80 ms, écrite en WAV 24 bits. */
    juce::File writeTestImpulseResponse (const juce::File& folder)
    {
        const auto file = folder.getChildFile (juce::String::fromUTF8 ("IR de test é.wav"));
        file.deleteFile();

        juce::AudioBuffer<float> ir (1, 3840);
        juce::Random random (7);

        for (int i = 0; i < ir.getNumSamples(); ++i)
            ir.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * std::exp (-static_cast<float> (i) / 600.0f));

        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        REQUIRE (stream != nullptr);

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (48000.0)
                                                       .withNumChannels (1)
                                                       .withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (ir, 0, ir.getNumSamples());
        return file;
    }

    void waitForAmpModel (SubdAmpProcessor& processor)
    {
        for (int i = 0; i < 500 && processor.getAmpStatus().isLoading; ++i)
            juce::Thread::sleep (10);

        INFO ("Message : " << processor.getAmpStatus().message);
        REQUIRE_FALSE (processor.getAmpStatus().isLoading);
        REQUIRE_FALSE (processor.getAmpStatus().hasError);
    }

    /** Amplitude de la composante à la fréquence donnée (Goertzel, fenêtre de Hann). */
    double magnitudeAt (const std::vector<float>& signal, double frequency, double sampleRate)
    {
        const auto n = signal.size();
        const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        const auto coefficient = 2.0 * std::cos (omega);
        double s1 = 0.0, s2 = 0.0, windowSum = 0.0;

        for (std::size_t i = 0; i < n; ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * static_cast<double> (i) / static_cast<double> (n));
            windowSum += window;
            const auto s0 = window * signal[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coefficient * s1 * s2)) / windowSum;
    }

    /** Sinusoïde mono dans un buffer stéréo (le plugin n'utilise que le canal 0). */
    struct SignalRunner
    {
        SignalRunner (SubdAmpProcessor& p, double rate, int size)
            : processor (p), sampleRate (rate), blockSize (size), buffer (2, size) {}

        SubdAmpProcessor& processor;
        double sampleRate;
        int blockSize;
        double phase = 0.0;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;

        /** Traite un bloc ; renvoie la plus grande variation d'un échantillon au suivant. */
        float processBlock (float frequency = 110.0f)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample (0, i, 0.3f * static_cast<float> (std::sin (phase)));
                phase += 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
            }

            buffer.clear (1, 0, blockSize);
            processor.processBlock (buffer, midi);

            float maxJump = 0.0f;

            for (int i = 1; i < blockSize; ++i)
                maxJump = std::max (maxJump, std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

            return maxJump;
        }

        bool outputIsFinite() const
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < blockSize; ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        return false;

            return true;
        }
    };
}

//==============================================================================
TEST_CASE ("Chaîne complète (tous les modules, ampli NAM, IR) sans aucune allocation dans processBlock", "[processor][realtime]")
{
    JuceEnvironment juce;
    const auto sampleRate = GENERATE (48000.0, 88200.0);

    auto processor = createProcessor();
    REQUIRE (processor != nullptr);

    processor->prepareToPlay (sampleRate, 64);

    setParameter (*processor, params::id::subEnabled, 1.0f);
    setParameter (*processor, params::id::fsEnabled, 1.0f);
    setParameter (*processor, params::id::subDown2, 50.0f);
    setParameter (*processor, params::id::whEnabled, 1.0f);
    setParameter (*processor, params::id::hmEnabled, 1.0f);
    setParameter (*processor, params::id::hmVoice2, 12.0f); // +5te

    // Les quatre textures, dont une avant l'ampli (en mono après elle), et le FREEZE figé.
    setParameter (*processor, params::id::shEnabled, 1.0f);
    setParameter (*processor, params::id::poEnabled, 1.0f);
    setParameter (*processor, params::id::poLevel2, 50.0f);
    setParameter (*processor, params::id::frEnabled, 1.0f);
    setParameter (*processor, params::id::frHold, 1.0f);
    setParameter (*processor, params::id::grEnabled, 1.0f);
    processor->moveModule (ModuleId::granular, 1);

    processor->loadAmpModel (exampleModels.getChildFile ("wavenet.nam"));
    processor->loadCabImpulseResponse (writeTestImpulseResponse (juce::File::getSpecialLocation (juce::File::tempDirectory)));
    waitForAmpModel (*processor);

    SignalRunner runner { *processor, sampleRate, 64 };

    // Mise en route : prise en compte de la capture, chargement de l'IR par la convolution.
    for (int i = 0; i < 2000; ++i)
        runner.processBlock();

    // Pendant le jeu : la chaîne change d'ordre plusieurs fois (le changement est demandé par
    // l'interface, hors mesure ; le thread audio l'applique, lui, dans la mesure).
    std::size_t allocations = 0;

    for (int move = 0; move < 4; ++move)
    {
        processor->moveModule (move % 2 == 0 ? ModuleId::shimmer : ModuleId::amp, move * 3);

        subdamp::test::ScopedAllocationCounter counter;

        for (int i = 0; i < 500; ++i)
            runner.processBlock();

        allocations += static_cast<std::size_t> (counter.count());
    }

    REQUIRE (allocations == 0);
    REQUIRE (runner.outputIsFinite());
}

TEST_CASE ("Les paramètres de l'HARMONIZER pilotent le module (Mi mineur, quinte au-dessus)", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);

    REQUIRE (params::harmonyVoiceSteps (0) == std::nullopt);
    REQUIRE (params::harmonyVoiceSteps (10) == 2);  // +3ce
    REQUIRE (params::harmonyVoiceSteps (12) == 4);  // +5te
    REQUIRE (params::harmonyVoiceSteps (1) == -14); // -2 octaves

    // Chaîne réduite à l'harmonizer, MIX 100 % : on n'entend que la voix.
    setParameter (*processor, params::id::gateEnabled, 0.0f);
    setParameter (*processor, params::id::cabEnabled, 0.0f);
    setParameter (*processor, params::id::hmEnabled, 1.0f);
    setParameter (*processor, params::id::hmVoice1, 12.0f);
    setParameter (*processor, params::id::hmLevel1, 100.0f);
    setParameter (*processor, params::id::hmMix, 100.0f);

    // Mi grave (82,41 Hz), dent de scie riche en harmoniques
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    std::vector<float> output;
    double phase = 0.0;

    for (int block = 0; block < 1500; ++block)
    {
        for (int i = 0; i < 64; ++i)
        {
            float sample = 0.0f;

            for (int k = 1; k < 40; ++k)
                sample += 0.2f * static_cast<float> (std::sin (k * phase)) / static_cast<float> (k);

            buffer.setSample (0, i, sample);
            phase += 2.0 * juce::MathConstants<double>::pi * 82.41 / 48000.0;
        }

        processor->processBlock (buffer, midi);
        output.insert (output.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 64);
    }

    dsp::PitchTracker tracker;
    tracker.prepare (48000.0);

    for (std::size_t i = output.size() / 2; i < output.size(); ++i)
        tracker.push (output[i]);

    REQUIRE (tracker.isVoiced());
    const auto expected = 82.41 * std::exp2 (7.0 / 12.0); // Si
    REQUIRE (std::abs (1200.0 * std::log2 (48000.0 / tracker.getPeriod() / expected)) < 5.0);

    // Badge : l'HARMONIZER allumé fait passer la latence perçue en JOUABLE.
    REQUIRE (processor->getLatencyStatus().tier == dsp::LatencyTier::playable);
    setParameter (*processor, params::id::hmEnabled, 0.0f);
    REQUIRE (processor->getLatencyStatus().tier == dsp::LatencyTier::live);
}

TEST_CASE ("Sans texture, la sortie est mono au centre ; une texture l'ouvre en stéréo", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);

    SignalRunner runner { *processor, 48000.0, 64 };

    const auto leftEqualsRight = [&runner]
    {
        for (int i = 0; i < runner.blockSize; ++i)
            if (std::abs (runner.buffer.getSample (0, i) - runner.buffer.getSample (1, i)) > 0.0f)
                return false;

        return true;
    };

    for (int i = 0; i < 100; ++i)
        runner.processBlock();

    REQUIRE (leftEqualsRight());
    REQUIRE (processor->getLatencyStatus().texturesActive == false);

    setParameter (*processor, params::id::shEnabled, 1.0f);
    setParameter (*processor, params::id::shMix, 50.0f);

    for (int i = 0; i < 300; ++i)
        runner.processBlock();

    REQUIRE_FALSE (leftEqualsRight());
    REQUIRE (runner.outputIsFinite());
    REQUIRE (processor->getLatencyStatus().texturesActive);
    REQUIRE (processor->getLatencyStatus().tier == dsp::LatencyTier::live); // le son direct n'attend pas
}

TEST_CASE ("Une texture à MIX 100 % n'a plus de son direct : le badge passe en TEXTURE", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);

    setParameter (*processor, params::id::poEnabled, 1.0f);
    setParameter (*processor, params::id::poMix, 50.0f);

    auto status = processor->getLatencyStatus();
    REQUIRE (status.tier == dsp::LatencyTier::live);
    REQUIRE (status.texturesActive);
    REQUIRE (status.textureMilliseconds > 40.0);

    setParameter (*processor, params::id::poMix, 100.0f);

    status = processor->getLatencyStatus();
    REQUIRE (status.tier == dsp::LatencyTier::texture);
    REQUIRE (status.milliseconds > 40.0); // tout ce qui suit attend la transposition
    REQUIRE_FALSE (status.texturesActive);

    // Un module à MIX 0 % ne change rien à ce qu'on entend.
    setParameter (*processor, params::id::poMix, 0.0f);
    REQUIRE (processor->getLatencyStatus() == LatencyStatus {});
}

TEST_CASE ("Sur une piste mono, les textures sont repliées en mono", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    REQUIRE (processor->setBusesLayout (layout));

    processor->prepareToPlay (48000.0, 64);
    setParameter (*processor, params::id::shEnabled, 1.0f);

    juce::AudioBuffer<float> buffer (1, 64);
    juce::MidiBuffer midi;

    for (int block = 0; block < 300; ++block)
    {
        for (int i = 0; i < 64; ++i)
            buffer.setSample (0, i, 0.3f * std::sin (0.05f * static_cast<float> (block * 64 + i)));

        processor->processBlock (buffer, midi);
    }

    for (int i = 0; i < 64; ++i)
        REQUIRE (std::isfinite (buffer.getSample (0, i)));
}

TEST_CASE ("Une capture rééchantillonnée déclare sa latence à l'hôte", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();

    processor->loadAmpModel (exampleModels.getChildFile ("wavenet.nam"));
    waitForAmpModel (*processor);

    processor->prepareToPlay (48000.0, 128);
    REQUIRE (processor->getLatencySamples() == 0);

    processor->prepareToPlay (88200.0, 128);
    REQUIRE (processor->getLatencySamples() > 0);
    REQUIRE (processor->getLatencySamples() < 88); // moins d'1 ms
}

TEST_CASE ("Un preset restaure les réglages, la capture et l'IR", "[processor][state]")
{
    JuceEnvironment juce;
    const auto ir = writeTestImpulseResponse (juce::File::getSpecialLocation (juce::File::tempDirectory));

    juce::MemoryBlock preset;

    {
        auto source = createProcessor();
        setParameter (*source, params::id::ampGain, 7.5f);
        setParameter (*source, params::id::subEnabled, 1.0f);
        setParameter (*source, params::id::subMix, 80.0f);
        source->moveModule (ModuleId::shimmer, 0);
        source->moveModule (ModuleId::sub, 6);
        source->loadAmpModel (exampleModels.getChildFile ("wavenet.nam"));
        source->loadCabImpulseResponse (ir);
        waitForAmpModel (*source);
        source->getStateInformation (preset);
    }

    auto restored = createProcessor();
    restored->setStateInformation (preset.getData(), static_cast<int> (preset.getSize()));
    waitForAmpModel (*restored);

    REQUIRE (parameterValue (*restored, params::id::ampGain) == Catch::Approx (7.5f));
    REQUIRE (parameterValue (*restored, params::id::subEnabled) == Catch::Approx (1.0f));
    REQUIRE (parameterValue (*restored, params::id::subMix) == Catch::Approx (80.0f));

    auto expectedOrder = withModuleMoved (withModuleMoved (defaultChainOrder, ModuleId::shimmer, 0), ModuleId::sub, 6);
    REQUIRE (restored->getChainOrder() == expectedOrder);
    REQUIRE (restored->getAmpStatus().fileName == "wavenet.nam");
    REQUIRE (restored->getCabStatus().fileName == ir.getFileName());
    REQUIRE_FALSE (restored->getCabStatus().hasError);
}

TEST_CASE ("Un preset de l'étape 1 (sans les nouveaux modules) remet ceux-ci à leurs valeurs par défaut", "[processor][state]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();

    // On dérègle d'abord les nouveaux paramètres...
    setParameter (*processor, params::id::subEnabled, 1.0f);
    setParameter (*processor, params::id::gateThreshold, -30.0f);

    // ...puis on charge un état tel que l'enregistrait l'étape 1.
    juce::XmlElement oldState ("SubdAmpState");
    oldState.setAttribute ("stateVersion", 1);

    for (const auto& [id, value] : { std::pair { "inputGain", 3.0 }, std::pair { "outputGain", -6.0 }, std::pair { "bypass", 0.0 } })
    {
        auto* parameter = oldState.createNewChildElement ("PARAM");
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }

    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (oldState, data);
    processor->setStateInformation (data.getData(), static_cast<int> (data.getSize()));

    REQUIRE (parameterValue (*processor, params::id::inputGain) == Catch::Approx (3.0f));
    REQUIRE (parameterValue (*processor, params::id::subEnabled) == Catch::Approx (0.0f));
    REQUIRE (parameterValue (*processor, params::id::gateThreshold) == Catch::Approx (-65.0f));
    REQUIRE (processor->getAmpStatus().fileName.isEmpty());

    // Migré vers la chaîne en série, dans l'ordre des positions par défaut de l'époque.
    REQUIRE (formatChainOrder (processor->getChainOrder())
             == "gate sub whammy amp cab harmonizer freqShift shimmer poly freeze granular");
    REQUIRE (parameterValue (*processor, params::id::subMix) == Catch::Approx (50.0f));
}

TEST_CASE ("Un état abîmé (ordre incomplet) donne toujours une chaîne complète", "[processor][state]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();

    juce::XmlElement state ("SubdAmpState");
    state.setAttribute ("stateVersion", 2);
    state.setAttribute ("chainOrder", "amp amp inconnu gate");

    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (state, data);
    processor->setStateInformation (data.getData(), static_cast<int> (data.getSize()));

    const auto order = processor->getChainOrder();
    REQUIRE (order[0] == ModuleId::amp);
    REQUIRE (order[1] == ModuleId::gate);
    REQUIRE (parseChainOrder (formatChainOrder (order)) == order);
}

TEST_CASE ("Déplacer un module dans la chaîne se fait sans saut dans le signal", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);

    setParameter (*processor, params::id::gateEnabled, 0.0f);
    setParameter (*processor, params::id::subEnabled, 1.0f);
    setParameter (*processor, params::id::subMix, 100.0f);

    SignalRunner runner { *processor, 48000.0, 64 };

    float steadyJump = 0.0f;

    for (int i = 0; i < 500; ++i)
        steadyJump = std::max (steadyJump, runner.processBlock());

    processor->moveModule (ModuleId::sub, 8);

    float switchJump = 0.0f;

    for (int i = 0; i < 100; ++i)
        switchJump = std::max (switchJump, runner.processBlock());

    // Le changement d'ordre passe par un fondu : aucune discontinuité plus forte qu'en jeu normal.
    REQUIRE (switchJump <= steadyJump * 1.5f);
}

TEST_CASE ("L'ordre choisi est bien celui du traitement (SUB puis FREQ SHIFT, ou l'inverse)", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);

    // Chaîne réduite à SUB (-1 octave seule) et FREQ SHIFT (+100 Hz seul).
    for (const auto* id : { params::id::gateEnabled, params::id::ampEnabled, params::id::cabEnabled })
        setParameter (*processor, id, 0.0f);

    setParameter (*processor, params::id::subEnabled, 1.0f);
    setParameter (*processor, params::id::subMix, 100.0f);
    setParameter (*processor, params::id::subDown1, 100.0f);
    setParameter (*processor, params::id::fsEnabled, 1.0f);
    setParameter (*processor, params::id::fsMix, 100.0f);
    setParameter (*processor, params::id::fsShift, 100.0f);

    const auto render = [&]
    {
        SignalRunner runner { *processor, 48000.0, 64 };
        std::vector<float> output;

        for (int i = 0; i < 400; ++i)
        {
            runner.processBlock (400.0f);

            if (i >= 200)
                output.insert (output.end(), runner.buffer.getReadPointer (0), runner.buffer.getReadPointer (0) + 64);
        }

        return output;
    };

    // 400 Hz → SUB → 200 Hz → +100 Hz → 300 Hz
    processor->setChainOrder (parseChainOrder ("sub freqShift"));
    const auto subFirst = render();
    REQUIRE (magnitudeAt (subFirst, 300.0, 48000.0) > 4.0 * magnitudeAt (subFirst, 250.0, 48000.0));

    // 400 Hz → +100 Hz → 500 Hz → SUB → 250 Hz
    processor->setChainOrder (parseChainOrder ("freqShift sub"));
    const auto shiftFirst = render();
    REQUIRE (magnitudeAt (shiftFirst, 250.0, 48000.0) > 4.0 * magnitudeAt (shiftFirst, 300.0, 48000.0));
}

TEST_CASE ("Voyant du GATE : éteint quand le son passe, allumé quand il coupe, éteint s'il est arrêté", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);
    setParameter (*processor, params::id::gateRelease, 80.0f);

    SignalRunner runner { *processor, 48000.0, 64 };

    // Une note bien au-dessus du seuil : le gate est ouvert, le voyant éteint.
    for (int i = 0; i < 100; ++i)
        runner.processBlock();

    REQUIRE (processor->getGateCutting() < 0.01f);

    // Silence : après le maintien (50 ms), la relâche referme le gate et le voyant s'allume.
    const auto silence = [&] (int blocks)
    {
        for (int i = 0; i < blocks; ++i)
        {
            runner.buffer.clear();
            processor->processBlock (runner.buffer, runner.midi);
        }
    };

    silence (30); // 40 ms : encore dans le maintien
    REQUIRE (processor->getGateCutting() < 0.01f);

    silence (300); // 400 ms de plus : relâche terminée
    REQUIRE (processor->getGateCutting() > 0.95f);

    // GATE arrêté : il ne coupe rien, voyant éteint.
    setParameter (*processor, params::id::gateEnabled, 0.0f);
    silence (20);
    REQUIRE (processor->getGateCutting() <= 0.0f);
}

TEST_CASE ("Le GATE n'a pas de MIX ; un réglage qui en contenait un se charge sans lui", "[processor][state]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();

    REQUIRE (processor->getParameters().getParameter ("gateMix") == nullptr);
    REQUIRE (processor->getModuleMix (ModuleId::gate) == Catch::Approx (1.0f));

    // État enregistré par la version précédente, avec le MIX du GATE à 50 %.
    juce::XmlElement saved ("SubdAmpState");
    saved.setAttribute ("stateVersion", 2);
    saved.setAttribute ("chainOrder", formatChainOrder (defaultChainOrder));

    for (const auto& [id, value] : { std::pair { "gateMix", 50.0 }, std::pair { "gateThreshold", -50.0 } })
    {
        auto* parameter = saved.createNewChildElement ("PARAM");
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    }

    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (saved, data);
    processor->setStateInformation (data.getData(), static_cast<int> (data.getSize()));

    REQUIRE (parameterValue (*processor, params::id::gateThreshold) == Catch::Approx (-50.0f));

    // Réenregistré, l'état ne garde pas l'identifiant retiré.
    juce::MemoryBlock resaved;
    processor->getStateInformation (resaved);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), static_cast<int> (resaved.getSize()));
    REQUIRE (xml != nullptr);
    REQUIRE (xml->getChildByAttribute ("id", "gateMix") == nullptr);
}

TEST_CASE ("Un fichier de capture introuvable est signalé sans planter", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();

    processor->loadAmpModel (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("absent.nam"));

    REQUIRE (processor->getAmpStatus().hasError);
    REQUIRE (processor->getAmpModelFile().getFileName() == "absent.nam"); // le chemin reste mémorisé
}

//==============================================================================
// Capture d'écran de l'interface, pour vérifier la mise en page sans hôte ni écran.
// Test caché : SubdAmpProcessorTests "[screenshot]", avec SUBDAMP_SCREENSHOT_DIR défini.
TEST_CASE ("Capture d'écran de l'interface avec une capture et une IR chargées", "[.][screenshot]")
{
    JuceEnvironment juce;
    const auto folder = juce::SystemStats::getEnvironmentVariable ("SUBDAMP_SCREENSHOT_DIR", {});
    REQUIRE (folder.isNotEmpty());

    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);
    setParameter (*processor, params::id::subEnabled, 1.0f);
    setParameter (*processor, params::id::hmEnabled, 1.0f);
    processor->moveModule (ModuleId::freqShift, 2); // FREQ SHIFT avant l'ampli…
    setParameter (*processor, params::id::fsEnabled, 1.0f); // … donc avant l'HARMO : bandeau de placement
    setParameter (*processor, params::id::shEnabled, 1.0f);
    setParameter (*processor, params::id::shMode, 1.0f);      // double : générations « ± »
    setParameter (*processor, params::id::shPreDelay, 120.0f); // visible sur la courbe
    setParameter (*processor, params::id::frEnabled, 1.0f);
    setParameter (*processor, params::id::frHold, 1.0f);
    setParameter (*processor, params::id::poEnabled, 1.0f);
    processor->loadAmpModel (exampleModels.getChildFile ("wavenet.nam"));
    processor->loadCabImpulseResponse (writeTestImpulseResponse (juce::File::getSpecialLocation (juce::File::tempDirectory)));
    waitForAmpModel (*processor);

    // Une note jouée, pour que la latence de l'HARMONIZER soit mesurée.
    SignalRunner runner { *processor, 48000.0, 64 };

    for (int i = 0; i < 300; ++i)
        runner.processBlock (164.81f);

    // Puis un court silence : le GATE se referme, son voyant s'allume sur la capture.
    for (int i = 0; i < 200; ++i)
    {
        runner.buffer.clear();
        processor->processBlock (runner.buffer, runner.midi);
    }

    // Une capture par module ouvert. Les fichiers sont déjà chargés : l'interface affiche
    // leur état dès sa création.
    for (const auto& info : moduleInfos)
    {
        processor->openModule = info.id;
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
        REQUIRE (editor != nullptr);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = juce::File (folder).getChildFile (juce::String ("subdamp-") + info.pedalName + ".png");
        file.deleteFile();

        juce::FileOutputStream stream (file);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));

        processor->editorBeingDeleted (editor.get());
    }

    // Tailles extrêmes autorisées par la fenêtre : 60 % et 150 %.
    processor->openModule = ModuleId::harmonizer;

    for (const auto percent : { 60, 150 })
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
        editor->setSize (1280 * percent / 100, 720 * percent / 100);

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = juce::File (folder).getChildFile ("subdamp-taille-" + juce::String (percent) + ".png");
        file.deleteFile();

        juce::FileOutputStream stream (file);
        REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
        processor->editorBeingDeleted (editor.get());
    }
}

//==============================================================================
// Glisser-déposer dans le pédalier, avec de vrais événements souris sur les composants.
// Test caché (il lui faut un affichage) : SubdAmpProcessorTests "[gui]", sous xvfb-run.
TEST_CASE ("Glisser une pédale dans le pédalier change l'ordre de la chaîne ; un clic ouvre ses réglages", "[.][gui]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);
    processor->openModule = ModuleId::amp;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
    REQUIRE (editor != nullptr);

    gui::PedalBoard* board = nullptr;

    for (auto* child : editor->getChildren())
        if (auto* found = dynamic_cast<gui::PedalBoard*> (child))
            board = found;

    REQUIRE (board != nullptr);

    // Les pédales sont créées dans l'ordre de ModuleId.
    auto& pedal = *board->getChildComponent (static_cast<int> (indexOf (ModuleId::shimmer)));
    auto& gatePedal = *board->getChildComponent (static_cast<int> (indexOf (ModuleId::gate)));

    auto source = juce::Desktop::getInstance().getMainMouseSource();

    const auto event = [&] (juce::Component& target, juce::Point<float> inBoard, juce::Point<float> downInBoard, bool dragged)
    {
        const auto offset = target.getPosition().toFloat();
        return juce::MouseEvent (source, inBoard - offset, juce::ModifierKeys::leftButtonModifier,
                                 juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &target, &target, juce::Time::getCurrentTime(), downInBoard - offset,
                                 juce::Time::getCurrentTime(), 1, dragged);
    };

    // Le SHIMMER (dernier) glissé tout au début, avant le GATE.
    const auto start = pedal.getBounds().getCentre().toFloat();
    const auto target = gatePedal.getBounds().getCentre().toFloat() - juce::Point<float> (10.0f, 0.0f);

    pedal.mouseDown (event (pedal, start, start, false));

    for (int step = 1; step <= 20; ++step)
    {
        const auto position = start + (target - start) * (static_cast<float> (step) / 20.0f);
        pedal.mouseDrag (event (pedal, position, start, true));

        // Capture à mi-chemin : la case de destination et la pédale soulevée.
        if (step == 12)
            if (const auto folder = juce::SystemStats::getEnvironmentVariable ("SUBDAMP_SCREENSHOT_DIR", {}); folder.isNotEmpty())
            {
                const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream stream (juce::File (folder).getChildFile ("subdamp-glisser.png").getNonexistentSibling());
                REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
            }
    }

    // Pendant le glisser, l'ordre de la chaîne ne change pas encore.
    REQUIRE (processor->getChainOrder() == defaultChainOrder);

    pedal.mouseUp (event (pedal, target, start, true));

    const auto order = processor->getChainOrder();
    REQUIRE (order[0] == ModuleId::shimmer);
    REQUIRE (order[1] == ModuleId::gate);
    REQUIRE (processor->openModule == ModuleId::shimmer);

    // Un simple clic (sans glisser) ouvre les réglages, sans rien déplacer.
    const auto gateCentre = gatePedal.getBounds().getCentre().toFloat();
    gatePedal.mouseDown (event (gatePedal, gateCentre, gateCentre, false));
    gatePedal.mouseUp (event (gatePedal, gateCentre, gateCentre, false));

    REQUIRE (processor->getChainOrder() == order);
    REQUIRE (processor->openModule == ModuleId::gate);

    processor->editorBeingDeleted (editor.get());
}

TEST_CASE ("La prise d'oscilloscope rend ce qu'on entend, décimé vers 24 kHz", "[processor]")
{
    ScopeTap tap;
    tap.prepare (48000.0);
    REQUIRE (tap.getSampleRate() == Catch::Approx (24000.0));

    std::vector<float> left (1000), right (1000);

    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = static_cast<float> (i);
        right[i] = static_cast<float> (i) + 2.0f;
    }

    tap.push (left, right);

    // Un échantillon sur deux (indices 993, 995, 997, 999), moyenne gauche / droite (i + 1) ;
    // les plus récents en dernier.
    std::vector<float> latest (4);
    tap.copyLatest (latest);
    REQUIRE (latest == std::vector<float> { 994.0f, 996.0f, 998.0f, 1000.0f });
}

//==============================================================================
// Banc d'essai des ordres de la chaîne (rapport détaillé) : SubdAmpProcessorTests "[combinations]".
namespace
{
    struct ChainMetrics
    {
        bool finite = true;
        float peak = 0.0f;
        double playingRms = 0.0, tailRms = 0.0;
        int dropouts = 0;
        float maxJumpRatio = 0.0f; // plus grand saut d'un échantillon au suivant / niveau local
    };

    /** Phrase de guitare : six notes pincées en legato, puis une seconde de silence. */
    std::vector<float> chainTestPhrase (double sampleRate)
    {
        const std::vector<int> delays { 582, 436, 326, 244, 194, 291 };
        std::vector<float> phrase;

        for (std::size_t k = 0; k < delays.size(); ++k)
        {
            const auto note = subdamp::test::pluckedString (delays[k], sampleRate, static_cast<int> (sampleRate * 0.4), 0.3f, static_cast<unsigned> (40 + k));
            phrase.insert (phrase.end(), note.begin(), note.end());
        }

        phrase.resize (phrase.size() + static_cast<std::size_t> (sampleRate), 0.0f);
        return phrase;
    }

    ChainMetrics renderChain (SubdAmpProcessor& processor, const ChainOrder& order, const std::vector<float>& input, double sampleRate)
    {
        constexpr int block = 256;
        processor.setChainOrder (order);
        processor.prepareToPlay (sampleRate, block); // l'ordre s'applique directement, état remis à zéro

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        std::vector<float> left;
        left.reserve (input.size());

        for (std::size_t start = 0; start + block <= input.size(); start += block)
        {
            buffer.clear();
            buffer.copyFrom (0, 0, input.data() + start, block);
            processor.processBlock (buffer, midi);
            left.insert (left.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
        }

        ChainMetrics metrics;
        const auto playingEnd = input.size() - static_cast<std::size_t> (sampleRate);
        const auto window = static_cast<std::size_t> (sampleRate * 0.01);
        double recent = 0.0;
        bool inDrop = false;
        double playing = 0.0, tail = 0.0;

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto s = left[i];
            metrics.finite = metrics.finite && std::isfinite (s);
            metrics.peak = std::max (metrics.peak, std::abs (s));
            (i < playingEnd ? playing : tail) += static_cast<double> (s) * s;
        }

        metrics.playingRms = std::sqrt (playing / static_cast<double> (playingEnd));
        metrics.tailRms = std::sqrt (tail / static_cast<double> (left.size() - playingEnd));

        // Coupures : le niveau de sortie s'effondre (-30 dB en 10 ms) pendant que la guitare joue.
        for (std::size_t start = window * 10; start + window <= playingEnd; start += window)
        {
            double energy = 0.0, local = 0.0;

            for (std::size_t i = start; i < start + window; ++i)
                energy += static_cast<double> (left[i]) * left[i];

            const auto level = std::sqrt (energy / static_cast<double> (window));
            const auto drop = recent > 1.0e-3 && level < recent * 0.03;

            if (drop && ! inDrop)
                ++metrics.dropouts;

            inDrop = drop;

            if (! drop)
                recent = 0.6 * recent + 0.4 * level;

            // Saut d'un échantillon au suivant comparé au niveau de la fenêtre (clic)
            for (std::size_t i = start + 1; i < start + window; ++i)
                local = std::max (local, static_cast<double> (std::abs (left[i] - left[i - 1])));

            if (level > 1.0e-3)
                metrics.maxJumpRatio = std::max (metrics.maxJumpRatio, static_cast<float> (local / level));
        }

        return metrics;
    }

    void enableEverything (SubdAmpProcessor& processor)
    {
        for (const auto& info : moduleInfos)
            setParameter (processor, info.enabledParameter, 1.0f);

        setParameter (processor, params::id::whMode, 0.0f);       // WHAMMY fixe
        setParameter (processor, params::id::whInterval, 12.0f);
        setParameter (processor, params::id::whMix, 50.0f);
        setParameter (processor, params::id::fsMix, 30.0f);
        setParameter (processor, params::id::fsShift, 7.0f);
        setParameter (processor, params::id::poLevel2, 40.0f);
    }

    juce::String describe (const ChainOrder& order)
    {
        juce::StringArray names;

        for (const auto module : order)
            names.add (infoFor (module).pedalName);

        return names.joinIntoString (" > ");
    }
}

TEST_CASE ("Ordres de la chaîne : paires, places, permutations (rapport)", "[.][combinations]")
{
    JuceEnvironment juce;
    constexpr double sampleRate = 48000.0;
    const auto input = chainTestPhrase (sampleRate);
    auto processor = createProcessor();

    struct Row { juce::String label; ChainMetrics metrics; };
    std::vector<Row> rows;

    // 1. Chaque paire ordonnée, seule allumée (les autres arrêtés)
    for (const auto& first : moduleInfos)
        for (const auto& second : moduleInfos)
        {
            if (first.id == second.id)
                continue;

            for (const auto& info : moduleInfos)
                setParameter (*processor, info.enabledParameter, info.id == first.id || info.id == second.id ? 1.0f : 0.0f);

            auto order = withModuleMoved (defaultChainOrder, first.id, 0);
            order = withModuleMoved (order, second.id, 1);
            rows.push_back ({ juce::String ("paire ") + first.pedalName + " > " + second.pedalName,
                              renderChain (*processor, order, input, sampleRate) });
        }

    // 2. Tous allumés, chaque module à chaque place
    enableEverything (*processor);

    for (const auto& info : moduleInfos)
        for (std::size_t place = 0; place < numModules; ++place)
        {
            const auto order = withModuleMoved (defaultChainOrder, info.id, place);
            rows.push_back ({ "place " + describe (order), renderChain (*processor, order, input, sampleRate) });
        }

    // 3. Permutations complètes au hasard (graine fixe : reproductible)
    std::mt19937 random (2026);

    for (int i = 0; i < 200; ++i)
    {
        auto order = defaultChainOrder;
        std::shuffle (order.begin(), order.end(), random);
        rows.push_back ({ "aléatoire " + describe (order), renderChain (*processor, order, input, sampleRate) });
    }

    // Rapport : les cas suspects d'abord
    int suspicious = 0;

    for (const auto& row : rows)
    {
        const auto& m = row.metrics;
        const auto bad = ! m.finite || m.peak > 2.0f || m.playingRms < 0.01 || m.dropouts > 0 || m.maxJumpRatio > 8.0f;

        if (bad)
        {
            ++suspicious;
            std::printf ("SUSPECT %s : fini=%d crête=%.2f rms=%.4f queue=%.4f coupures=%d saut=%.1f\n",
                         row.label.toRawUTF8(), m.finite, m.peak, m.playingRms, m.tailRms, m.dropouts, m.maxJumpRatio);
        }
    }

    std::printf ("%zu rendus, %d suspects\n", rows.size(), suspicious);
    SUCCEED();
}

TEST_CASE ("Niveau de chaque module seul, à son MIX par défaut (rapport)", "[.][combinations]")
{
    JuceEnvironment juce;
    constexpr double sampleRate = 48000.0;
    const auto input = chainTestPhrase (sampleRate);
    auto processor = createProcessor();

    for (const auto& info : moduleInfos)
        setParameter (*processor, info.enabledParameter, 0.0f);

    const auto reference = renderChain (*processor, defaultChainOrder, input, sampleRate).playingRms;

    for (const auto& module : moduleInfos)
    {
        for (const auto& info : moduleInfos)
            setParameter (*processor, info.enabledParameter, info.id == module.id ? 1.0f : 0.0f);

        const auto level = renderChain (*processor, defaultChainOrder, input, sampleRate).playingRms;
        std::printf ("%-10s MIX %3.0f %%  : %+5.1f dB\n", module.pedalName, 100.0 * processor->getModuleMix (module.id),
                     20.0 * std::log10 (level / reference));
    }

    SUCCEED();
}

TEST_CASE ("Suivi de l'HARMONIZER selon le module placé avant lui (rapport)", "[.][combinations]")
{
    JuceEnvironment juce;
    constexpr double sampleRate = 48000.0;
    const auto input = chainTestPhrase (sampleRate);
    auto processor = createProcessor();
    processor->prepareToPlay (sampleRate, 256);

    for (const auto& before : moduleInfos)
    {
        if (before.id == ModuleId::harmonizer)
            continue;

        for (const auto& info : moduleInfos)
            setParameter (*processor, info.enabledParameter, info.id == before.id || info.id == ModuleId::harmonizer ? 1.0f : 0.0f);

        processor->setChainOrder (withModuleMoved (withModuleMoved (defaultChainOrder, before.id, 0), ModuleId::harmonizer, 1));
        processor->prepareToPlay (sampleRate, 256);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        int tracked = 0, playing = 0;
        const auto playingEnd = input.size() - static_cast<std::size_t> (sampleRate);

        for (std::size_t start = 0; start + 256 <= playingEnd; start += 256)
        {
            buffer.clear();
            buffer.copyFrom (0, 0, input.data() + start, 256);
            processor->processBlock (buffer, midi);
            ++playing;
            tracked += processor->getTelemetry().harmonyNote.load() >= 0 ? 1 : 0;
        }

        std::printf ("%-8s avant HARMO : note suivie %3d %% du temps\n", before.pedalName, 100 * tracked / playing);
    }

    SUCCEED();
}

TEST_CASE ("Ordres de la chaîne : aucune combinaison ne casse le son (paires, places, permutations)", "[processor][combinations-quick]")
{
    // Régression : avec le mélange linéaire, chaque module dosé faisait perdre 3 à 6 dB ; une
    // chaîne complète tombait à -30 dB (et le GATE, placé tard, se mettait à couper).
    JuceEnvironment juce;
    constexpr double sampleRate = 48000.0;
    const auto input = chainTestPhrase (sampleRate);
    auto processor = createProcessor();

    for (const auto& info : moduleInfos)
        setParameter (*processor, info.enabledParameter, 0.0f);

    const auto reference = renderChain (*processor, defaultChainOrder, input, sampleRate).playingRms;

    const auto check = [&] (const ChainOrder& order, double minimumLevel)
    {
        const auto m = renderChain (*processor, order, input, sampleRate);
        INFO (describe (order) << " : crête " << m.peak << ", niveau " << 20.0 * std::log10 (m.playingRms / reference)
                               << " dB, coupures " << m.dropouts << ", saut " << m.maxJumpRatio);
        REQUIRE (m.finite);
        REQUIRE (m.peak < 2.0f);
        REQUIRE (m.playingRms > minimumLevel * reference);
        REQUIRE (m.dropouts == 0);
        REQUIRE (m.maxJumpRatio < 10.0f);
    };

    // Chaque paire ordonnée, seule allumée : jamais plus de 6 dB de perte.
    for (const auto& first : moduleInfos)
        for (const auto& second : moduleInfos)
        {
            if (first.id == second.id)
                continue;

            for (const auto& info : moduleInfos)
                setParameter (*processor, info.enabledParameter, info.id == first.id || info.id == second.id ? 1.0f : 0.0f);

            check (withModuleMoved (withModuleMoved (defaultChainOrder, first.id, 0), second.id, 1), 0.5);
        }

    // Tout allumé : chaque module en tête et en fin de chaîne, plus des ordres au hasard ;
    // jamais plus de 10,5 dB de perte, malgré onze modules empilés (dont les pertes voulues :
    // octaves du SUB plus basses que le direct, filtrage du cab, voix du POLY à 60 %).
    enableEverything (*processor);

    for (const auto& info : moduleInfos)
        for (const auto place : { std::size_t (0), numModules - 1 })
            check (withModuleMoved (defaultChainOrder, info.id, place), 0.3);

    std::mt19937 random (11);

    for (int i = 0; i < 12; ++i)
    {
        auto order = defaultChainOrder;
        std::shuffle (order.begin(), order.end(), random);
        check (order, 0.3);
    }
}
