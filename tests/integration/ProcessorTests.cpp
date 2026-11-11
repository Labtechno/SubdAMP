// Tests du processeur complet (chaîne, fichiers, état sauvegardé), sans hôte audio.

#include "../realtime/AllocationTracking.h"

#include "gui/PedalBoard.h"
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
    processor->moveModule (ModuleId::freqShift, 2); // FREQ SHIFT avant l'ampli
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
