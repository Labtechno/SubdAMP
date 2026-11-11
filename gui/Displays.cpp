#include "gui/Displays.h"

#include "gui/Theme.h"
#include "subdamp/dsp/Decibels.h"
#include "subdamp/dsp/ShimmerReverb.h"
#include "subdamp/dsp/ToneStack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace subdamp::gui
{

namespace
{
    juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    // Couleurs d'écran : trace principale, information secondaire, consigne, alerte.
    const juce::Colour primary   = theme::phosphor;
    const juce::Colour secondary = theme::phosphor.withAlpha (0.55f);
    const juce::Colour faint     = theme::phosphor.withAlpha (0.28f);

    constexpr double minHz = 40.0, maxHz = 12000.0;

    float xForFrequency (juce::Rectangle<float> area, double hz)
    {
        return area.getX() + area.getWidth() * static_cast<float> (std::log (hz / minHz) / std::log (maxHz / minHz));
    }

    double frequencyForProportion (double proportion)
    {
        return minHz * std::pow (maxHz / minHz, proportion);
    }

    void caption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float>& area, float scale)
    {
        g.setColour (secondary);
        g.setFont (theme::monoFont (10.5f * scale));
        g.drawText (text, area.removeFromTop (15.0f * scale), juce::Justification::centredLeft, false);
        area.removeFromTop (4.0f * scale);
    }

    void screenText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                     juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft)
    {
        g.setColour (colour);
        g.setFont (theme::monoFont (height));
        g.drawText (text, area, justification, false);
    }

    /** Trace lumineuse : halo large et pâle, puis trait fin. */
    void glowPath (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float scale)
    {
        g.setColour (colour.withAlpha (0.18f));
        g.strokePath (path, juce::PathStrokeType (5.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.6f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void dashedLine (juce::Graphics& g, juce::Line<float> line, juce::Colour colour, float scale)
    {
        const float dashes[] { 4.0f * scale, 3.0f * scale };
        g.setColour (colour);
        g.drawDashedLine (line, dashes, 2, 1.0f * scale);
    }

    juce::String noteName (int midiNote)
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midiNote % 12) + 12) % 12]) + juce::String (midiNote / 12 - 1);
    }

    juce::String signedValue (float value, int decimals)
    {
        return (value > 0.0f ? "+" : "") + juce::String (value, decimals);
    }

    juce::String percent (float value)
    {
        return juce::String (juce::roundToInt (value)) + " %";
    }
}

//==============================================================================
SegmentReadout::SegmentReadout (const juce::String& captionText, const juce::String& unitText, int numberOfDigits)
    : caption (captionText), unit (unitText), valueColour (theme::phosphor), statusColour (theme::textDim), digits (numberOfDigits)
{
    setInterceptsMouseClicks (true, false); // infobulle
}

void SegmentReadout::setValue (const juce::String& text, juce::Colour colour)
{
    if (text != value || colour != valueColour)
    {
        value = text;
        valueColour = colour;
        repaint();
    }
}

void SegmentReadout::setStatus (const juce::String& text, juce::Colour colour)
{
    if (text != status || colour != statusColour)
    {
        status = text;
        statusColour = colour;
        repaint();
    }
}

void SegmentReadout::paint (juce::Graphics& g)
{
    auto inner = theme::drawScreen (g, getLocalBounds().toFloat(), scale);

    auto top = inner.removeFromTop (13.0f * scale);
    g.setColour (theme::textDim);
    g.setFont (theme::monoFont (10.0f * scale));
    g.drawText (caption, top, juce::Justification::centredLeft, false);

    if (status.isNotEmpty())
    {
        g.setColour (statusColour);
        g.setFont (theme::labelFont (11.0f * scale));
        g.drawText (status, top, juce::Justification::centredRight, false);
    }

    // Valeur en segments, alignée à droite avant l'unité, sur un nombre fixe de chiffres
    auto unitArea = inner.removeFromRight (juce::GlyphArrangement::getStringWidth (theme::monoFont (11.0f * scale), unit) + 4.0f * scale);
    g.setColour (theme::textDim);
    g.setFont (theme::monoFont (11.0f * scale));
    g.drawText (unit, unitArea.withTrimmedTop (inner.getHeight() * 0.35f), juce::Justification::centredRight, false);

    auto shown = value;

    while (shown.length() < digits)
        shown = " " + shown;

    theme::drawSegments (g, shown, inner.reduced (2.0f * scale, 0.0f), juce::jmin (inner.getHeight() * 0.92f, 24.0f * scale),
                         valueColour, juce::Justification::centredRight, false);
}

//==============================================================================
Oscilloscope::Oscilloscope (const ScopeTap& tapToRead)
    : tap (tapToRead), latest (4096, 0.0f)
{
    setInterceptsMouseClicks (false, false);
}

void Oscilloscope::refresh()
{
    tap.copyLatest (latest);

    // 40 ms affichées ; déclenchement sur le dernier front montant qui laisse une fenêtre complète.
    const auto window = static_cast<std::size_t> (tap.getSampleRate() * 0.04);
    const auto size = latest.size();
    auto start = size - window;

    for (auto i = size - window - 1; i > size / 2; --i)
    {
        if (latest[i - 1] < 0.0f && latest[i] >= 0.0f)
        {
            start = i;
            break;
        }
    }

    trace.assign (latest.begin() + static_cast<std::ptrdiff_t> (start),
                  latest.begin() + static_cast<std::ptrdiff_t> (start + window));
    repaint();
}

void Oscilloscope::paint (juce::Graphics& g)
{
    auto inner = theme::drawScreen (g, getLocalBounds().toFloat(), scale);
    theme::drawGraticule (g, inner, 8, 4, scale);

    g.setColour (secondary);
    g.setFont (theme::monoFont (9.5f * scale));
    g.drawText ("SORTIE", inner.reduced (3.0f * scale, 1.0f * scale), juce::Justification::topLeft, false);
    g.drawText (utf8 ("5 ms/DIV · 0,5 FS/DIV"), inner.reduced (3.0f * scale, 1.0f * scale), juce::Justification::bottomRight, false);

    if (trace.size() < 2)
        return;

    juce::Path path;

    for (std::size_t i = 0; i < trace.size(); ++i)
    {
        const auto x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (trace.size() - 1);
        const auto y = inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, trace[i]) * inner.getHeight() * 0.5f;

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    glowPath (g, path, primary, scale);
}

//==============================================================================
ModuleDisplay::ModuleDisplay (ModuleId moduleToShow, SubdAmpProcessor& processorToUse)
    : module (moduleToShow), processor (processorToUse)
{
    setInterceptsMouseClicks (false, false);

    if (module == ModuleId::freqShift || module == ModuleId::sub || module == ModuleId::poly)
    {
        fft.prepare (2048);
        fftData.resize (2048);
        scopeSamples.resize (2048);
        spectrumDb.assign (1024, -120.0f);
    }
}

float ModuleDisplay::parameter (const char* id) const
{
    const auto* value = processor.getParameters().getRawParameterValue (id);
    return value != nullptr ? value->load (std::memory_order_relaxed) : 0.0f;
}

std::vector<float> ModuleDisplay::signature() const
{
    const auto& telemetry = processor.getTelemetry();
    const auto status = processor.isModuleEnabled (module) ? 1.0f : 0.0f;

    switch (module)
    {
        case ModuleId::gate:
            return { status, juce::roundToInt (dsp::gainToDecibels (telemetry.gateLevel.load()) * 2.0f) * 0.5f,
                     telemetry.gateCutting.load() > 0.5f ? 1.0f : 0.0f, parameter (params::id::gateThreshold), parameter (params::id::gateRelease) };
        case ModuleId::sub:
        case ModuleId::freqShift:
        case ModuleId::poly:
            return {}; // spectre : redessiné à chaque rafraîchissement
        case ModuleId::whammy:
            return { status, std::round (telemetry.whammySemitones.load() * 20.0f), parameter (params::id::whInterval), parameter (params::id::whMode) };
        case ModuleId::harmonizer:
            return { status, static_cast<float> (telemetry.harmonyNote.load()), static_cast<float> (telemetry.harmonyShift1.load()),
                     static_cast<float> (telemetry.harmonyShift2.load()), parameter (params::id::hmKey), parameter (params::id::hmScale),
                     parameter (params::id::hmVoice1), parameter (params::id::hmVoice2) };
        case ModuleId::amp:
            return { status, parameter (params::id::ampGain), parameter (params::id::ampBass), parameter (params::id::ampMid),
                     parameter (params::id::ampTreble), parameter (params::id::ampLevel),
                     static_cast<float> (processor.getAmpStatus().fileName.hashCode()) };
        case ModuleId::cab:
            return { status, static_cast<float> (processor.getCabStatus().fileName.hashCode()), static_cast<float> (impulseResponseDb.size()) };
        case ModuleId::shimmer:
            return { status, parameter (params::id::shInterval), parameter (params::id::shDecay), parameter (params::id::shAmount), parameter (params::id::shTone) };
        case ModuleId::freeze:
            return { status, telemetry.freezeHeld.load() ? 1.0f : 0.0f, parameter (params::id::frAttack), parameter (params::id::frRelease), parameter (params::id::frTone) };
        case ModuleId::granular:
            return { status, parameter (params::id::grMode), parameter (params::id::grSize), parameter (params::id::grDensity),
                     parameter (params::id::grDivision), parameter (params::id::grRepeats), static_cast<float> (processor.getTempo()) };
    }

    return {};
}

void ModuleDisplay::refresh()
{
    // Seul l'écran du module ouvert travaille (les autres façades sont masquées).
    if (! isVisible() || getParentComponent() == nullptr || ! getParentComponent()->isVisible())
        return;

    if (module == ModuleId::freqShift || module == ModuleId::sub || module == ModuleId::poly)
    {
        updateSpectrum();
        repaint();
        return;
    }

    if (module == ModuleId::cab)
        updateImpulseResponseCurve();

    if (auto current = signature(); current != shownSignature)
    {
        shownSignature = std::move (current);
        repaint();
    }
}

void ModuleDisplay::updateSpectrum()
{
    processor.getScope().copyLatest (scopeSamples);

    const auto size = scopeSamples.size();

    for (std::size_t i = 0; i < size; ++i)
    {
        const auto window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / static_cast<float> (size));
        fftData[i] = { scopeSamples[i] * window, 0.0f };
    }

    fft.forward (fftData.data());

    // Retombée douce, comme un analyseur matériel ; montée instantanée.
    for (std::size_t bin = 0; bin < spectrumDb.size(); ++bin)
    {
        const auto magnitude = std::abs (fftData[bin]) * 4.0f / static_cast<float> (size);
        const auto decibels = juce::jmax (-120.0f, dsp::gainToDecibels (magnitude));
        spectrumDb[bin] = juce::jmax (decibels, spectrumDb[bin] - 2.5f);
    }
}

void ModuleDisplay::updateImpulseResponseCurve()
{
    const auto status = processor.getCabStatus();
    const auto file = processor.getCabImpulseResponseFile();
    const auto key = status.hasError || status.fileName.isEmpty() ? juce::String() : file.getFullPathName();

    if (key == impulseResponseShown)
        return;

    impulseResponseShown = key;
    impulseResponseDb.clear();

    if (key.isEmpty())
        return;

    // Lecture du fichier sur le thread de l'interface : une IR fait quelques dizaines de ko.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return;

    constexpr int size = 8192;
    const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, size));
    juce::AudioBuffer<float> buffer (1, size);
    buffer.clear();
    reader->read (&buffer, 0, length, 0, true, false);

    impulseResponseSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    impulseResponseRate = reader->sampleRate;

    dsp::Fft transform;
    transform.prepare (size);
    std::vector<std::complex<float>> data (size);

    for (int i = 0; i < size; ++i)
        data[static_cast<std::size_t> (i)] = { buffer.getSample (0, i), 0.0f };

    transform.forward (data.data());

    // Courbe lissée au 1/6 d'octave, normalisée sur son maximum.
    constexpr int points = 160;
    impulseResponseDb.resize (points);
    auto peak = -200.0f;

    for (int p = 0; p < points; ++p)
    {
        const auto centre = frequencyForProportion (static_cast<double> (p) / (points - 1));
        const auto low = centre * std::pow (2.0, -1.0 / 12.0), high = centre * std::pow (2.0, 1.0 / 12.0);
        const auto binLow = juce::jmax (1, static_cast<int> (low * size / reader->sampleRate));
        const auto binHigh = juce::jmax (binLow, juce::jmin (size / 2 - 1, static_cast<int> (high * size / reader->sampleRate)));
        double power = 0.0;

        for (int bin = binLow; bin <= binHigh; ++bin)
            power += std::norm (data[static_cast<std::size_t> (bin)]);

        const auto decibels = static_cast<float> (10.0 * std::log10 (power / (binHigh - binLow + 1) + 1.0e-20));
        impulseResponseDb[static_cast<std::size_t> (p)] = decibels;
        peak = juce::jmax (peak, decibels);
    }

    for (auto& value : impulseResponseDb)
        value -= peak;
}

//==============================================================================
void ModuleDisplay::paint (juce::Graphics& g)
{
    auto inner = theme::drawScreen (g, getLocalBounds().toFloat(), scale).reduced (4.0f * scale, 2.0f * scale);

    switch (module)
    {
        case ModuleId::gate:       paintGate (g, inner); break;
        case ModuleId::sub:        paintSub (g, inner); break;
        case ModuleId::whammy:     paintWhammy (g, inner); break;
        case ModuleId::harmonizer: paintHarmonizer (g, inner); break;
        case ModuleId::amp:        paintAmp (g, inner); break;
        case ModuleId::cab:        paintCab (g, inner); break;
        case ModuleId::freqShift:  paintFreqShift (g, inner); break;
        case ModuleId::shimmer:    paintShimmer (g, inner); break;
        case ModuleId::poly:       paintPoly (g, inner); break;
        case ModuleId::freeze:     paintFreeze (g, inner); break;
        case ModuleId::granular:   paintGranular (g, inner); break;
    }

    // Module à l'arrêt : l'écran reste lisible, mais le signale.
    if (! processor.isModuleEnabled (module))
    {
        const auto glass = getLocalBounds().toFloat().reduced (3.0f * scale);
        g.setColour (theme::screenGlass.withAlpha (0.8f));
        g.fillRect (glass);

        const auto label = glass.withSizeKeepingCentre (juce::jmin (glass.getWidth() - 20.0f * scale, 200.0f * scale), 30.0f * scale);
        g.setColour (theme::seam);
        g.fillPath (theme::chamfered (label, 4.0f * scale));
        g.setColour (theme::hairline.brighter (0.3f));
        g.strokePath (theme::chamfered (label, 4.0f * scale), juce::PathStrokeType (1.0f * scale));
        g.setColour (theme::textDim);
        g.setFont (theme::labelFont (13.0f * scale));
        g.drawText (utf8 ("MODULE À L'ARRÊT"), label, juce::Justification::centred, false);
    }
}

//==============================================================================
void ModuleDisplay::paintResponse (juce::Graphics& g, juce::Rectangle<float> area, const std::vector<float>& decibels, float range)
{
    // Graduations : 0 dB, ±range/2, fréquences de décade
    const auto yFor = [&] (float db) { return area.getCentreY() - juce::jlimit (-range, range, db) / range * area.getHeight() * 0.5f; };

    for (const auto hz : { 100.0, 1000.0, 10000.0 })
    {
        const auto x = xForFrequency (area, hz);
        g.setColour (theme::screenGrid);
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        screenText (g, hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                    juce::Rectangle<float> (x + 2.0f * scale, area.getBottom() - 12.0f * scale, 30.0f * scale, 12.0f * scale), 9.0f * scale, faint);
    }

    for (const auto db : { -range * 0.5f, 0.0f, range * 0.5f })
    {
        g.setColour (std::abs (db) < 0.01f ? theme::screenGrid.brighter (0.6f) : theme::screenGrid);
        g.drawHorizontalLine (juce::roundToInt (yFor (db)), area.getX(), area.getRight());
        screenText (g, signedValue (db, 0), juce::Rectangle<float> (area.getX() + 2.0f * scale, yFor (db) - 12.0f * scale, 40.0f * scale, 11.0f * scale),
                    9.0f * scale, faint);
    }

    if (decibels.empty())
        return;

    juce::Path curve;

    for (std::size_t i = 0; i < decibels.size(); ++i)
    {
        const auto x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (decibels.size() - 1);

        if (i == 0)
            curve.startNewSubPath (x, yFor (decibels[i]));
        else
            curve.lineTo (x, yFor (decibels[i]));
    }

    glowPath (g, curve, primary, scale);
}

void ModuleDisplay::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> area, float markerHz)
{
    constexpr float floorDb = -84.0f;
    const auto rate = processor.getScope().getSampleRate();
    const auto bins = spectrumDb.size();

    for (const auto hz : { 100.0, 1000.0, 10000.0 })
    {
        const auto x = xForFrequency (area, hz);
        g.setColour (theme::screenGrid);
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        screenText (g, hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                    juce::Rectangle<float> (x + 2.0f * scale, area.getBottom() - 12.0f * scale, 30.0f * scale, 12.0f * scale), 9.0f * scale, faint);
    }

    for (int db = -24; db > floorDb; db -= 24)
    {
        const auto y = area.getY() + area.getHeight() * static_cast<float> (db) / floorDb;
        g.setColour (theme::screenGrid);
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }

    if (markerHz > 0.0f && markerHz < static_cast<float> (maxHz))
    {
        const auto x = xForFrequency (area, juce::jmax (static_cast<double> (markerHz), minHz));
        dashedLine (g, { x, area.getY(), x, area.getBottom() }, theme::amber.withAlpha (0.7f), scale);
    }

    // Une colonne par pixel : le maximum des cases FFT qui y tombent.
    juce::Path fill, outline;
    const auto columns = juce::jmax (2, juce::roundToInt (area.getWidth() / scale));

    for (int c = 0; c < columns; ++c)
    {
        const auto fromHz = frequencyForProportion (static_cast<double> (c) / columns);
        const auto toHz = frequencyForProportion (static_cast<double> (c + 1) / columns);
        const auto fromBin = juce::jlimit<std::size_t> (1, bins - 1, static_cast<std::size_t> (fromHz * 2.0 * static_cast<double> (bins) / rate));
        const auto toBin = juce::jlimit (fromBin, bins - 1, static_cast<std::size_t> (toHz * 2.0 * static_cast<double> (bins) / rate));
        auto level = -120.0f;

        for (auto bin = fromBin; bin <= toBin; ++bin)
            level = juce::jmax (level, spectrumDb[bin]);

        const auto x = area.getX() + area.getWidth() * static_cast<float> (c) / static_cast<float> (columns - 1);
        const auto y = area.getY() + area.getHeight() * juce::jlimit (0.0f, 1.0f, level / floorDb);

        if (c == 0)
        {
            outline.startNewSubPath (x, y);
            fill.startNewSubPath (x, area.getBottom());
        }
        else
        {
            outline.lineTo (x, y);
        }

        fill.lineTo (x, y);
    }

    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();

    g.setColour (primary.withAlpha (0.12f));
    g.fillPath (fill);
    glowPath (g, outline, primary, scale);
}

//==============================================================================
void ModuleDisplay::paintGate (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("DÉTECTEUR · SEUILS D'OUVERTURE ET DE FERMETURE"), area, scale);

    constexpr float minDb = -90.0f, maxDb = 0.0f;
    const auto& telemetry = processor.getTelemetry();
    const auto threshold = parameter (params::id::gateThreshold);
    const auto level = juce::jmax (minDb, dsp::gainToDecibels (telemetry.gateLevel.load()));
    const auto cutting = telemetry.gateCutting.load() > 0.5f;

    auto bar = area.withSizeKeepingCentre (area.getWidth() - 8.0f * scale, 26.0f * scale).translated (0.0f, -10.0f * scale);
    const auto xFor = [&] (float db) { return bar.getX() + bar.getWidth() * (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb); };

    // Échelle en dB
    for (auto db = minDb; db <= maxDb; db += 10.0f)
    {
        const auto x = xFor (db);
        g.setColour (faint);
        g.drawVerticalLine (juce::roundToInt (x), bar.getBottom() + 2.0f * scale, bar.getBottom() + 7.0f * scale);
        screenText (g, juce::String (juce::roundToInt (db)), juce::Rectangle<float> (x - 15.0f * scale, bar.getBottom() + 8.0f * scale, 30.0f * scale, 12.0f * scale),
                    9.0f * scale, faint, juce::Justification::centred);
    }

    g.setColour (theme::screenGrid);
    g.fillRect (bar);
    g.setColour ((cutting ? theme::alert : primary).withAlpha (0.85f));
    g.fillRect (bar.withRight (xFor (level)));

    // Seuils : ouverture (plein) et fermeture, 6 dB plus bas (pointillés)
    const auto openX = xFor (threshold), closeX = xFor (threshold - 6.0f);
    g.setColour (theme::amber);
    g.fillRect (openX - 1.0f * scale, bar.getY() - 10.0f * scale, 2.0f * scale, bar.getHeight() + 10.0f * scale);
    dashedLine (g, { closeX, bar.getY() - 10.0f * scale, closeX, bar.getBottom() }, theme::amber.withAlpha (0.7f), scale);

    screenText (g, utf8 ("OUVRE ") + juce::String (threshold, 0), juce::Rectangle<float> (openX + 3.0f * scale, bar.getY() - 22.0f * scale, 90.0f * scale, 12.0f * scale),
                9.5f * scale, theme::amber);
    screenText (g, utf8 ("FERME ") + juce::String (threshold - 6.0f, 0), juce::Rectangle<float> (closeX - 93.0f * scale, bar.getY() - 22.0f * scale, 90.0f * scale, 12.0f * scale),
                9.5f * scale, theme::amber.withAlpha (0.7f), juce::Justification::centredRight);

    // État et réglage de relâche, en bas
    auto bottom = area.removeFromBottom (22.0f * scale);
    theme::drawLed (g, bottom.removeFromLeft (16.0f * scale).withSizeKeepingCentre (12.0f * scale, 12.0f * scale),
                    theme::alert, telemetry.gateCutting.load(), scale);
    bottom.removeFromLeft (6.0f * scale);
    screenText (g, cutting ? "COUPE" : "PASSE", bottom, 12.0f * scale, cutting ? theme::alert : primary);
    screenText (g, utf8 ("RELÂCHE ") + juce::String (juce::roundToInt (parameter (params::id::gateRelease))) + " ms", bottom, 10.0f * scale, secondary,
                juce::Justification::centredRight);
}

void ModuleDisplay::paintSub (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("SPECTRE DE SORTIE · VOIX"), area, scale);

    auto bars = area.removeFromRight (area.getWidth() * 0.34f);
    area.removeFromRight (6.0f * scale);
    paintSpectrum (g, area, 0.0f);

    // Niveaux des voix réglées
    const std::array<std::pair<const char*, float>, 3> voices {{
        { "-2", parameter (params::id::subDown2) }, { "-1", parameter (params::id::subDown1) }, { "+1", parameter (params::id::subUp) }
    }};

    auto labels = bars.removeFromBottom (14.0f * scale);
    const auto width = bars.getWidth() / 3.0f;

    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        auto column = bars.withX (bars.getX() + width * static_cast<float> (i)).withWidth (width).reduced (5.0f * scale, 0.0f);
        g.setColour (theme::screenGrid);
        g.fillRect (column);
        g.setColour (primary.withAlpha (0.85f));
        g.fillRect (column.withTop (column.getBottom() - column.getHeight() * voices[i].second * 0.01f));
        screenText (g, juce::String (voices[i].first) + " OCT", labels.withX (column.getX() - 4.0f * scale).withWidth (column.getWidth() + 8.0f * scale),
                    9.0f * scale, secondary, juce::Justification::centred);
    }
}

void ModuleDisplay::paintWhammy (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("TRANSPOSITION EN COURS · CIBLE"), area, scale);

    const auto current = processor.getTelemetry().whammySemitones.load();
    const auto target = parameter (params::id::whInterval);

    // Lecture en grand
    auto readout = area.removeFromTop (area.getHeight() * 0.42f);
    theme::drawSegments (g, signedValue (current, 1), readout.withTrimmedRight (40.0f * scale), readout.getHeight() * 0.8f,
                         primary, juce::Justification::centredRight, true);
    screenText (g, "dt", readout.removeFromRight (36.0f * scale).withTrimmedTop (readout.getHeight() * 0.4f), 12.0f * scale, secondary);

    // Échelle de -24 à +24 demi-tons, octaves marquées
    auto scaleArea = area.removeFromTop (46.0f * scale).reduced (8.0f * scale, 0.0f);
    const auto xFor = [&] (float st) { return scaleArea.getX() + scaleArea.getWidth() * (juce::jlimit (-24.0f, 24.0f, st) + 24.0f) / 48.0f; };
    const auto axisY = scaleArea.getCentreY();

    g.setColour (faint);
    g.drawHorizontalLine (juce::roundToInt (axisY), scaleArea.getX(), scaleArea.getRight());

    for (int st = -24; st <= 24; ++st)
    {
        const auto isOctave = st % 12 == 0;
        g.setColour (isOctave ? secondary : faint);
        g.drawVerticalLine (juce::roundToInt (xFor (static_cast<float> (st))), axisY - (isOctave ? 7.0f : 3.0f) * scale, axisY);

        if (isOctave)
            screenText (g, st == 0 ? "0" : signedValue (static_cast<float> (st / 12), 0) + " oct",
                        juce::Rectangle<float> (xFor (static_cast<float> (st)) - 24.0f * scale, axisY + 3.0f * scale, 48.0f * scale, 12.0f * scale),
                        9.0f * scale, secondary, juce::Justification::centred);
    }

    // Cible (consigne) et aiguille (mesure)
    juce::Path triangle;
    const auto tx = xFor (target);
    triangle.addTriangle (tx - 5.0f * scale, scaleArea.getY(), tx + 5.0f * scale, scaleArea.getY(), tx, scaleArea.getY() + 8.0f * scale);
    g.setColour (theme::amber);
    g.fillPath (triangle);

    const auto nx = xFor (current);
    g.setColour (primary.withAlpha (0.25f));
    g.fillRect (nx - 3.0f * scale, axisY - 14.0f * scale, 6.0f * scale, 14.0f * scale);
    g.setColour (primary);
    g.fillRect (nx - 1.0f * scale, axisY - 14.0f * scale, 2.0f * scale, 14.0f * scale);

    if (auto* mode = processor.getParameters().getParameter (params::id::whMode))
        screenText (g, utf8 ("MODE ") + mode->getCurrentValueAsText().toUpperCase(), area.removeFromBottom (16.0f * scale), 10.0f * scale, secondary);
}

void ModuleDisplay::paintHarmonizer (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("SUIVI DE HAUTEUR · VOIX"), area, scale);

    const auto& telemetry = processor.getTelemetry();
    const auto note = telemetry.harmonyNote.load();
    auto top = area.removeFromTop (area.getHeight() * 0.48f);

    auto noteArea = top.removeFromLeft (top.getWidth() * 0.5f);

    if (note >= 0)
    {
        g.setColour (primary);
        g.setFont (theme::titleFont (noteArea.getHeight() * 0.72f));
        g.drawText (noteName (note), noteArea, juce::Justification::centredLeft, false);
    }
    else
    {
        theme::drawSegments (g, "--", noteArea, noteArea.getHeight() * 0.5f, faint, juce::Justification::centredLeft, true);
    }

    auto keyArea = top.reduced (0.0f, top.getHeight() * 0.2f);

    for (const auto* id : { params::id::hmKey, params::id::hmScale })
        if (auto* parameterObject = processor.getParameters().getParameter (id))
            screenText (g, parameterObject->getCurrentValueAsText().toUpperCase(), keyArea.removeFromTop (keyArea.getHeight() * 0.5f), 10.5f * scale,
                        id == params::id::hmKey ? theme::amber : secondary, juce::Justification::centredRight);

    screenText (g, note >= 0 ? utf8 ("NOTE SUIVIE") : utf8 ("JOUE UNE NOTE ISOLÉE"), area.removeFromTop (14.0f * scale), 9.5f * scale, secondary);
    area.removeFromTop (4.0f * scale);

    const std::array<std::tuple<const char*, const char*, int>, 2> voices {{
        { "VOIX 1", params::id::hmVoice1, telemetry.harmonyShift1.load() },
        { "VOIX 2", params::id::hmVoice2, telemetry.harmonyShift2.load() }
    }};

    for (const auto& [name, id, shift] : voices)
    {
        auto line = area.removeFromTop (juce::jmin (22.0f * scale, area.getHeight() * 0.5f));
        const auto steps = params::harmonyVoiceSteps (juce::roundToInt (parameter (id)));
        auto* choice = processor.getParameters().getParameter (id);
        const auto interval = choice != nullptr ? choice->getCurrentValueAsText() : juce::String();

        screenText (g, name, line.removeFromLeft (58.0f * scale), 11.0f * scale, secondary);

        if (! steps.has_value())
        {
            screenText (g, utf8 ("—"), line, 11.0f * scale, faint);
            continue;
        }

        screenText (g, interval, line.removeFromLeft (line.getWidth() * 0.45f), 11.0f * scale, theme::amber);
        screenText (g, note >= 0 ? noteName (note + shift) + "  (" + signedValue (static_cast<float> (shift), 0) + " dt)" : utf8 ("—"),
                    line, 11.0f * scale, note >= 0 ? primary : faint, juce::Justification::centredRight);
    }
}

void ModuleDisplay::paintAmp (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("ÉGALISEUR · RÉPONSE CALCULÉE"), area, scale);

    auto footer = area.removeFromBottom (16.0f * scale);
    const auto status = processor.getAmpStatus();
    screenText (g, status.fileName.isEmpty() ? utf8 ("AUCUNE CAPTURE : AMPLI TRANSPARENT")
                                             : utf8 ("CAPTURE ") + juce::File::createFileWithoutCheckingPath (status.fileName).getFileNameWithoutExtension().toUpperCase(),
                footer, 9.5f * scale, status.fileName.isEmpty() ? secondary : primary);

    // Vraie réponse des trois filtres de l'égaliseur, mêmes formules que le DSP.
    constexpr double rate = 48000.0;
    const auto filters = dsp::ToneStack::coefficientsFor (rate, parameter (params::id::ampBass), parameter (params::id::ampMid), parameter (params::id::ampTreble));
    std::vector<float> curve (160);

    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const auto hz = frequencyForProportion (static_cast<double> (i) / static_cast<double> (curve.size() - 1));
        double total = 0.0;

        for (const auto& filter : filters)
            total += filter.magnitudeDecibels (hz, rate);

        curve[i] = static_cast<float> (total);
    }

    paintResponse (g, area.withTrimmedBottom (4.0f * scale), curve, 15.0f);
}

void ModuleDisplay::paintCab (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto hasImpulse = ! impulseResponseDb.empty();
    caption (g, hasImpulse ? utf8 ("RÉPONSE DE L'IR CHARGÉE") : utf8 ("RÉPONSE DU CAB SIMULÉ"), area, scale);

    auto footer = area.removeFromBottom (16.0f * scale);

    if (hasImpulse)
    {
        screenText (g, juce::String (impulseResponseSeconds * 1000.0, 0) + " ms · " + juce::String (impulseResponseRate / 1000.0, 1) + " kHz",
                    footer, 9.5f * scale, primary);

        // Normalisée à 0 dB au maximum : on centre l'affichage sur -18 dB.
        std::vector<float> centred (impulseResponseDb);

        for (auto& value : centred)
            value += 18.0f;

        paintResponse (g, area.withTrimmedBottom (4.0f * scale), centred, 21.0f);
        return;
    }

    screenText (g, utf8 ("4×12 PAR FILTRES · AUCUNE IR CHARGÉE"), footer, 9.5f * scale, secondary);

    constexpr double rate = 48000.0;
    const auto filters = dsp::FallbackCabinet::coefficientsFor (rate);
    std::vector<float> curve (160);

    for (std::size_t i = 0; i < curve.size(); ++i)
    {
        const auto hz = frequencyForProportion (static_cast<double> (i) / static_cast<double> (curve.size() - 1));
        double total = 0.0;

        for (const auto& filter : filters)
            total += filter.magnitudeDecibels (hz, rate);

        curve[i] = static_cast<float> (total) + 9.0f;
    }

    paintResponse (g, area.withTrimmedBottom (4.0f * scale), curve, 21.0f);
}

void ModuleDisplay::paintFreqShift (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto shift = parameter (params::id::fsShift);
    caption (g, utf8 ("SPECTRE DE SORTIE · DÉCALAGE ") + signedValue (shift, std::abs (shift) < 100.0f ? 1 : 0) + " Hz", area, scale);

    auto footer = area.removeFromBottom (16.0f * scale);
    screenText (g, utf8 ("RETOUR ") + percent (parameter (params::id::fsFeedback)), footer, 9.5f * scale, secondary);
    paintSpectrum (g, area.withTrimmedBottom (4.0f * scale), std::abs (shift));
}

void ModuleDisplay::paintShimmer (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("DÉCROISSANCE · GÉNÉRATIONS (dt)"), area, scale);

    const auto decay = parameter (params::id::shDecay);
    const auto amount = parameter (params::id::shAmount) * 0.01f;
    const auto tone = parameter (params::id::shTone);
    const auto interval = params::shimmerIntervalSemitones (juce::roundToInt (parameter (params::id::shInterval)));

    auto generations = area.removeFromRight (area.getWidth() * 0.4f);
    area.removeFromRight (8.0f * scale);

    // Décroissance : graves au temps réglé, aigus (au-dessus de TONALITÉ) deux fois plus vite.
    auto graph = area.withTrimmedBottom (14.0f * scale);
    const auto span = decay * 1.25f;
    const auto xFor = [&] (float seconds) { return graph.getX() + graph.getWidth() * seconds / span; };
    const auto yFor = [&] (float db) { return graph.getY() + graph.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 66.0f); };

    g.setColour (theme::screenGrid);

    for (const auto db : { -20.0f, -40.0f, -60.0f })
        g.drawHorizontalLine (juce::roundToInt (yFor (db)), graph.getX(), graph.getRight());

    const auto highDecay = juce::jmax (0.25f, decay * static_cast<float> (dsp::ShimmerReverb::highDecayRatio));
    dashedLine (g, { xFor (0.0f), yFor (0.0f), xFor (juce::jmin (span, highDecay * 1.1f)), yFor (-60.0f * juce::jmin (span, highDecay * 1.1f) / highDecay) },
                secondary, scale);

    juce::Path lows;
    lows.startNewSubPath (xFor (0.0f), yFor (0.0f));
    lows.lineTo (xFor (span), yFor (-60.0f * span / decay));
    glowPath (g, lows, primary, scale);

    g.setColour (theme::amber);
    g.drawVerticalLine (juce::roundToInt (xFor (decay)), yFor (-60.0f) - 6.0f * scale, yFor (-60.0f) + 6.0f * scale);
    screenText (g, "T60 " + juce::String (decay, decay < 10.0f ? 1 : 0) + " s", area.removeFromBottom (14.0f * scale), 9.5f * scale, theme::amber);
    screenText (g, utf8 ("aigus > ") + juce::String (tone / 1000.0f, 1) + " kHz", juce::Rectangle<float> (graph.getRight() - 110.0f * scale, graph.getY(), 110.0f * scale, 12.0f * scale),
                9.0f * scale, secondary, juce::Justification::centredRight);

    // Générations : énergie gardée à chaque tour de boucle (calibrage exact du module).
    const auto energy = dsp::ShimmerReverb::energyPerGenerationAtMaximum * amount * amount;
    auto labels = generations.removeFromBottom (14.0f * scale);
    constexpr int count = 5;
    const auto width = generations.getWidth() / count;

    for (int k = 0; k < count; ++k)
    {
        const auto levelDb = k == 0 ? 0.0f : 10.0f * std::log10 (juce::jmax (1.0e-6f, std::pow (energy, static_cast<float> (k))));
        auto column = generations.withX (generations.getX() + width * static_cast<float> (k)).withWidth (width).reduced (3.0f * scale, 0.0f);
        const auto height = column.getHeight() * juce::jlimit (0.0f, 1.0f, 1.0f + levelDb / 40.0f);

        g.setColour (theme::screenGrid);
        g.fillRect (column);
        g.setColour ((k == 0 ? secondary : primary).withAlpha (0.85f));
        g.fillRect (column.withTop (column.getBottom() - height));

        // Transposition cumulée de chaque génération, en demi-tons
        const auto semitones = interval * static_cast<float> (k);
        screenText (g, k == 0 ? "REV" : signedValue (semitones, 0),
                    labels.withX (column.getX() - 4.0f * scale).withWidth (column.getWidth() + 8.0f * scale), 8.5f * scale, secondary, juce::Justification::centred);
    }
}

void ModuleDisplay::paintPoly (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("SPECTRE DE SORTIE · VOIX"), area, scale);

    auto field = area.removeFromRight (area.getWidth() * 0.34f);
    area.removeFromRight (10.0f * scale);
    paintSpectrum (g, area, 0.0f);

    // Champ stéréo : voix 1 à gauche, voix 2 à droite, écartées selon LARGEUR ; hauteur = intervalle.
    const auto width = parameter (params::id::poWidth) * 0.01f;
    auto labels = field.removeFromBottom (14.0f * scale);
    const auto xFor = [&] (float pan) { return field.getCentreX() + pan * field.getWidth() * 0.42f; };
    const auto yFor = [&] (float st) { return field.getCentreY() - juce::jlimit (-24.0f, 24.0f, st) / 24.0f * field.getHeight() * 0.45f; };

    g.setColour (theme::screenGrid);
    g.drawRect (field);
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), field.getX(), field.getRight());
    g.drawVerticalLine (juce::roundToInt (field.getCentreX()), field.getY(), field.getBottom());

    screenText (g, "G", labels.removeFromLeft (labels.getWidth() * 0.5f), 9.0f * scale, secondary);
    screenText (g, "D", labels, 9.0f * scale, secondary, juce::Justification::centredRight);

    const std::array<std::tuple<float, float, float>, 2> voices {{
        { -width, parameter (params::id::poInterval1), parameter (params::id::poLevel1) * 0.01f },
        { width, parameter (params::id::poInterval2), parameter (params::id::poLevel2) * 0.01f }
    }};

    for (const auto& [pan, interval, level] : voices)
    {
        const auto centre = juce::Point<float> (xFor (pan), yFor (interval));
        const auto size = (5.0f + 9.0f * level) * scale;

        if (level <= 0.0f)
        {
            g.setColour (faint);
            g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (centre), 1.0f * scale);
            continue;
        }

        g.setColour (primary.withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (size * 1.8f, size * 1.8f).withCentre (centre));
        g.setColour (primary);
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (centre));
        screenText (g, signedValue (interval, 0), juce::Rectangle<float> (centre.x - 30.0f * scale, centre.y + size * 0.6f, 60.0f * scale, 11.0f * scale),
                    9.0f * scale, primary, juce::Justification::centred);
    }
}

void ModuleDisplay::paintFreeze (juce::Graphics& g, juce::Rectangle<float> area)
{
    caption (g, utf8 ("NAPPE · ENVELOPPE"), area, scale);

    const auto held = processor.getTelemetry().freezeHeld.load();
    auto top = area.removeFromTop (area.getHeight() * 0.45f);
    theme::drawSegments (g, held ? "FIGEE" : "PRETE", top.reduced (4.0f * scale, 0.0f), top.getHeight() * 0.7f,
                         held ? theme::amber : faint, juce::Justification::centredLeft, true);

    // Enveloppe : montée (ATTAQUE), tenue, descente (RELÂCHE), à l'échelle des temps réglés.
    const auto attack = parameter (params::id::frAttack);
    const auto release = parameter (params::id::frRelease);
    auto graph = area.withTrimmedBottom (16.0f * scale).reduced (4.0f * scale, 4.0f * scale);
    const auto total = attack + release + juce::jmax (attack, release);
    const auto xFor = [&] (float ms) { return graph.getX() + graph.getWidth() * ms / total; };

    juce::Path envelope;
    envelope.startNewSubPath (graph.getX(), graph.getBottom());
    envelope.lineTo (xFor (attack), graph.getY());
    envelope.lineTo (xFor (total - release), graph.getY());
    envelope.lineTo (graph.getRight(), graph.getBottom());

    g.setColour (theme::screenGrid);
    g.drawHorizontalLine (juce::roundToInt (graph.getBottom()), graph.getX(), graph.getRight());
    glowPath (g, envelope, held ? theme::amber : primary, scale);

    auto footer = area.removeFromBottom (14.0f * scale);
    screenText (g, utf8 ("ATTAQUE ") + juce::String (juce::roundToInt (attack)) + " ms", footer, 9.5f * scale, secondary);
    screenText (g, utf8 ("RELÂCHE ") + juce::String (juce::roundToInt (release)) + " ms", footer, 9.5f * scale, secondary, juce::Justification::centredRight);
}

void ModuleDisplay::paintGranular (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto stutter = parameter (params::id::grMode) > 0.5f;
    const auto tempo = static_cast<float> (processor.getTempo());
    caption (g, stutter ? utf8 ("STUTTER · GRILLE DU TEMPO") : utf8 ("NUAGE · FENÊTRE DE GRAIN"), area, scale);

    auto footer = area.removeFromBottom (14.0f * scale);
    screenText (g, juce::String (juce::roundToInt (tempo)) + " BPM", footer, 9.5f * scale, theme::amber);

    auto graph = area.withTrimmedBottom (4.0f * scale).reduced (4.0f * scale, 6.0f * scale);

    if (stutter)
    {
        // Une mesure de 4 temps, découpée selon DIVISION ; la tranche répétée en surbrillance.
        const auto division = static_cast<float> (params::granularDivisionBeats (juce::roundToInt (parameter (params::id::grDivision))));
        const auto repeats = juce::roundToInt (parameter (params::id::grRepeats));
        const auto slices = juce::jmax (1, juce::roundToInt (4.0f / division));

        for (int beat = 0; beat <= 4; ++beat)
        {
            const auto x = graph.getX() + graph.getWidth() * static_cast<float> (beat) / 4.0f;
            g.setColour (secondary);
            g.drawVerticalLine (juce::roundToInt (x), graph.getY(), graph.getBottom());
        }

        for (int slice = 0; slice < slices; ++slice)
        {
            const auto x0 = graph.getX() + graph.getWidth() * static_cast<float> (slice) / static_cast<float> (slices);
            const auto x1 = graph.getX() + graph.getWidth() * static_cast<float> (slice + 1) / static_cast<float> (slices);
            const auto isRepeat = slice % repeats != 0;
            g.setColour (isRepeat ? primary.withAlpha (0.35f) : primary.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x0 + 1.0f * scale, graph.getCentreY() - 10.0f * scale, x1 - x0 - 2.0f * scale, 20.0f * scale));
        }

        if (auto* choice = processor.getParameters().getParameter (params::id::grDivision))
            screenText (g, choice->getCurrentValueAsText() + utf8 (" · ×") + juce::String (repeats), footer, 9.5f * scale, secondary,
                        juce::Justification::centredRight);
        return;
    }

    // Nuage : forme du grain (fenêtre de Hann) à l'échelle de 1 s de mémoire.
    const auto size = parameter (params::id::grSize);
    const auto grainWidth = graph.getWidth() * juce::jlimit (0.02f, 1.0f, size / 1000.0f);
    const auto grain = graph.withSizeKeepingCentre (grainWidth, graph.getHeight());

    g.setColour (theme::screenGrid);
    g.drawHorizontalLine (juce::roundToInt (graph.getBottom()), graph.getX(), graph.getRight());

    for (int tick = 0; tick <= 10; ++tick)
        g.drawVerticalLine (juce::roundToInt (graph.getX() + graph.getWidth() * static_cast<float> (tick) / 10.0f), graph.getBottom() - 4.0f * scale, graph.getBottom());

    juce::Path window;

    for (int i = 0; i <= 64; ++i)
    {
        const auto t = static_cast<float> (i) / 64.0f;
        const auto value = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * t);
        const auto point = juce::Point<float> (grain.getX() + grain.getWidth() * t, grain.getBottom() - grain.getHeight() * value);

        if (i == 0)
            window.startNewSubPath (point);
        else
            window.lineTo (point);
    }

    glowPath (g, window, primary, scale);
    screenText (g, juce::String (juce::roundToInt (size)) + utf8 (" ms · ") + juce::String (parameter (params::id::grDensity), 1) + " /s",
                footer, 9.5f * scale, secondary, juce::Justification::centredRight);
}

} // namespace subdamp::gui
