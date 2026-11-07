#include "Panels.h"
#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vintagevisual
{
namespace
{
constexpr float twoPi = juce::MathConstants<float>::twoPi;

// Shared audio features for the generative panels: 48 smoothed band levels (0..1) plus low/mid/high
// energy, overall level and spectral centroid. UI thread only.
class BandSource
{
public:
    static constexpr int order = 11, numBands = 48;

    BandSource() : level ((size_t) numBands, 0.0f) {}

    bool update (VintageVisualProcessor& owner)
    {
        const double rate = owner.getCurrentSampleRate();
        if (rate != preparedRate)
        {
            analyzer.prepare (rate, order, numBands, 30.0, 16000.0);
            const auto n = (size_t) analyzer.getFftSize();
            left.assign (n, 0.0f);
            right.assign (n, 0.0f);
            mono.assign (n, 0.0f);
            preparedRate = rate;

            lowEnd = highStart = numBands;
            for (int b = numBands - 1; b >= 0; --b)
            {
                if (analyzer.getBandFrequency (b) >= 250.0) lowEnd = b;
                if (analyzer.getBandFrequency (b) >= 3000.0) highStart = b;
            }
        }

        const int n = analyzer.getFftSize();
        if (owner.getHistory().copyLatest (left.data(), right.data(), n) < n) return false;
        for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (left[i] + right[i]);
        analyzer.analyse (mono.data());

        const auto& db = analyzer.getBandsDb();
        float sum = 0.0f, weighted = 0.0f, lo = 0.0f, mi = 0.0f, hi = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            const float v = juce::jlimit (0.0f, 1.0f, (db[(size_t) b] + 75.0f) / 55.0f);
            level[(size_t) b] = std::max (v, level[(size_t) b] * 0.88f);
            const float l = level[(size_t) b];
            sum += l;
            weighted += l * (float) b;
            (b < lowEnd ? lo : (b < highStart ? mi : hi)) += l;
        }

        low = lo / (float) std::max (1, lowEnd);
        mid = mi / (float) std::max (1, highStart - lowEnd);
        high = hi / (float) std::max (1, numBands - highStart);
        overall = juce::jlimit (0.0f, 1.0f, sum / (float) numBands * 1.6f);
        if (sum > 0.05f) centroid += 0.2f * (weighted / sum / (float) (numBands - 1) - centroid);
        return true;
    }

    std::vector<float> level;
    float low = 0.0f, mid = 0.0f, high = 0.0f, overall = 0.0f, centroid = 0.5f;

private:
    SpectrumAnalyzer analyzer;
    std::vector<float> left, right, mono;
    double preparedRate = 0.0;
    int lowEnd = numBands, highStart = numBands;
};

juce::Colour along (const theme::Palette& p, float t)
{
    return p.traceLeft.interpolatedWith (p.traceRight, juce::jlimit (0.0f, 1.0f, t));
}

// ------------------------------------------------------------------ Constellation

class ConstellationPanel final : public VisualizerPanel
{
public:
    explicit ConstellationPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Constellation");
        setDescription ("Decorative star map: each star is a frequency band that brightens with its level");
        std::uint32_t seed = 2463534242u;
        auto rnd = [&seed]
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (float) (seed & 0xFFFFFF) / (float) 0x1000000;
        };

        for (int i = 0; i < BandSource::numBands; ++i)
        {
            sx[(size_t) i] = ((float) i + 0.2f + 0.6f * rnd()) / (float) BandSource::numBands;  // left to right = low to high
            sy[(size_t) i] = 0.12f + 0.76f * rnd();
        }
    }

    void update() override
    {
        bands.update (owner);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto a = getLocalBounds().toFloat().reduced (10.0f);
        if (a.getWidth() < 40.0f || a.getHeight() < 40.0f) return;

        auto pt = [&] (int i) { return juce::Point<float> (a.getX() + sx[(size_t) i] * a.getWidth(), a.getY() + sy[(size_t) i] * a.getHeight()); };
        const int n = BandSource::numBands;

        for (int i = 0; i + 1 < n; ++i)
        {
            const float m = std::min (bands.level[(size_t) i], bands.level[(size_t) i + 1]);
            if (m < 0.18f) continue;
            g.setColour (along (palette, (float) i / (float) n).withAlpha (m * 0.7f));
            g.drawLine (juce::Line<float> (pt (i), pt (i + 1)), 1.2f);
        }

        for (int i = 0; i < n; ++i)
        {
            const float v = bands.level[(size_t) i], r = 1.5f + 6.0f * v;
            const auto c = along (palette, (float) i / (float) n);
            const auto p = pt (i);
            g.setColour (c.withAlpha (0.12f + 0.2f * v));
            g.fillEllipse (p.x - r * 2.6f, p.y - r * 2.6f, r * 5.2f, r * 5.2f);
            g.setColour (c.withAlpha (0.4f + 0.6f * v));
            g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);
        }
    }

private:
    BandSource bands;
    std::array<float, BandSource::numBands> sx {}, sy {};
};

// ------------------------------------------------------------------ Mandala

class MandalaPanel final : public VisualizerPanel
{
public:
    explicit MandalaPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Mandala");
        setDescription ("Decorative radial pattern: twelve petals shaped by the spectrum, rotating with the treble");
    }

    void update() override
    {
        bands.update (owner);
        rotation = std::fmod (rotation + 0.004f + 0.05f * bands.high, twoPi);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto a = getLocalBounds().toFloat();
        const float R = std::min (a.getWidth(), a.getHeight()) * 0.5f - 6.0f;
        if (R < 20.0f) return;
        const auto c = a.getCentre();

        constexpr int petals = 12, steps = 24;
        const float half = twoPi / (float) petals * 0.47f;
        juce::Path sector;
        sector.startNewSubPath (c);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int j = 0; j < steps; ++j)
            {
                const int jj = pass == 0 ? j : steps - 1 - j;
                const float t = (float) jj / (float) (steps - 1);
                const float ang = (pass == 0 ? 1.0f : -1.0f) * t * half;
                const float r = R * (0.12f + 0.88f * bands.level[(size_t) (jj * 2)]);
                sector.lineTo (c.x + r * std::sin (ang), c.y - r * std::cos (ang));
            }
        }
        sector.closeSubPath();

        for (int k = 0; k < petals; ++k)
        {
            const auto t = juce::AffineTransform::rotation (rotation + (float) k * twoPi / (float) petals, c.x, c.y);
            const auto colour = k % 2 == 0 ? palette.traceLeft : palette.traceRight;
            g.setColour (colour.withAlpha (0.2f));
            g.fillPath (sector, t);
            g.setColour (colour.withAlpha (0.8f));
            g.strokePath (sector, juce::PathStrokeType (1.0f), t);
        }

        const float core = R * (0.07f + 0.08f * bands.low);
        g.setColour (palette.displayText.withAlpha (0.9f));
        g.fillEllipse (c.x - core, c.y - core, core * 2.0f, core * 2.0f);
    }

private:
    BandSource bands;
    float rotation = 0.0f;
};

// ------------------------------------------------------------------ Signal Map

class SignalMapPanel final : public VisualizerPanel
{
public:
    explicit SignalMapPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Signal map");
        setDescription ("Where each frequency sits in the stereo field: left to right is pan, bottom to top is frequency");
        level.fill (0.0f);
        pan.fill (0.0f);
    }

    void update() override
    {
        const double rate = owner.getCurrentSampleRate();
        if (rate != preparedRate)
        {
            analyzerL.prepare (rate, 11, numBands, 40.0, 16000.0);
            analyzerR.prepare (rate, 11, numBands, 40.0, 16000.0);
            left.assign ((size_t) analyzerL.getFftSize(), 0.0f);
            right.assign (left.size(), 0.0f);
            preparedRate = rate;
        }

        const int n = analyzerL.getFftSize();
        if (owner.getHistory().copyLatest (left.data(), right.data(), n) < n) return;
        analyzerL.analyse (left.data());
        analyzerR.analyse (right.data());

        for (int b = 0; b < numBands; ++b)
        {
            const float dl = analyzerL.getBandsDb()[(size_t) b], dr = analyzerR.getBandsDb()[(size_t) b];
            const float al = std::pow (10.0f, dl / 20.0f), ar = std::pow (10.0f, dr / 20.0f);
            const float lv = juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (std::max (al, ar) + 1.0e-9f) + 75.0f) / 55.0f);
            const auto i = (size_t) b;
            if (lv > 0.05f) pan[i] += 0.35f * ((ar - al) / (ar + al + 1.0e-9f) - pan[i]);
            level[i] = std::max (lv, level[i] * 0.88f);
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto plot = getLocalBounds().toFloat();
        plot.removeFromLeft (34.0f);
        plot.removeFromTop (8.0f);
        plot.removeFromBottom (18.0f);
        if (plot.getWidth() < 60.0f || plot.getHeight() < 40.0f || preparedRate == 0.0) return;

        g.setColour (palette.grid);
        g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * 0.25f), plot.getY(), plot.getBottom());
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * 0.75f), plot.getY(), plot.getBottom());

        g.setFont (theme::bodyFont (theme::fontSize::note));
        g.setColour (palette.displayDim);
        g.drawText ("L", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
        g.drawText ("C", juce::Rectangle<float> (plot.getCentreX() - 15.0f, plot.getBottom() + 2.0f, 30.0f, 14.0f), juce::Justification::centred);
        g.drawText ("R", juce::Rectangle<float> (plot.getRight() - 30.0f, plot.getBottom() + 2.0f, 30.0f, 14.0f), juce::Justification::centredRight);

        const double fMin = analyzerL.getBandFrequency (0), fMax = analyzerL.getBandFrequency (numBands - 1);
        auto yFor = [&] (double f) { return plot.getBottom() - plot.getHeight() * (float) (std::log (f / fMin) / std::log (fMax / fMin)); };

        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const float y = yFor (f);
            g.setColour (palette.grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (palette.displayDim);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (0.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }

        for (int b = 0; b < numBands; ++b)
        {
            const auto i = (size_t) b;
            const float v = level[i];
            if (v < 0.04f) continue;
            const float x = plot.getCentreX() + pan[i] * plot.getWidth() * 0.5f, y = yFor (analyzerL.getBandFrequency (b));
            const float r = 2.0f + 7.0f * v;
            const auto c = along (palette, (pan[i] + 1.0f) * 0.5f);
            g.setColour (c.withAlpha (0.18f * v + 0.05f));
            g.fillEllipse (x - r * 2.0f, y - r * 2.0f, r * 4.0f, r * 4.0f);
            g.setColour (c.withAlpha (0.35f + 0.65f * v));
            g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
        }
    }

private:
    static constexpr int numBands = 40;
    SpectrumAnalyzer analyzerL, analyzerR;
    std::vector<float> left, right;
    std::array<float, numBands> level, pan;
    double preparedRate = 0.0;
};

// ------------------------------------------------------------------ Pulsar

class PulsarPanel final : public VisualizerPanel
{
public:
    explicit PulsarPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Pulsar");
        setDescription ("Decorative rings that expand from the centre on every bass hit");
    }

    void update() override
    {
        if (bands.update (owner))
        {
            if (cooldown > 0) --cooldown;
            const float flux = bands.low - previousLow;
            previousLow = bands.low;

            if (cooldown == 0 && flux > 0.07f && bands.low > 0.25f)
            {
                auto slot = std::find_if (rings.begin(), rings.end(), [] (const Ring& r) { return ! r.active; });
                if (slot == rings.end()) slot = std::max_element (rings.begin(), rings.end(), [] (const Ring& a, const Ring& b) { return a.age < b.age; });
                *slot = { true, 0, bands.low };
                cooldown = 5;
            }
        }

        for (auto& r : rings)
            if (r.active && ++r.age > maxAge) r.active = false;

        glow = std::max (bands.overall, glow * 0.9f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto a = getLocalBounds().toFloat();
        const float maxR = std::min (a.getWidth(), a.getHeight()) * 0.5f - 4.0f;
        if (maxR < 20.0f) return;
        const auto c = a.getCentre();

        for (const auto& r : rings)
        {
            if (! r.active) continue;
            const float t = (float) r.age / (float) maxAge, radius = maxR * (0.1f + 0.9f * t);
            g.setColour (along (palette, t).withAlpha ((1.0f - t) * (0.3f + 0.7f * r.strength)));
            g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.5f + 4.0f * (1.0f - t) * r.strength);
        }

        const float core = maxR * (0.05f + 0.08f * glow);
        g.setColour (palette.traceLeft.withAlpha (0.25f));
        g.fillEllipse (c.x - core * 2.2f, c.y - core * 2.2f, core * 4.4f, core * 4.4f);
        g.setColour (palette.displayText.withAlpha (0.95f));
        g.fillEllipse (c.x - core, c.y - core, core * 2.0f, core * 2.0f);
    }

private:
    struct Ring
    {
        bool active = false;
        int age = 0;
        float strength = 0.0f;
    };

    static constexpr int maxAge = 36;
    BandSource bands;
    std::array<Ring, 12> rings;
    float previousLow = 0.0f, glow = 0.0f;
    int cooldown = 0;
};

// ------------------------------------------------------------------ Orb

class OrbPanel final : public VisualizerPanel
{
public:
    explicit OrbPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Orb");
        setDescription ("Decorative glowing sphere: size follows the level, colour follows the brightness of the sound");
    }

    void update() override
    {
        bands.update (owner);
        size += 0.25f * ((0.3f + 0.5f * bands.overall) - size);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto a = getLocalBounds().toFloat();
        const float R = std::min (a.getWidth(), a.getHeight()) * 0.5f - 6.0f;
        if (R < 20.0f) return;
        const auto c = a.getCentre();
        const float r = R * size;
        const auto colour = along (palette, bands.centroid * 1.6f - 0.15f);

        juce::ColourGradient halo (colour.withAlpha (0.55f), c.x, c.y, colour.withAlpha (0.0f), c.x + r * 1.9f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - r * 1.9f, c.y - r * 1.9f, r * 3.8f, r * 3.8f);

        juce::ColourGradient body (palette.displayText.withAlpha (0.95f), c.x - r * 0.35f, c.y - r * 0.35f, colour, c.x + r * 0.8f, c.y + r * 0.8f, true);
        g.setGradientFill (body);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        for (int i = 0; i < 2; ++i)
        {
            const float rr = r * (1.15f + 0.35f * (float) i + 0.5f * bands.high);
            g.setColour (colour.withAlpha ((0.45f - 0.2f * (float) i) * (0.3f + bands.high)));
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 1.5f);
        }
    }

private:
    BandSource bands;
    float size = 0.3f;
};

// ------------------------------------------------------------------ Attractor

class AttractorPanel final : public VisualizerPanel
{
public:
    explicit AttractorPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Attractor");
        setDescription ("Decorative Lorenz attractor whose shape and speed follow the music");
    }

    void update() override
    {
        bands.update (owner);

        const double rho = 28.0 + 14.0 * bands.mid, sigma = 10.0, beta = 8.0 / 3.0, dt = 0.004;
        const int steps = 6 + (int) (24.0f * bands.overall);

        for (int s = 0; s < steps; ++s)
        {
            const double dx = sigma * (y - x), dy = x * (rho - z) - y, dz = x * y - beta * z;
            x += dx * dt;
            y += dy * dt;
            z += dz * dt;
            if (! std::isfinite (x + y + z) || std::abs (x) > 400.0 || std::abs (y) > 400.0 || std::abs (z) > 400.0) { x = y = z = 1.0; }
            if (s % 2 == 0)
            {
                trail[(size_t) head] = { (float) x, (float) z };
                head = (head + 1) % trailLength;
                count = std::min (count + 1, trailLength);
            }
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto a = getLocalBounds().toFloat().reduced (10.0f);
        if (a.getWidth() < 40.0f || a.getHeight() < 40.0f || count < 10) return;

        auto map = [&] (juce::Point<float> p)
        {
            return juce::Point<float> (a.getCentreX() + p.x / 28.0f * a.getWidth() * 0.5f, a.getBottom() - p.y / 56.0f * a.getHeight());
        };
        auto at = [&] (int k) { return map (trail[(size_t) ((head - count + k + trailLength * 2) % trailLength)]); };

        constexpr int segments = 5;
        for (int s = 0; s < segments; ++s)
        {
            const int from = count * s / segments, to = std::min (count - 1, count * (s + 1) / segments);
            juce::Path path;
            path.startNewSubPath (at (from));
            for (int k = from + 1; k <= to; ++k) path.lineTo (at (k));
            g.setColour (palette.traceLeft.withAlpha (0.1f + 0.22f * (float) s));
            g.strokePath (path, juce::PathStrokeType (1.0f + 0.4f * (float) s));
        }

        const auto tip = at (count - 1);
        g.setColour (palette.traceRight);
        g.fillEllipse (tip.x - 3.5f, tip.y - 3.5f, 7.0f, 7.0f);
    }

private:
    static constexpr int trailLength = 600;
    BandSource bands;
    std::array<juce::Point<float>, trailLength> trail {};
    double x = 1.0, y = 1.0, z = 1.0;
    int head = 0, count = 0;
};
} // namespace

std::unique_ptr<VisualizerPanel> makeGenerativePanel (PanelKind kind, VintageVisualProcessor& p)
{
    switch (kind)
    {
        case PanelKind::constellation: return std::make_unique<ConstellationPanel> (p);
        case PanelKind::mandala: return std::make_unique<MandalaPanel> (p);
        case PanelKind::signalMap: return std::make_unique<SignalMapPanel> (p);
        case PanelKind::pulsar: return std::make_unique<PulsarPanel> (p);
        case PanelKind::orb: return std::make_unique<OrbPanel> (p);
        case PanelKind::attractor: return std::make_unique<AttractorPanel> (p);
        default: break;
    }
    return nullptr;
}
} // namespace vintagevisual
