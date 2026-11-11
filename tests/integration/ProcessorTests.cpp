// Tests du processeur complet (chaîne, fichiers, état sauvegardé), sans hôte audio.

#include "../realtime/AllocationTracking.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "subdamp/dsp/PitchTracker.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

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

    // Les quatre textures, dont une sur la prise avant l'ampli, et le FREEZE figé.
    setParameter (*processor, params::id::shEnabled, 1.0f);
    setParameter (*processor, params::id::poEnabled, 1.0f);
    setParameter (*processor, params::id::poLevel2, 50.0f);
    setParameter (*processor, params::id::frEnabled, 1.0f);
    setParameter (*processor, params::id::frHold, 1.0f);
    setParameter (*processor, params::id::grEnabled, 1.0f);
    setParameter (*processor, params::id::grSource, 0.0f);

    processor->loadAmpModel (exampleModels.getChildFile ("wavenet.nam"));
    processor->loadCabImpulseResponse (writeTestImpulseResponse (juce::File::getSpecialLocation (juce::File::tempDirectory)));
    waitForAmpModel (*processor);

    SignalRunner runner { *processor, sampleRate, 64 };

    // Mise en route : prise en compte de la capture, chargement de l'IR par la convolution.
    for (int i = 0; i < 2000; ++i)
        runner.processBlock();

    subdamp::test::ScopedAllocationCounter counter;

    for (int i = 0; i < 2000; ++i)
        runner.processBlock();

    REQUIRE (counter.count() == 0);
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

    // Chaîne réduite à l'harmonizer, son direct coupé : on n'entend que la voix.
    setParameter (*processor, params::id::gateEnabled, 0.0f);
    setParameter (*processor, params::id::cabEnabled, 0.0f);
    setParameter (*processor, params::id::hmEnabled, 1.0f);
    setParameter (*processor, params::id::hmVoice1, 12.0f);
    setParameter (*processor, params::id::hmLevel1, 100.0f);
    setParameter (*processor, params::id::hmDirect, 0.0f);

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
    setParameter (*processor, params::id::shLevel, 100.0f);

    for (int i = 0; i < 300; ++i)
        runner.processBlock();

    REQUIRE_FALSE (leftEqualsRight());
    REQUIRE (runner.outputIsFinite());
    REQUIRE (processor->getLatencyStatus().texturesActive);
    REQUIRE (processor->getLatencyStatus().tier == dsp::LatencyTier::live); // le son direct n'attend pas
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
        setParameter (*source, params::id::subPosition, 1.0f);
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
    REQUIRE (parameterValue (*restored, params::id::subPosition) == Catch::Approx (1.0f));
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
}

TEST_CASE ("Déplacer un module avant / après l'ampli se fait sans saut dans le signal", "[processor]")
{
    JuceEnvironment juce;
    auto processor = createProcessor();
    processor->prepareToPlay (48000.0, 64);

    setParameter (*processor, params::id::gateEnabled, 0.0f);
    setParameter (*processor, params::id::subEnabled, 1.0f);
    setParameter (*processor, params::id::subDry, 0.0f);

    SignalRunner runner { *processor, 48000.0, 64 };

    float steadyJump = 0.0f;

    for (int i = 0; i < 500; ++i)
        steadyJump = std::max (steadyJump, runner.processBlock());

    setParameter (*processor, params::id::subPosition, 1.0f);

    float switchJump = 0.0f;

    for (int i = 0; i < 100; ++i)
        switchJump = std::max (switchJump, runner.processBlock());

    // Le changement de place passe par un fondu : aucune discontinuité plus forte qu'en jeu normal.
    REQUIRE (switchJump <= steadyJump * 1.5f);
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
    setParameter (*processor, params::id::fsPosition, 0.0f); // FREQ SHIFT avant l'ampli
    setParameter (*processor, params::id::shEnabled, 1.0f);
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
}
