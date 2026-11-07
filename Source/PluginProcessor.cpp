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

    // DOCK defaults: LUFS, spectrum and levels first, then the rest.
    constexpr int dockDefaults[maxDockSlots] = { 0, 1, 3, 2, 7, 5, 4, 6 };
    for (int i = 0; i < maxDockSlots; ++i)
        dockSlotPanels[(size_t) i].store (dockDefaults[i]);

    loadUserConfig();  // a new instance starts from the user's saved configuration, if there is one
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

std::unique_ptr<juce::XmlElement> VintageVisualProcessor::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("VintageVisual");
    xml->setAttribute ("version", stateVersion);
    xml->setAttribute ("skin", skinIndex.load());
    xml->setAttribute ("target", (double) targetLufs.load());
    xml->setAttribute ("layout", layoutPreset.load());
    for (int i = 0; i < maxSlots; ++i)
    {
        xml->setAttribute ("slot" + juce::String (i), slotPanels[(size_t) i].load());
        xml->setAttribute ("fslot" + juce::String (i), fullSlotPanels[(size_t) i].load());
    }
    xml->setAttribute ("dockCount", dockCount.load());
    for (int i = 0; i < maxDockSlots; ++i)
        xml->setAttribute ("dslot" + juce::String (i), dockSlotPanels[(size_t) i].load());
    xml->setAttribute ("dockHeight", dockHeight.load());
    xml->setAttribute ("pinned", pinned.load());
    xml->setAttribute ("width", uiWidth.load());
    xml->setAttribute ("height", uiHeight.load());
    return xml;
}

void VintageVisualProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    copyXmlToBinary (*createStateXml(), destData);
}

void VintageVisualProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName ("VintageVisual")) return;
    if (xml.getIntAttribute ("version", 1) > stateVersion) return;  // written by a newer build: ignore

    skinIndex.store (std::clamp (xml.getIntAttribute ("skin", 0), 0, numSkins - 1));

    const double target = xml.getDoubleAttribute ("target", -14.0);
    if (std::isfinite (target))
        targetLufs.store ((float) std::clamp (target, -40.0, 0.0));

    layoutPreset.store (std::clamp (xml.getIntAttribute ("layout", 0), 0, numUserLayouts - 1));
    for (int i = 0; i < maxSlots; ++i)
    {
        const int legacy = i == 0 ? xml.getIntAttribute ("panel", slotPanels[0].load()) : slotPanels[(size_t) i].load();  // older states stored one "panel"
        slotPanels[(size_t) i].store (std::clamp (xml.getIntAttribute ("slot" + juce::String (i), legacy), 0, numPanels - 1));
        fullSlotPanels[(size_t) i].store (std::clamp (xml.getIntAttribute ("fslot" + juce::String (i), fullSlotPanels[(size_t) i].load()), 0, numPanels - 1));
    }

    // DOCK settings are optional: states from 0.1.0 do not have them and keep the current values.
    dockCount.store (std::clamp (xml.getIntAttribute ("dockCount", dockCount.load()), 1, maxDockSlots));
    for (int i = 0; i < maxDockSlots; ++i)
        dockSlotPanels[(size_t) i].store (std::clamp (xml.getIntAttribute ("dslot" + juce::String (i), dockSlotPanels[(size_t) i].load()), 0, numPanels - 1));
    dockHeight.store (std::clamp (xml.getIntAttribute ("dockHeight", dockHeight.load()), ui::dockMinHeight, ui::dockMaxHeight));

    pinned.store (xml.getBoolAttribute ("pinned", true));
    uiWidth.store (std::clamp (xml.getIntAttribute ("width", ui::defaultWidth), ui::minWidth, ui::maxWidth));
    uiHeight.store (std::clamp (xml.getIntAttribute ("height", ui::defaultHeight), ui::minHeight, ui::maxHeight));
}

void VintageVisualProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml != nullptr) applyStateXml (*xml);
}

// ---------------------------------------------------------------- user configuration

namespace
{
juce::File& configOverride()
{
    static juce::File file;  // empty = use the real location
    return file;
}
} // namespace

void VintageVisualProcessor::setConfigFileOverride (const juce::File& file)
{
    configOverride() = file;
}

juce::File VintageVisualProcessor::getUserConfigFile()
{
    if (configOverride() != juce::File())
        return configOverride();

    // Windows: %APPDATA%/Vintage Visual/config.xml (no admin rights needed, unlike Program Files)
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Vintage Visual")
        .getChildFile ("config.xml");
}

bool VintageVisualProcessor::saveUserConfig() const
{
    const auto file = getUserConfigFile();
    if (! file.getParentDirectory().createDirectory().wasOk()) return false;

    // Written to a temporary file first, so a crash never leaves a half-written configuration behind.
    juce::TemporaryFile temp (file);
    if (! createStateXml()->writeTo (temp.getFile())) return false;
    return temp.overwriteTargetFileWithTemporary();
}

bool VintageVisualProcessor::loadUserConfig()
{
    const auto file = getUserConfigFile();
    if (! file.existsAsFile() || file.getSize() > 64 * 1024) return false;

    const auto xml = juce::parseXML (file);
    if (xml == nullptr || ! xml->hasTagName ("VintageVisual")) return false;

    applyStateXml (*xml);  // same validation and limits as a project state
    return true;
}
} // namespace vintagevisual

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vintagevisual::VintageVisualProcessor();
}
