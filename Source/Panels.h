#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace vintagevisual
{
enum class PanelKind
{
    loudness = 0,
    spectrum,
    waveform,
    levels,
    vu,
    goniometer,
    scope,
    spectrogram,
    tuner,
    constellation,
    mandala,
    signalMap,
    pulsar,
    orb,
    attractor
};

static_assert (numPanels == 15, "update numPanels in PluginProcessor.h when adding a PanelKind");
const char* getPanelName (PanelKind) noexcept;

// A visualiser module. update() runs on the message thread at ~30 Hz and reads audio from the
// processor's AudioHistory; paint() only draws what update() prepared.
class VisualizerPanel : public juce::Component
{
public:
    explicit VisualizerPanel (VintageVisualProcessor& p) : owner (p) { setInterceptsMouseClicks (false, false); }

    void setPalette (const theme::Palette& newPalette)
    {
        palette = newPalette;
        paletteChanged();
        repaint();
    }

    virtual void update() = 0;

protected:
    virtual void paletteChanged() {}

    VintageVisualProcessor& owner;
    theme::Palette palette { skins[0].colors };
};

// Level-based panels (Levels, VU, Goniometer, Scope), implemented in LevelPanels.cpp.
std::unique_ptr<VisualizerPanel> makeLevelPanel (PanelKind, VintageVisualProcessor&);

// Analysis panels (Spectrogram, Tuner), implemented in AnalysisPanels.cpp.
std::unique_ptr<VisualizerPanel> makeAnalysisPanel (PanelKind, VintageVisualProcessor&);

// Generative visualisers (Constellation, Mandala, Signal Map, Pulsar, Orb, Attractor), in GenerativePanels.cpp.
std::unique_ptr<VisualizerPanel> makeGenerativePanel (PanelKind, VintageVisualProcessor&);

// Never returns nullptr for a valid kind.
std::unique_ptr<VisualizerPanel> makePanel (PanelKind, VintageVisualProcessor&);
} // namespace vintagevisual
