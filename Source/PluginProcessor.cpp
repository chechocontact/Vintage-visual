#include "PluginProcessor.h"
#include "FloatingWindow.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace vintagevisual
{
VintageVisualProcessor::VintageVisualProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Defaults: LUFS first, then the classic meters; FULL shows spectrogram, loudness, waveform, stereo and levels.
    constexpr int normalDefaults[maxSlots] = { 0, 1, 2, 3, 5 };
    constexpr int fullDefaults[maxSlots] = { 7, 0, 2, 5, 3 };
    for (int i = 0; i < maxSlots; ++i)
    {
        slotPanels[(size_t) i].store (normalDefaults[i]);
        fullSlotPanels[(size_t) i].store (fullDefaults[i]);
    }
}

VintageVisualProcessor::~VintageVisualProcessor()
{
    if (floatingWindow == nullptr) return;

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        floatingWindow.reset();
    else
    {
        // Hosts normally destroy plug-ins on the message thread. If not, hide now and delete later.
        auto* window = floatingWindow.release();
        juce::MessageManager::callAsync ([window] { delete window; });
    }
}

void VintageVisualProcessor::showFloatingMeter()
{
    if (floatingWindow != nullptr)
    {
        floatingWindow->toFront (true);
        return;
    }

    floatingWindow = std::make_unique<FloatingWindow> (*this, [this]
    {
        // Never delete the window from inside its own callback.
        juce::MessageManager::callAsync ([this, alive = juce::Component::SafePointer<juce::Component> (floatingWindow.get())]
        {
            if (alive != nullptr) hideFloatingMeter();
        });
    });
}

void VintageVisualProcessor::hideFloatingMeter()
{
    floatingWindow.reset();
}

void VintageVisualProcessor::prepareToPlay (double sampleRate, int)
{
    // Allocation happens here, never in processBlock.
    meter.prepare (sampleRate, 2);
    history.prepare (1 << 17);  // ~2.7 s at 48 kHz; readers may use up to half
    sampleRateForUi.store (sampleRate);
    resetRequested.store (false);
}

bool VintageVisualProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void VintageVisualProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0) return;

    if (resetRequested.exchange (false))
        meter.reset();

    // The buffer is left untouched: the plugin only observes.
    const float* channels[2] = { buffer.getReadPointer (0), buffer.getReadPointer (numChannels > 1 ? 1 : 0) };

    meter.process (channels, numSamples);
    history.push (channels[0], channels[1], numSamples);
    readingsMailbox.push (meter.getReadings());
}

juce::AudioProcessorEditor* VintageVisualProcessor::createEditor()
{
    return new VintageVisualEditor (*this);
}

void VintageVisualProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("VintageVisual");
    xml.setAttribute ("version", stateVersion);
    xml.setAttribute ("skin", skinIndex.load());
    xml.setAttribute ("target", (double) targetLufs.load());
    xml.setAttribute ("layout", layoutPreset.load());
    for (int i = 0; i < maxSlots; ++i)
    {
        xml.setAttribute ("slot" + juce::String (i), slotPanels[(size_t) i].load());
        xml.setAttribute ("fslot" + juce::String (i), fullSlotPanels[(size_t) i].load());
    }
    xml.setAttribute ("pinned", pinned.load());
    xml.setAttribute ("width", uiWidth.load());
    xml.setAttribute ("height", uiHeight.load());
    copyXmlToBinary (xml, destData);
}

void VintageVisualProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("VintageVisual")) return;
    if (xml->getIntAttribute ("version", 1) > stateVersion) return;  // written by a newer build: ignore

    skinIndex.store (std::clamp (xml->getIntAttribute ("skin", 0), 0, numSkins - 1));

    const double target = xml->getDoubleAttribute ("target", -14.0);
    if (std::isfinite (target))
        targetLufs.store ((float) std::clamp (target, -40.0, 0.0));

    layoutPreset.store (std::clamp (xml->getIntAttribute ("layout", 0), 0, numUserLayouts - 1));
    for (int i = 0; i < maxSlots; ++i)
    {
        const int legacy = i == 0 ? xml->getIntAttribute ("panel", slotPanels[0].load()) : slotPanels[(size_t) i].load();  // older states stored one "panel"
        slotPanels[(size_t) i].store (std::clamp (xml->getIntAttribute ("slot" + juce::String (i), legacy), 0, numPanels - 1));
        fullSlotPanels[(size_t) i].store (std::clamp (xml->getIntAttribute ("fslot" + juce::String (i), fullSlotPanels[(size_t) i].load()), 0, numPanels - 1));
    }
    pinned.store (xml->getBoolAttribute ("pinned", true));
    uiWidth.store (std::clamp (xml->getIntAttribute ("width", ui::defaultWidth), ui::minWidth, ui::maxWidth));
    uiHeight.store (std::clamp (xml->getIntAttribute ("height", ui::defaultHeight), ui::minHeight, ui::maxHeight));
}
} // namespace vintagevisual

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vintagevisual::VintageVisualProcessor();
}
