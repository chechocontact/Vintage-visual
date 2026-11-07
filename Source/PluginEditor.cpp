#include "PluginEditor.h"

namespace vintagevisual
{
VintageVisualEditor::VintageVisualEditor (VintageVisualProcessor& p)
    : juce::AudioProcessorEditor (p), owner (p), view (p, MeterView::Mode::embedded)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (ui::minWidth, ui::minHeight, ui::maxWidth, ui::maxHeight);
    setSize (owner.uiWidth.load(), owner.uiHeight.load());
}

void VintageVisualEditor::resized()
{
    view.setBounds (getLocalBounds());
    owner.uiWidth.store (getWidth());
    owner.uiHeight.store (getHeight());
}
} // namespace vintagevisual
