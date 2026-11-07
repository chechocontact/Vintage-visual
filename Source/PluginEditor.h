#pragma once

#include "MeterView.h"
#include "PluginProcessor.h"

namespace vintagevisual
{
// The editor the DAW shows: a resizable host for the embedded MeterView.
class VintageVisualEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VintageVisualEditor (VintageVisualProcessor&);

    void resized() override;
    void refreshNow() { view.refreshNow(); }
    MeterView& getView() noexcept { return view; }

private:
    VintageVisualProcessor& owner;
    MeterView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageVisualEditor)
};
} // namespace vintagevisual
