#include "LookAndFeel.h"

namespace vintagevisual
{
VintageLookAndFeel::VintageLookAndFeel()
{
    applyPalette (palette);
}

void VintageLookAndFeel::applyPalette (const theme::Palette& p)
{
    palette = p;

    setColour (juce::TextButton::textColourOffId, p.textPrimary);
    setColour (juce::TextButton::textColourOnId, p.textOnAccent);
    setColour (juce::ComboBox::textColourId, p.textPrimary);
    setColour (juce::ComboBox::arrowColourId, p.textPrimary);
    setColour (juce::ComboBox::backgroundColourId, p.control);
    setColour (juce::ComboBox::outlineColourId, p.controlBorder);
    setColour (juce::PopupMenu::backgroundColourId, p.elevated);
    setColour (juce::PopupMenu::textColourId, p.textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, p.textOnAccent);
    setColour (juce::Label::textColourId, p.textPrimary);
    setColour (juce::TooltipWindow::backgroundColourId, p.elevated);
    setColour (juce::TooltipWindow::textColourId, p.textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, p.controlBorder);
}

void VintageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    auto fill = down ? palette.controlPressed : (highlighted ? palette.controlHover : palette.control);
    if (button.getToggleState()) fill = palette.accent;
    if (! button.isEnabled()) fill = fill.withAlpha (0.5f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, theme::cornerRadius);
    g.setColour (palette.controlBorder);
    g.drawRoundedRectangle (bounds, theme::cornerRadius, 1.0f);

    if (button.hasKeyboardFocus (true))
    {
        g.setColour (palette.focus);
        g.drawRoundedRectangle (bounds.reduced (1.0f), theme::cornerRadius, theme::focusRingWidth);
    }
}

void VintageLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isDown, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

    g.setColour (isDown ? palette.controlPressed : (box.isMouseOver (true) ? palette.controlHover : palette.control));
    g.fillRoundedRectangle (bounds, theme::cornerRadius);
    g.setColour (palette.controlBorder);
    g.drawRoundedRectangle (bounds, theme::cornerRadius, 1.0f);

    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (palette.textPrimary);
    g.fillPath (arrow);

    if (box.hasKeyboardFocus (true))
    {
        g.setColour (palette.focus);
        g.drawRoundedRectangle (bounds.reduced (1.0f), theme::cornerRadius, theme::focusRingWidth);
    }
}

juce::Font VintageLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return theme::bodyFont (theme::fontSize::label); }
juce::Font VintageLookAndFeel::getComboBoxFont (juce::ComboBox&) { return theme::bodyFont (theme::fontSize::label); }
juce::Font VintageLookAndFeel::getPopupMenuFont() { return theme::bodyFont (theme::fontSize::label); }
} // namespace vintagevisual
