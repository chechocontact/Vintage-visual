#include "DualSpectrum.h"
#include "Panels.h"
#include "Pitch.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vintagevisual
{
namespace
{
void drawText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c, float size = theme::fontSize::note)
{
    g.setColour (c);
    g.setFont (theme::bodyFont (size));
    g.drawText (t, r, j);
}

// ------------------------------------------------------------------ Spectrogram

// Two analysis resolutions: a long FFT for detailed bass, a short FFT for the rest.
class SpectrogramPanel final : public VisualizerPanel
{
public:
    explicit SpectrogramPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Spectrogram");
        setDescription ("Scrolling spectrogram. Frequency is vertical on a logarithmic scale and time runs to the right");
        levels.assign ((size_t) (columns * numBands), 0);
        image = juce::Image (juce::Image::RGB, columns, numBands, true);  // opaque: skips alpha blending when drawn
        paletteChanged();
    }

    void update() override
    {
        const double rate = owner.getCurrentSampleRate();
        if (rate != preparedRate)
        {
            dual.prepare (rate, numBands);
            const auto size = (size_t) dual.getLongSize();
            left.resize (size);
            right.resize (size);
            mono.resize (size);
            preparedRate = rate;
        }

        if (owner.getHistory().copyLatest (left.data(), right.data(), dual.getLongSize()) < dual.getLongSize())
            return;

        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (left[i] + right[i]);

        dual.analyse (mono.data());
        const auto& db = dual.getBandsDb();

        auto* column = &levels[(size_t) head * (size_t) numBands];
        for (int b = 0; b < numBands; ++b)
            column[b] = toLevel (db[(size_t) b]);

        paintColumn (head);
        head = (head + 1) % columns;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto plot = getLocalBounds().toFloat();
        plot.removeFromLeft (34.0f);
        plot.removeFromTop (8.0f);
        plot.removeFromBottom (16.0f);
        if (plot.isEmpty() || preparedRate == 0.0) return;

        // Smooth scaling costs about 9 ms on a 1600x900 window; nearest-neighbour is ~5x cheaper and looks fine
        // once the plot is large, because each source pixel is already several screen pixels wide.
        g.setImageResamplingQuality (plot.getWidth() * plot.getHeight() > 250000.0f ? juce::Graphics::lowResamplingQuality
                                                                                    : juce::Graphics::mediumResamplingQuality);
        const float colWidth = plot.getWidth() / (float) columns;

        // Oldest data starts at `head`; the ring is drawn in two parts so time always runs left to right.
        const int oldPart = columns - head;
        g.drawImage (image, (int) plot.getX(), (int) plot.getY(), (int) std::round (colWidth * (float) oldPart), (int) plot.getHeight(),
                     head, 0, oldPart, numBands);
        if (head > 0)
            g.drawImage (image, (int) std::round (plot.getX() + colWidth * (float) oldPart), (int) plot.getY(), (int) std::round (colWidth * (float) head),
                         (int) plot.getHeight(), 0, 0, head, numBands);

        const double fMin = dual.getBandFrequency (0), fMax = dual.getBandFrequency (numBands - 1);
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const float y = plot.getBottom() - plot.getHeight() * (float) (std::log (f / fMin) / std::log (fMax / fMin));
            g.setColour (palette.grid.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            drawText (g, f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), { 0.0f, y - 7.0f, 30.0f, 14.0f },
                      juce::Justification::centredRight, palette.displayDim);
        }

        drawText (g, "older", { plot.getX(), plot.getBottom() + 2.0f, 60.0f, 14.0f }, juce::Justification::centredLeft, palette.displayDim);
        drawText (g, "now", { plot.getRight() - 60.0f, plot.getBottom() + 2.0f, 60.0f, 14.0f }, juce::Justification::centredRight, palette.displayDim);
    }

private:
    static constexpr int columns = 600, numBands = 256;

    static std::uint8_t toLevel (float db) noexcept
    {
        return (std::uint8_t) juce::jlimit (0, 255, (int) std::lround ((db + 90.0f) / 85.0f * 255.0f));
    }

    void paletteChanged() override
    {
        juce::ColourGradient gradient (palette.displayBackground, 0.0f, 0.0f, juce::Colours::white, 1.0f, 0.0f, false);
        gradient.addColour (0.35, palette.displayBackground.interpolatedWith (palette.traceLeft, 0.35f));
        gradient.addColour (0.65, palette.traceLeft);
        gradient.addColour (0.85, palette.displayText);
        for (int i = 0; i < 256; ++i) lut[(size_t) i] = gradient.getColourAtPosition ((double) i / 255.0);

        for (int c = 0; c < columns; ++c) paintColumn (c);  // recolour the history
    }

    void paintColumn (int c)
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);
        const auto* column = &levels[(size_t) c * (size_t) numBands];
        for (int b = 0; b < numBands; ++b)
            data.setPixelColour (c, numBands - 1 - b, lut[column[b]]);
    }

    DualSpectrum dual;
    std::vector<float> left, right, mono;
    std::vector<std::uint8_t> levels;  // [column][band], 0..255 mapped from -90..-5 dB
    std::array<juce::Colour, 256> lut;
    juce::Image image;
    int head = 0;
    double preparedRate = 0.0;
};

// ------------------------------------------------------------------ Tuner

class TunerPanel final : public VisualizerPanel
{
public:
    explicit TunerPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Tuner");
        setDescription ("Note name and tuning error in cents for the dominant pitch. Works best on a single instrument");
    }

    void update() override
    {
        const double rate = owner.getCurrentSampleRate();
        if (rate != preparedRate)
        {
            detector.prepare (rate);
            const auto size = (size_t) detector.getRequiredSamples();
            left.resize (size);
            right.resize (size);
            mono.resize (size);
            preparedRate = rate;
        }

        if (++tick % 3 != 0) return;  // about 10 detections per second

        const int needed = detector.getRequiredSamples();
        if (owner.getHistory().copyLatest (left.data(), right.data(), needed) < needed) return;
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (left[i] + right[i]);

        const auto result = detector.detect (mono.data());
        if (result.valid)
        {
            const auto note = describeNote (result.frequencyHz);
            // Smooth the needle while the note stays the same; jump when it changes.
            cents = (haveNote && note.midi == midi) ? cents + 0.35 * (note.cents - cents) : note.cents;
            midi = note.midi;
            pitchClass = note.pitchClass;
            octave = note.octave;
            frequency = result.frequencyHz;
            haveNote = true;
            missed = 0;
        }
        else if (++missed > 5)  // half a second without a stable pitch
        {
            haveNote = false;
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        if (area.getHeight() < 60.0f) return;

        auto gauge = area.removeFromBottom (44.0f);
        auto top = area;

        const bool inTune = haveNote && std::abs (cents) <= 5.0;
        const juce::String name = haveNote ? juce::String (noteName (pitchClass)) + juce::String (octave) : juce::String ("--");

        g.setColour (inTune ? palette.traceLeft : palette.displayText);
        g.setFont (theme::monoFont (juce::jlimit (28.0f, 96.0f, top.getHeight() * 0.55f)));
        g.drawText (name, top.removeFromTop (top.getHeight() * 0.62f), juce::Justification::centred);

        juce::String detail = "Play a single note";
        if (haveNote)
        {
            const int c = (int) std::lround (cents);
            const juce::String state = inTune ? "In tune" : (cents > 0 ? "Sharp" : "Flat");
            detail = juce::String (frequency, 1) + " Hz   " + (c > 0 ? "+" : "") + juce::String (c) + " cents   " + state;
        }
        drawText (g, detail, top, juce::Justification::centred, palette.displayText, theme::fontSize::body);

        // Cents gauge: -50 .. +50
        auto bar = gauge.reduced (24.0f, 14.0f).withTrimmedTop (4.0f);
        g.setColour (palette.grid);
        g.fillRect (bar);
        const float cx = bar.getCentreX(), halfW = bar.getWidth() * 0.5f;
        g.setColour (palette.traceLeft.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (cx - halfW * 0.1f, bar.getY(), halfW * 0.2f, bar.getHeight()));  // in-tune zone, +-5 cents

        for (int c = -50; c <= 50; c += 10)
        {
            const float x = cx + halfW * (float) c / 50.0f;
            g.setColour (c == 0 ? palette.displayText : palette.displayDim);
            g.drawVerticalLine ((int) x, bar.getY() - (c % 50 == 0 ? 6.0f : 3.0f), bar.getBottom() + 3.0f);
        }
        drawText (g, "-50", { bar.getX() - 24.0f, bar.getBottom() + 2.0f, 40.0f, 12.0f }, juce::Justification::centred, palette.displayDim);
        drawText (g, "+50", { bar.getRight() - 16.0f, bar.getBottom() + 2.0f, 40.0f, 12.0f }, juce::Justification::centred, palette.displayDim);

        if (haveNote)
        {
            const float x = cx + halfW * (float) juce::jlimit (-50.0, 50.0, cents) / 50.0f;
            g.setColour (inTune ? palette.traceLeft : palette.traceRight);
            g.fillRect (juce::Rectangle<float> (x - 2.0f, bar.getY() - 6.0f, 4.0f, bar.getHeight() + 12.0f));
        }
    }

private:
    PitchDetector detector;
    std::vector<float> left, right, mono;
    double preparedRate = 0.0;
    int tick = 0, missed = 0;
    bool haveNote = false;
    int midi = 0, pitchClass = 0, octave = 0;
    double cents = 0.0, frequency = 0.0;
};
} // namespace

std::unique_ptr<VisualizerPanel> makeAnalysisPanel (PanelKind kind, VintageVisualProcessor& p)
{
    switch (kind)
    {
        case PanelKind::spectrogram: return std::make_unique<SpectrogramPanel> (p);
        case PanelKind::tuner: return std::make_unique<TunerPanel> (p);
        default: break;
    }
    return nullptr;
}
} // namespace vintagevisual
