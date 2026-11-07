#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vintagevisual
{
// The single look-and-feel for all controls. Colours come from the active skin's Palette.
class VintageLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();

    void applyPalette (const theme::Palette& newPalette);

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isDown, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

private:
    theme::Palette palette { skins[0].colors };
};
} // namespace vintagevisual
