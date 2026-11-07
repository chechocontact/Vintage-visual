#pragma once

#include "Skins.h"

#include <juce_graphics/juce_graphics.h>

// Every visual value lives here. No colours or sizes are written anywhere else in the UI code.
namespace vintagevisual::theme
{
namespace space
{
constexpr int xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32;
}

constexpr int controlHeight = 28;
constexpr float cornerRadius = 6.0f;
constexpr float focusRingWidth = 2.0f;

namespace fontSize
{
constexpr float body = 14.0f, label = 13.0f, note = 12.0f, title = 18.0f;
}

inline juce::Colour colour (std::uint32_t rgb) noexcept
{
    return juce::Colour (0xFF000000u | rgb);
}

inline juce::Font bodyFont (float size)
{
    return juce::Font (juce::FontOptions (size));
}

inline juce::Font titleFont (float size)
{
    return juce::Font (juce::FontOptions ("Georgia", size, juce::Font::italic));
}

inline juce::Font monoFont (float size)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

struct Palette
{
    explicit Palette (const SkinColors& c)
        : canvas (colour (c.canvas)), surface (colour (c.surface)), elevated (colour (c.elevated)),
          control (colour (c.control)), controlHover (colour (c.controlHover)), controlPressed (colour (c.controlPressed)),
          textPrimary (colour (c.textPrimary)), textSecondary (colour (c.textSecondary)), textOnAccent (colour (c.textOnAccent)),
          separator (colour (c.separator)), controlBorder (colour (c.controlBorder)),
          accent (colour (c.accent)), focus (colour (c.focus)), warning (colour (c.warning)),
          displayBackground (colour (c.displayBackground)), displayText (colour (c.displayText)), displayDim (colour (c.displayDim)),
          traceLeft (colour (c.traceLeft)), traceRight (colour (c.traceRight)), grid (colour (c.grid))
    {
    }

    juce::Colour canvas, surface, elevated;
    juce::Colour control, controlHover, controlPressed;
    juce::Colour textPrimary, textSecondary, textOnAccent;
    juce::Colour separator, controlBorder;
    juce::Colour accent, focus, warning;
    juce::Colour displayBackground, displayText, displayDim;
    juce::Colour traceLeft, traceRight, grid;
};
} // namespace vintagevisual::theme
