#include "LevelTracker.h"
#include "Panels.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
namespace
{
constexpr float sqrt2 = 1.41421356f;

// Feeds a LevelTracker with the audio written since the last call.
class AudioFollower
{
public:
    void pull (VintageVisualProcessor& owner, LevelTracker& tracker)
    {
        const double rate = owner.getCurrentSampleRate();
        const auto& history = owner.getHistory();

        if (rate != preparedRate)
        {
            tracker.prepare (rate);
            preparedRate = rate;
            cursor = history.getTotalFramesWritten();  // ignore audio from before
        }

        for (int guard = 0; guard < 8; ++guard)
        {
            const int n = history.readNew (cursor, left, right, chunk);
            if (n <= 0) break;
            tracker.process (left, right, n);
            if (n < chunk) break;
        }
    }

private:
    static constexpr int chunk = 8192;
    float left[chunk] {}, right[chunk] {};
    std::uint64_t cursor = 0;
    double preparedRate = 0.0;
};

void drawText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, juce::Justification j, juce::Colour c)
{
    g.setColour (c);
    g.setFont (theme::bodyFont (theme::fontSize::note));
    g.drawText (t, r, j);
}

juce::String formatDb (float db)
{
    return db <= LevelTracker::floorDb + 0.5f ? juce::String ("-inf") : juce::String (db, 1);
}

// ------------------------------------------------------------------ Levels

class LevelsPanel final : public VisualizerPanel
{
public:
    explicit LevelsPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Levels display");
        setDescription ("Peak and RMS level of the left and right channels in dBFS");
    }

    void update() override
    {
        follower.pull (owner, tracker);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        area.removeFromLeft (34.0f);
        area.removeFromTop (22.0f);
        area.removeFromBottom (18.0f);
        if (area.getHeight() < 20.0f) return;

        auto yFor = [&] (float db) { return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb); };

        for (int db = 0; db >= (int) minDb; db -= 10)
        {
            g.setColour (palette.grid);
            g.drawHorizontalLine ((int) yFor ((float) db), area.getX(), area.getRight());
            drawText (g, juce::String (db), { 0.0f, yFor ((float) db) - 7.0f, 30.0f, 14.0f }, juce::Justification::centredRight, palette.displayDim);
        }

        const float barWidth = std::min (70.0f, area.getWidth() * 0.22f), gap = barWidth * 0.5f;
        const float totalWidth = barWidth * 2.0f + gap;
        const float startX = area.getCentreX() - totalWidth * 0.5f;
        const ChannelLevels* ch[2] = { &tracker.getLeft(), &tracker.getRight() };
        const juce::Colour colours[2] = { palette.traceLeft, palette.traceRight };

        for (int c = 0; c < 2; ++c)
        {
            const float x = startX + (float) c * (barWidth + gap);
            const auto& l = *ch[c];

            g.setColour (colours[c].withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (x, yFor (l.rmsDb), barWidth, area.getBottom() - yFor (l.rmsDb)));
            g.setColour (colours[c]);
            g.fillRect (juce::Rectangle<float> (x, yFor (l.peakDb) - 1.0f, barWidth, 3.0f));
            g.setColour (palette.displayText);
            g.fillRect (juce::Rectangle<float> (x - 3.0f, yFor (l.holdDb) - 1.0f, barWidth + 6.0f, 2.0f));

            drawText (g, c == 0 ? "L" : "R", { x, area.getBottom() + 2.0f, barWidth, 14.0f }, juce::Justification::centred, palette.displayDim);
            drawText (g, formatDb (l.holdDb), { x - 10.0f, area.getY() - 20.0f, barWidth + 20.0f, 14.0f }, juce::Justification::centred, palette.displayText);
        }
    }

private:
    static constexpr float minDb = -60.0f;
    AudioFollower follower;
    LevelTracker tracker;
};

// ------------------------------------------------------------------ VU

class VuPanel final : public VisualizerPanel
{
public:
    explicit VuPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("VU meters");
        setDescription ("Analog style VU meters for the left and right channels, 0 VU equals minus 18 dBFS RMS");
    }

    void update() override
    {
        follower.pull (owner, tracker);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        const float w = area.getWidth() * 0.5f;
        drawMeter (g, area.withWidth (w).reduced (6.0f), tracker.getLeft().vuDb, "L");
        drawMeter (g, area.withTrimmedLeft (w).reduced (6.0f), tracker.getRight().vuDb, "R");
    }

private:
    // Classic VU scale: close to linear in amplitude, -20 at the left stop and +3 at the right.
    static float fractionFor (float vu)
    {
        const float lo = std::pow (10.0f, -20.0f / 20.0f), hi = std::pow (10.0f, 3.0f / 20.0f);
        return (std::pow (10.0f, vu / 20.0f) - lo) / (hi - lo);
    }

    void drawMeter (juce::Graphics& g, juce::Rectangle<float> r, float vu, const juce::String& label) const
    {
        if (r.getWidth() < 60.0f || r.getHeight() < 50.0f) return;

        g.setColour (palette.separator);
        g.drawRoundedRectangle (r, theme::cornerRadius, 1.0f);

        const float radius = std::min (r.getWidth() * 0.5f - 10.0f, r.getHeight() * 0.92f);
        const juce::Point<float> pivot (r.getCentreX(), r.getBottom() - 10.0f);
        const float span = 50.0f * juce::MathConstants<float>::pi / 180.0f;

        auto pointAt = [&] (float fraction, float rad)
        {
            const float a = -span + 2.0f * span * fraction;
            return juce::Point<float> (pivot.x + rad * std::sin (a), pivot.y - rad * std::cos (a));
        };

        const float redStart = fractionFor (0.0f);
        juce::Path red;
        red.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, -span + 2.0f * span * redStart, span, true);
        g.setColour (palette.warning.brighter (0.7f));
        g.strokePath (red, juce::PathStrokeType (3.0f));

        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, -span, -span + 2.0f * span * redStart, true);
        g.setColour (palette.displayDim);
        g.strokePath (arc, juce::PathStrokeType (1.5f));

        g.setFont (theme::bodyFont (theme::fontSize::note));
        for (int mark : { -20, -10, -7, -5, -3, 0, 3 })
        {
            const float f = fractionFor ((float) mark);
            g.setColour (mark >= 0 ? palette.warning.brighter (0.7f) : palette.displayDim);
            g.drawLine ({ pointAt (f, radius - 8.0f), pointAt (f, radius + 3.0f) }, 1.5f);
            const auto lp = pointAt (f, radius - 20.0f);
            g.drawText (mark > 0 ? "+" + juce::String (mark) : juce::String (mark), juce::Rectangle<float> (lp.x - 14.0f, lp.y - 7.0f, 28.0f, 14.0f),
                        juce::Justification::centred);
        }

        const float f = juce::jlimit (-0.03f, 1.05f, vu <= LevelTracker::floorDb + 1.0f ? -0.03f : fractionFor (vu));
        g.setColour (palette.displayText);
        g.drawLine ({ pivot, pointAt (f, radius - 4.0f) }, 2.0f);
        g.fillEllipse (pivot.x - 5.0f, pivot.y - 5.0f, 10.0f, 10.0f);

        drawText (g, "VU " + label, { r.getX() + 8.0f, r.getBottom() - 20.0f, 60.0f, 14.0f }, juce::Justification::centredLeft, palette.displayDim);
    }

    AudioFollower follower;
    LevelTracker tracker;
};

// ------------------------------------------------------------------ Goniometer

class GoniometerPanel final : public VisualizerPanel
{
public:
    explicit GoniometerPanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Goniometer");
        setDescription ("Stereo image: mono signals draw a vertical line. The bar below shows phase correlation");
    }

    void update() override
    {
        follower.pull (owner, tracker);
        got = owner.getHistory().copyLatest (left, right, frames);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto meter = area.removeFromBottom (46.0f);
        const float radius = std::min (area.getWidth(), area.getHeight()) * 0.5f - 4.0f;
        if (radius < 20.0f) return;
        const auto c = area.getCentre();

        g.setColour (palette.grid);
        g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
        g.drawLine (c.x, c.y - radius, c.x, c.y + radius);
        g.drawLine (c.x - radius, c.y, c.x + radius, c.y);
        const float d = radius / sqrt2;
        g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d);
        g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d);
        drawText (g, "L", { c.x - d - 18.0f, c.y - d - 14.0f, 16.0f, 14.0f }, juce::Justification::centred, palette.displayDim);
        drawText (g, "R", { c.x + d + 2.0f, c.y - d - 14.0f, 16.0f, 14.0f }, juce::Justification::centred, palette.displayDim);

        g.setColour (palette.traceLeft.withAlpha (0.55f));
        for (int i = 0; i < got; ++i)
        {
            const float x = c.x + (left[i] - right[i]) * 0.5f * radius;
            const float y = c.y - (left[i] + right[i]) * 0.5f * radius;
            g.fillRect (x, y, 1.6f, 1.6f);
        }

        // Correlation bar: -1 .. +1
        const float corr = tracker.getCorrelation();
        auto bar = meter.withTrimmedTop (20.0f).reduced (30.0f, 8.0f);
        g.setColour (palette.grid);
        g.fillRect (bar);
        g.setColour (palette.displayDim);
        g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 3.0f, bar.getBottom() + 3.0f);
        g.setColour (corr >= 0.0f ? palette.traceLeft : palette.traceRight);
        const float x = bar.getCentreX() + corr * bar.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float> (std::min (x, bar.getCentreX()), bar.getY(), std::abs (x - bar.getCentreX()), bar.getHeight()));
        drawText (g, "-1", { meter.getX(), bar.getY() - 3.0f, 28.0f, 14.0f }, juce::Justification::centredRight, palette.displayDim);
        drawText (g, "+1", { bar.getRight() + 2.0f, bar.getY() - 3.0f, 28.0f, 14.0f }, juce::Justification::centredLeft, palette.displayDim);
        drawText (g, "Correlation " + juce::String (corr, 2), { meter.getX(), meter.getY() + 2.0f, meter.getWidth(), 14.0f },
                  juce::Justification::centred, palette.displayText);
    }

private:
    static constexpr int frames = 1024;
    float left[frames] {}, right[frames] {};
    int got = 0;
    AudioFollower follower;
    LevelTracker tracker;
};

// ------------------------------------------------------------------ Scope

class ScopePanel final : public VisualizerPanel
{
public:
    explicit ScopePanel (VintageVisualProcessor& p) : VisualizerPanel (p)
    {
        setTitle ("Oscilloscope");
        setDescription ("Triggered view of the left and right waveforms over a few milliseconds");
    }

    void update() override
    {
        const int n = owner.getHistory().copyLatest (left, right, available);
        start = 0;
        shown = 0;
        if (n >= window * 2)
        {
            // Latest rising zero crossing of L+R that still leaves a full window after it.
            int t = n - window;
            for (int i = n - window - 1; i > 0; --i)
                if (left[i - 1] + right[i - 1] < 0.0f && left[i] + right[i] >= 0.0f) { t = i; break; }
            start = t;
            shown = window;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        area.removeFromLeft (34.0f);
        area.removeFromTop (8.0f);
        area.removeFromBottom (18.0f);
        if (area.isEmpty()) return;

        const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
        for (float v : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
        {
            g.setColour (palette.grid);
            g.drawHorizontalLine ((int) juce::jlimit (area.getY(), area.getBottom() - 1.0f, mid - half * v), area.getX(), area.getRight());
            drawText (g, v == 0.0f ? "0" : juce::String (v, 1), { 0.0f, juce::jlimit (area.getY(), area.getBottom() - 14.0f, mid - half * v - 7.0f), 30.0f, 14.0f },
                      juce::Justification::centredRight, palette.displayDim);
        }

        const double rate = std::max (1.0, owner.getCurrentSampleRate());
        const double totalMs = 1000.0 * window / rate;
        for (int ms = 0; ms <= (int) totalMs; ms += 5)
        {
            const float x = area.getX() + area.getWidth() * (float) (ms / totalMs);
            g.setColour (palette.grid);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            drawText (g, juce::String (ms) + " ms", { x - 20.0f, area.getBottom() + 2.0f, 40.0f, 14.0f }, juce::Justification::centred, palette.displayDim);
        }

        if (shown <= 0) return;

        auto trace = [&] (const float* data, juce::Colour colour)
        {
            juce::Path path;
            for (int i = 0; i < shown; ++i)
            {
                const float x = area.getX() + area.getWidth() * (float) i / (float) (shown - 1);
                const float y = mid - half * juce::jlimit (-1.0f, 1.0f, data[start + i]);
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (1.4f));
        };

        trace (left, palette.traceLeft);
        trace (right, palette.traceRight.withAlpha (0.8f));
    }

private:
    static constexpr int available = 8192, window = 1024;
    float left[available] {}, right[available] {};
    int start = 0, shown = 0;
};
} // namespace

std::unique_ptr<VisualizerPanel> makeLevelPanel (PanelKind kind, VintageVisualProcessor& p)
{
    switch (kind)
    {
        case PanelKind::levels: return std::make_unique<LevelsPanel> (p);
        case PanelKind::vu: return std::make_unique<VuPanel> (p);
        case PanelKind::goniometer: return std::make_unique<GoniometerPanel> (p);
        case PanelKind::scope: return std::make_unique<ScopePanel> (p);
        default: break;
    }
    return nullptr;
}
} // namespace vintagevisual
