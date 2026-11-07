#include "Panels.h"

#include "LoudnessMeter.h"
#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
const char* getPanelName (PanelKind kind) noexcept
{
    switch (kind)
    {
        case PanelKind::loudness: return "LUFS";
        case PanelKind::spectrum: return "Spectrum";
        case PanelKind::waveform: return "Waveform";
        case PanelKind::levels: return "Levels";
        case PanelKind::vu: return "VU";
        case PanelKind::goniometer: return "Goniometer";
        case PanelKind::scope: return "Scope";
        case PanelKind::spectrogram: return "Spectrogram";
        case PanelKind::tuner: return "Tuner";
        case PanelKind::constellation: return "Constellation";
        case PanelKind::mandala: return "Mandala";
        case PanelKind::signalMap: return "Signal Map";
        case PanelKind::pulsar: return "Pulsar";
        case PanelKind::orb: return "Orb";
        case PanelKind::attractor: return "Attractor";
    }
    return "";
}

namespace
{
constexpr float labelMarginLeft = 34.0f, labelMarginBottom = 16.0f;

void drawLabel (juce::Graphics& g, const juce::String& text, float x, float y, float w, juce::Justification j)
{
    g.drawText (text, juce::Rectangle<float> (x, y, w, 14.0f), j);
}

// ------------------------------------------------------------------ Spectrum

class SpectrumPanel final : public VisualizerPanel
{
public:
    explicit SpectrumPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Spectrum display");
        setDescription ("Frequency spectrum of the master signal, logarithmic frequency axis");
        left.resize (fftSize);
        right.resize (fftSize);
        mono.resize (fftSize);
    }

    void update() override
    {
        const double rate = owner.getCurrentSampleRate();
        if (rate != preparedRate)
        {
            analyzer.prepare (rate, fftOrder, numBands);
            display.assign ((size_t) numBands, SpectrumAnalyzer::floorDb);
            preparedRate = rate;
        }

        if (owner.getHistory().copyLatest (left.data(), right.data(), (int) fftSize) < (int) fftSize)
            return;

        for (size_t i = 0; i < fftSize; ++i)
            mono[i] = 0.5f * (left[i] + right[i]);

        analyzer.analyse (mono.data());
        const auto& db = analyzer.getBandsDb();

        constexpr float fallPerTick = 1.2f;  // ~36 dB/s at 30 Hz
        for (size_t i = 0; i < display.size(); ++i)
            display[i] = std::max (db[i], display[i] - fallPerTick);

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto plot = getLocalBounds().toFloat();
        plot.removeFromLeft (labelMarginLeft);
        plot.removeFromTop (8.0f);
        plot.removeFromBottom (labelMarginBottom);
        if (plot.isEmpty() || display.empty()) return;

        g.setFont (theme::bodyFont (theme::fontSize::note));

        // Level grid (dB)
        for (int db = 0; db >= (int) minDb; db -= 20)
        {
            const float y = plot.getY() + plot.getHeight() * (float) (-db) / -minDb;
            g.setColour (palette.grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (palette.displayDim);
            drawLabel (g, juce::String (db), 0.0f, y - 7.0f, labelMarginLeft - 4.0f, juce::Justification::centredRight);
        }

        // Frequency grid
        const double fMin = analyzer.getBandFrequency (0), fMax = analyzer.getBandFrequency (numBands - 1);
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const float x = plot.getX() + plot.getWidth() * (float) (std::log (f / fMin) / std::log (fMax / fMin));
            g.setColour (palette.grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (palette.displayDim);
            drawLabel (g, f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                       x - 20.0f, plot.getBottom() + 2.0f, 40.0f, juce::Justification::centred);
        }

        juce::Path line, fill;
        const float step = plot.getWidth() / (float) (numBands - 1);
        for (int i = 0; i < numBands; ++i)
        {
            const float level = juce::jlimit (0.0f, 1.0f, (display[(size_t) i] - minDb) / -minDb);
            const float x = plot.getX() + step * (float) i, y = plot.getBottom() - plot.getHeight() * level;
            if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, plot.getBottom()); fill.lineTo (x, y); }
            else { line.lineTo (x, y); fill.lineTo (x, y); }
        }
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();

        g.setColour (palette.traceLeft.withAlpha (0.28f));
        g.fillPath (fill);
        g.setColour (palette.traceLeft);
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

private:
    static constexpr int fftOrder = 13, numBands = 160;
    static constexpr size_t fftSize = 1u << fftOrder;
    static constexpr float minDb = -90.0f;

    SpectrumAnalyzer analyzer;
    std::vector<float> left, right, mono, display;
    double preparedRate = 0.0;
};

// ------------------------------------------------------------------ Waveform

class WaveformPanel final : public VisualizerPanel
{
public:
    explicit WaveformPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Waveform display");
        setDescription ("Recent waveform of the left and right channels");
        left.resize (frames);
        right.resize (frames);
    }

    void update() override
    {
        got = owner.getHistory().copyLatest (left.data(), right.data(), (int) frames);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto plot = getLocalBounds().toFloat();
        plot.removeFromLeft (labelMarginLeft);
        plot.removeFromTop (8.0f);
        plot.removeFromBottom (8.0f);
        if (plot.isEmpty()) return;

        g.setFont (theme::bodyFont (theme::fontSize::note));
        const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f;

        for (float v : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
        {
            const float y = mid - half * v;
            g.setColour (palette.grid);
            g.drawHorizontalLine ((int) std::round (juce::jlimit (plot.getY(), plot.getBottom() - 1.0f, y)), plot.getX(), plot.getRight());
            g.setColour (palette.displayDim);
            drawLabel (g, v == 0.0f ? "0" : juce::String (v, 1), 0.0f, juce::jlimit (plot.getY(), plot.getBottom() - 14.0f, y - 7.0f),
                       labelMarginLeft - 4.0f, juce::Justification::centredRight);
        }

        if (got <= 0) return;

        const int columns = (int) plot.getWidth();
        auto drawChannel = [&] (const std::vector<float>& data, juce::Colour colour)
        {
            g.setColour (colour.withAlpha (0.75f));
            for (int x = 0; x < columns; ++x)
            {
                const int i0 = (int) ((long long) x * got / columns), i1 = std::max (i0 + 1, (int) ((long long) (x + 1) * got / columns));
                float lo = 1.0f, hi = -1.0f;
                for (int i = i0; i < i1; ++i)
                {
                    lo = std::min (lo, data[(size_t) i]);
                    hi = std::max (hi, data[(size_t) i]);
                }
                lo = juce::jlimit (-1.0f, 1.0f, lo);
                hi = juce::jlimit (-1.0f, 1.0f, hi);
                const float px = plot.getX() + (float) x;
                g.drawVerticalLine ((int) px, mid - half * hi, std::max (mid - half * lo, mid - half * hi + 1.0f));
            }
        };

        drawChannel (left, palette.traceLeft);
        drawChannel (right, palette.traceRight);
    }

private:
    static constexpr size_t frames = 1u << 15;  // ~0.68 s at 48 kHz

    std::vector<float> left, right;
    int got = 0;
};

// ------------------------------------------------------------------ LUFS

class LoudnessPanel final : public VisualizerPanel
{
public:
    explicit LoudnessPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Loudness display");
        setDescription ("Momentary, short-term and integrated loudness in LUFS, loudness range and true peak");
    }

    void update() override { repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto& r = owner.getLatestReadings();
        auto area = getLocalBounds().reduced (4);
        if (area.getWidth() < 60 || area.getHeight() < 30) return;

        auto fmt = [] (double v, int d = 1) { return v <= silenceLufs + 0.5 ? juce::String ("-inf") : juce::String (v, d); };

        const bool room = area.getHeight() >= 110;
        auto cells = room ? area.removeFromBottom (44) : juce::Rectangle<int>();

        g.setColour (palette.displayDim);
        g.setFont (bodyFont (fontSize::note));
        g.drawText ("Momentary LUFS", area.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (palette.displayText);
        g.setFont (monoFont (juce::jlimit (20.0f, 96.0f, std::min ((float) area.getHeight() * 0.75f, (float) area.getWidth() * 0.22f))));
        g.drawText (fmt (r.momentary), area, juce::Justification::centredLeft);

        if (! room) return;

        const juce::String names[] = { "Short-term", "Integrated", "Range", "True peak" };
        const juce::String values[] = { fmt (r.shortTerm) + " LUFS", fmt (r.integrated) + " LUFS",
                                        juce::String (r.loudnessRange, 1) + " LU", fmt (r.truePeakDb) + " dBTP" };
        const int w = cells.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto cell = cells.removeFromLeft (w);
            g.setColour (palette.displayDim);
            g.setFont (bodyFont (fontSize::note));
            g.drawText (names[i], cell.removeFromTop (16), juce::Justification::centredLeft);
            g.setColour (palette.displayText);
            g.setFont (monoFont (fontSize::body));
            g.drawText (values[i], cell, juce::Justification::centredLeft, true);
        }
    }
};
} // namespace

std::unique_ptr<VisualizerPanel> makePanel (PanelKind kind, VintageVisualProcessor& p)
{
    switch (kind)
    {
        case PanelKind::spectrum: return std::make_unique<SpectrumPanel> (p);
        case PanelKind::waveform: return std::make_unique<WaveformPanel> (p);
        case PanelKind::levels:
        case PanelKind::vu:
        case PanelKind::goniometer:
        case PanelKind::scope: return makeLevelPanel (kind, p);
        case PanelKind::spectrogram:
        case PanelKind::tuner: return makeAnalysisPanel (kind, p);
        case PanelKind::constellation:
        case PanelKind::mandala:
        case PanelKind::signalMap:
        case PanelKind::pulsar:
        case PanelKind::orb:
        case PanelKind::attractor: return makeGenerativePanel (kind, p);
        case PanelKind::loudness: return std::make_unique<LoudnessPanel> (p);
    }
    return std::make_unique<LoudnessPanel> (p);
}
} // namespace vintagevisual
