#include "../Source/FloatingWindow.h"
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <functional>
#if defined (__GLIBC__)
#include <malloc.h>
#endif


using namespace vintagevisual;

// ---------------------------------------------------------------------------------------------
// Allocation detector (glibc only): counts malloc/calloc/realloc/free made on the calling thread
// while `trackAllocations` is set. operator new goes through malloc, so it is covered as well.
// On other platforms the allocation test is skipped (see the note printed by the test).
// ---------------------------------------------------------------------------------------------
#if defined (__GLIBC__) && ! defined (VV_NO_ALLOCATION_HOOKS)
#include <atomic>
#include <cstdlib>

static thread_local bool trackAllocations = false;
static std::atomic<long long> allocationCount { 0 };

extern "C"
{
void* __libc_malloc (size_t);
void* __libc_calloc (size_t, size_t);
void* __libc_realloc (void*, size_t);
void __libc_free (void*);

void* malloc (size_t n) { if (trackAllocations) ++allocationCount; return __libc_malloc (n); }
void* calloc (size_t a, size_t b) { if (trackAllocations) ++allocationCount; return __libc_calloc (a, b); }
void* realloc (void* p, size_t n) { if (trackAllocations) ++allocationCount; return __libc_realloc (p, n); }
void free (void* p) { if (trackAllocations && p != nullptr) ++allocationCount; __libc_free (p); }
}
#define VV_HAS_ALLOCATION_TRACKING 1
#endif

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        ++checks;                                                                \
        if (! (cond))                                                            \
        {                                                                        \
            ++failures;                                                          \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(actual, expected, tol) CHECK (std::abs ((double) (actual) - (double) (expected)) <= (tol))

static void fillSine (juce::AudioBuffer<float>& b, double fs, double freq, double db, long long start)
{
    const double amp = std::pow (10.0, db / 20.0);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, (float) (amp * std::sin (2.0 * 3.14159265358979 * freq * (double) (start + i) / fs)));
}

static void testProcessor()
{
    VintageVisualProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512), copy (2, 512);
    juce::MidiBuffer midi;
    bool unchanged = true;

    for (int block = 0; block < 48000 * 10 / 512; ++block)
    {
        fillSine (buffer, 48000.0, 1000.0, -23.0, (long long) block * 512);
        copy.makeCopyOf (buffer);
        p.processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                if (buffer.getSample (c, i) != copy.getSample (c, i)) unchanged = false;
    }

    CHECK (unchanged);  // pass-through is bit-exact

    LoudnessReadings r;
    CHECK (p.pullReadings (r));
    CHECK_NEAR (r.momentary, -23.0, 0.15);
    CHECK_NEAR (r.integrated, -23.0, 0.15);
    CHECK_NEAR (r.truePeakDb, -23.0, 0.3);
    CHECK (! p.pullReadings (r));  // nothing new until the next block

    // The history feeds the visualisers.
    std::vector<float> l (256), rr (256);
    CHECK (p.getHistory().copyLatest (l.data(), rr.data(), 256) == 256);
    CHECK (std::abs (l[100]) > 0.0f);

    // Reset is applied on the audio thread at the next block.
    p.requestMeterReset();
    juce::AudioBuffer<float> silence (2, 512);
    silence.clear();
    p.processBlock (silence, midi);
    CHECK (p.pullReadings (r));
    CHECK (r.integrated == silenceLufs);
}

static void testLayouts()
{
    VintageVisualProcessor p;
    juce::AudioProcessor::BusesLayout stereo, mono;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    CHECK (p.checkBusesLayoutSupported (stereo));
    CHECK (! p.checkBusesLayoutSupported (mono));
    CHECK (p.getTailLengthSeconds() == 0.0);
    CHECK (p.getLatencySamples() == 0);
}

static void testState()
{
    VintageVisualProcessor a;
    a.skinIndex = 2;
    a.targetLufs = -16.0f;
    a.uiWidth = 900;
    a.uiHeight = 500;

    juce::MemoryBlock block;
    a.getStateInformation (block);

    VintageVisualProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (b.skinIndex.load() == 2);
    CHECK (b.targetLufs.load() == -16.0f);
    CHECK (b.uiWidth.load() == 900 && b.uiHeight.load() == 500);

    // Garbage and states from a newer version leave the defaults untouched.
    VintageVisualProcessor c;
    const char junk[] = "not a state";
    c.setStateInformation (junk, (int) sizeof (junk));
    CHECK (c.skinIndex.load() == 0 && c.targetLufs.load() == -14.0f);

    juce::XmlElement future ("VintageVisual");
    future.setAttribute ("version", 99);
    future.setAttribute ("skin", 1);
    juce::MemoryBlock futureBlock;
    VintageVisualProcessor::copyXmlToBinary (future, futureBlock);
    c.setStateInformation (futureBlock.getData(), (int) futureBlock.getSize());
    CHECK (c.skinIndex.load() == 0);

    // Out-of-range values are clamped.
    juce::XmlElement wild ("VintageVisual");
    wild.setAttribute ("version", 1);
    wild.setAttribute ("skin", 77);
    wild.setAttribute ("target", 500.0);
    wild.setAttribute ("width", 5);
    juce::MemoryBlock wildBlock;
    VintageVisualProcessor::copyXmlToBinary (wild, wildBlock);
    c.setStateInformation (wildBlock.getData(), (int) wildBlock.getSize());
    CHECK (c.skinIndex.load() == numSkins - 1);
    CHECK (c.targetLufs.load() == 0.0f);
    CHECK (c.uiWidth.load() == ui::minWidth);
}

static bool imageHasContent (const juce::Image& img)
{
    const auto corner = img.getPixelAt (1, 1);
    for (int y = 0; y < img.getHeight(); y += 3)
        for (int x = 0; x < img.getWidth(); x += 3)
            if (img.getPixelAt (x, y) != corner) return true;
    return false;
}

static void collectControls (juce::Component& parent, juce::Array<juce::Component*>& out)
{
    for (auto* child : parent.getChildren())
    {
        if (! child->isVisible()) continue;
        if (dynamic_cast<juce::Button*> (child) != nullptr || dynamic_cast<juce::ComboBox*> (child) != nullptr)
            out.add (child);
        collectControls (*child, out);
    }
}

static void testEditor()
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    CHECK (editor != nullptr);
    if (editor == nullptr) return;

    for (auto size : { juce::Point<int> (760, 420), juce::Point<int> (ui::minWidth, ui::minHeight), juce::Point<int> (1600, 900) })
    {
        editor->setSize (size.x, size.y);
        const auto bounds = editor->getLocalBounds();

        juce::Array<juce::Component*> controls;
        collectControls (*editor, controls);
        CHECK (controls.size() == 7);  // layout, panel 1, skin, target, reset, float, save

        juce::Array<juce::Rectangle<int>> boxes;
        for (auto* c : controls)
        {
            const auto b = editor->getLocalArea (c->getParentComponent(), c->getBounds());
            CHECK (bounds.contains (b));                // nothing clipped
            CHECK (c->getTitle().isNotEmpty());         // accessible name
            CHECK (c->getWantsKeyboardFocus());         // reachable with Tab
            for (auto& other : boxes) CHECK (! other.intersects (b));  // no overlap
            boxes.add (b);
        }

        CHECK (imageHasContent (editor->createComponentSnapshot (bounds)));
    }

    // Each skin repaints without trouble.
    for (int skin = 0; skin < numSkins; ++skin)
    {
        p.skinIndex = skin;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (760, 420);
        CHECK (imageHasContent (e->createComponentSnapshot (e->getLocalBounds())));
    }

    // The window size is remembered.
    editor->setSize (1000, 600);
    CHECK (p.uiWidth.load() == 1000 && p.uiHeight.load() == 600);
}


static void testFloatingWindow()
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    CHECK (! p.isMeterFloating());

    p.showFloatingMeter();
    CHECK (p.isMeterFloating());
    auto* w = p.getFloatingWindow();
    CHECK (w != nullptr);
    if (w == nullptr) return;

    p.showFloatingMeter();  // second call reuses the same window
    CHECK (p.getFloatingWindow() == w);

    // PIN
    CHECK (w->isPinned() && w->isAlwaysOnTop());
    w->setPinned (false);
    CHECK (! w->isAlwaysOnTop() && ! p.pinned.load());
    w->setPinned (true);
    CHECK (w->isAlwaysOnTop() && p.pinned.load());

    // Controls of the floating view are labelled, reachable and do not overlap.
    juce::Array<juce::Component*> controls;
    collectControls (*w, controls);
    CHECK (controls.size() == 10);  // layout, panel 1, skin, target, reset, pin, dock, full, close, save
    for (auto* c : controls)
    {
        CHECK (c->getTitle().isNotEmpty());
        CHECK (c->getWantsKeyboardFocus());
    }

    // DOCK: strip across the top of the display, restored afterwards.
    w->setBounds (200, 150, 800, 450);
    const auto before = w->getBounds();
    w->setDocked (true);
    CHECK (w->isDocked() && w->getView().isCompact());
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (w->getScreenBounds());
    CHECK (display != nullptr);
    if (display != nullptr)
    {
        CHECK (w->getHeight() == FloatingWindow::stripHeight);
        CHECK (w->getWidth() == display->userBounds.toNearestInt().getWidth());
        CHECK (w->getY() == display->userBounds.toNearestInt().getY());
    }
    CHECK (w->isAlwaysOnTop());

    juce::Array<juce::Component*> stripControls;
    collectControls (*w, stripControls);
    CHECK (stripControls.size() == 5 + p.dockCount.load());  // panel count, save, pin, dock, close + one "..." per panel
    for (auto* c : stripControls)
        CHECK (w->getLocalBounds().contains (w->getLocalArea (c->getParentComponent(), c->getBounds())));

    w->setDocked (false);
    CHECK (! w->isDocked() && ! w->getView().isCompact());
    CHECK (w->getBounds() == before);

    // FULL and DOCK are mutually exclusive.
    w->setFull (true);
    CHECK (w->isFull());
    w->setDocked (true);
    CHECK (w->isDocked() && ! w->isFull());
    w->setFull (true);
    CHECK (w->isFull() && ! w->isDocked());
    w->setFull (false);
    CHECK (! w->isFull());

    // Editor and floating window share one readings stream.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 48000 * 3 / 512; ++block)
    {
        fillSine (buffer, 48000.0, 1000.0, -23.0, (long long) block * 512);
        p.processBlock (buffer, midi);
    }
    p.refreshReadings();
    CHECK_NEAR (p.getLatestReadings().momentary, -23.0, 0.2);
    const auto serial = p.getReadingsSerial();
    p.refreshReadings();
    CHECK (p.getReadingsSerial() == serial);  // nothing new, nothing consumed twice

    p.resetMeters();
    CHECK (p.getLatestReadings().momentary == silenceLufs);

    p.hideFloatingMeter();
    CHECK (! p.isMeterFloating());

    // Pinned preference survives a save/restore.
    p.pinned = false;
    juce::MemoryBlock block;
    p.getStateInformation (block);
    VintageVisualProcessor q;
    q.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (! q.pinned.load());
}


static void testPanels()
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 48000 * 3 / 512; ++block)
    {
        fillSine (buffer, 48000.0, 1000.0, -12.0, (long long) block * 512);
        p.processBlock (buffer, midi);
    }

    for (int panel = 0; panel < numPanels; ++panel)
    {
        p.panelIndex = panel;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (760, 420);
        auto& editor = static_cast<VintageVisualEditor&> (*e);
        editor.refreshNow();
        editor.refreshNow();
        CHECK (imageHasContent (e->createComponentSnapshot (e->getLocalBounds())));

        juce::Array<juce::Component*> controls;
        collectControls (*e, controls);
        CHECK (controls.size() == 7);  // layout, panel 1, skin, target, reset, float, save
        for (auto* c : controls) CHECK (e->getLocalBounds().contains (e->getLocalArea (c->getParentComponent(), c->getBounds())));

        // Choosing a visualiser from the "..." menu is remembered by the processor.
        editor.getView().setSlotKind (0, (panel + 1) % numPanels);
        CHECK (p.panelIndex.load() == (panel + 1) % numPanels);
    }

    // The choice survives save/restore and is clamped.
    p.panelIndex = 2;
    juce::MemoryBlock block;
    p.getStateInformation (block);
    VintageVisualProcessor q;
    q.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (q.panelIndex.load() == 2);

    juce::XmlElement wild ("VintageVisual");
    wild.setAttribute ("panel", 42);
    juce::MemoryBlock wildBlock;
    VintageVisualProcessor::copyXmlToBinary (wild, wildBlock);
    q.setStateInformation (wildBlock.getData(), (int) wildBlock.getSize());
    CHECK (q.panelIndex.load() == numPanels - 1);
}

// ------------------------------------------------------------------------------------------------
// Performance
// ------------------------------------------------------------------------------------------------
#include <algorithm>
#include <chrono>

using Clock = std::chrono::steady_clock;
static double microsSince (Clock::time_point t0) { return std::chrono::duration<double, std::micro> (Clock::now() - t0).count(); }

struct AudioPerf
{
    double meanUs = 0, p99Us = 0, maxUs = 0, blockUs = 0;
    double cpuPercent() const { return 100.0 * meanUs / blockUs; }
    double worstPercent() const { return 100.0 * maxUs / blockUs; }
};

static AudioPerf measureAudio (double fs, int blockSize, double seconds)
{
    VintageVisualProcessor p;
    p.setRateAndBufferSizeDetails (fs, blockSize);
    p.prepareToPlay (fs, blockSize);

    juce::AudioBuffer<float> source (2, blockSize), work (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<double> micros;
    const int blocks = (int) (fs * seconds / blockSize);
    micros.reserve ((size_t) blocks);

    for (int b = 0; b < blocks; ++b)
    {
        fillSine (source, fs, 997.0, -14.0, (long long) b * blockSize);
        work.makeCopyOf (source, true);
        const auto t0 = Clock::now();
        p.processBlock (work, midi);
        micros.push_back (microsSince (t0));
    }

    std::sort (micros.begin(), micros.end());
    AudioPerf r;
    for (double m : micros) r.meanUs += m;
    r.meanUs /= (double) micros.size();
    r.p99Us = micros[(size_t) ((double) micros.size() * 0.99)];
    r.maxUs = micros.back();
    r.blockUs = 1.0e6 * blockSize / fs;
    return r;
}

static void testAudioThreadPerformance()
{
    struct Config { double fs; int block; };
    std::printf ("Audio thread (processBlock), 10 s of audio per configuration:\n");

    for (auto c : { Config { 44100.0, 64 }, Config { 48000.0, 512 }, Config { 48000.0, 4096 }, Config { 96000.0, 512 }, Config { 192000.0, 1024 } })
    {
        const auto r = measureAudio (c.fs, c.block, 10.0);
        std::printf ("  %6.0f Hz / %4d samples: mean %6.1f us (%.3f %% of the block), p99 %6.1f us, worst %7.1f us (%.1f %%)\n",
                     c.fs, c.block, r.meanUs, r.cpuPercent(), r.p99Us, r.maxUs, r.worstPercent());

        CHECK (r.cpuPercent() < 2.0);    // average load must stay tiny
        CHECK (r.p99Us < r.blockUs * 0.25);  // 99 % of blocks use under a quarter of the deadline
    }
}

static void testNoAllocationInProcessBlock()
{
#if defined (VV_HAS_ALLOCATION_TRACKING)
    VintageVisualProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    fillSine (buffer, 48000.0, 997.0, -14.0, 0);
    for (int i = 0; i < 50; ++i) p.processBlock (buffer, midi);  // warm up

    allocationCount = 0;
    trackAllocations = true;
    for (int i = 0; i < 4000; ++i)  // ~43 s of audio: crosses gating, short-term and LRA updates
    {
        if (i == 2000) p.requestMeterReset();
        p.processBlock (buffer, midi);
    }
    trackAllocations = false;

    std::printf ("Allocations/frees inside processBlock over 4000 blocks: %lld\n", allocationCount.load());
    CHECK (allocationCount.load() == 0);
#else
    std::printf ("Allocation test skipped: only implemented for glibc. On Windows use a profiler or Application Verifier.\n");
#endif
}

struct UiPerf
{
    double updateMs = 0, paintMs = 0;
};

static UiPerf measurePanel (int panel, int width, int height)
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    p.panelIndex = panel;

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    long long pos = 0;
    auto feed = [&] (int blocks)
    {
        for (int k = 0; k < blocks; ++k, pos += 512)
        {
            fillSine (buffer, 48000.0, 997.0, -14.0, pos);
            p.processBlock (buffer, midi);
        }
    };

    feed (200);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
    e->setSize (width, height);
    auto& editor = static_cast<VintageVisualEditor&> (*e);

    constexpr int frames = 60;
    UiPerf r;
    for (int i = 0; i < frames + 5; ++i)
    {
        feed (2);  // ~33 ms of audio per UI frame
        auto t0 = Clock::now();
        editor.refreshNow();
        const double u = microsSince (t0);
        t0 = Clock::now();
        auto img = e->createComponentSnapshot (e->getLocalBounds());
        const double d = microsSince (t0);
        if (i >= 5)  // skip warm-up
        {
            r.updateMs += u / 1000.0;
            r.paintMs += d / 1000.0;
        }
    }

    r.updateMs /= frames;
    r.paintMs /= frames;
    return r;
}

static void testUiPerformance()
{
    std::printf ("UI thread per frame (refresh + software paint of the whole editor), 30 Hz budget = 33 ms:\n");
    double worst = 0.0;

    for (auto size : { juce::Point<int> (760, 420), juce::Point<int> (1600, 900) })
    {
        for (int panel = 0; panel < numPanels; ++panel)
        {
            const auto r = measurePanel (panel, size.x, size.y);
            const double total = r.updateMs + r.paintMs;
            worst = std::max (worst, total);
            std::printf ("  %4dx%-4d %-10s update %5.2f ms, paint %5.2f ms, total %5.2f ms (%4.1f %% of one core at 30 Hz)\n",
                         size.x, size.y, getPanelName (static_cast<PanelKind> (panel)), r.updateMs, r.paintMs, total, total * 30.0 / 10.0);
            CHECK (total < 20.0);  // each panel must leave headroom inside the 33 ms frame
        }
    }
}

static void testStress()
{
    // 1) Denormal-prone input: digital silence and subnormal floats must not slow the audio thread.
    {
        VintageVisualProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> loud (2, 512), tiny (2, 512);
        juce::MidiBuffer midi;
        fillSine (loud, 48000.0, 997.0, -3.0, 0);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                tiny.setSample (c, i, (i % 2 == 0 ? 1.0e-40f : -1.0e-40f));  // subnormal

        for (int i = 0; i < 200; ++i) { auto b = loud; p.processBlock (b, midi); }  // excite the filters first

        std::vector<double> micros;
        for (int i = 0; i < 4000; ++i)
        {
            auto b = tiny;
            const auto t0 = Clock::now();
            p.processBlock (b, midi);
            micros.push_back (microsSince (t0));
        }
        std::sort (micros.begin(), micros.end());
        const double blockUs = 1.0e6 * 512 / 48000.0;
        std::printf ("Subnormal input: p99 %.1f us, worst %.1f us (block %.0f us)\n", micros[3960], micros.back(), blockUs);
        CHECK (micros[3960] < blockUs * 0.25);
    }

    // 2) Large host buffers (FL Studio allows big ones) and 1-sample blocks.
    for (int block : { 1, 4096, 8192 })
    {
        VintageVisualProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, block);
        p.prepareToPlay (48000.0, block);
        juce::AudioBuffer<float> b (2, block);
        juce::MidiBuffer midi;
        const int blocks = block == 1 ? 48000 * 4 : 48000 * 6 / block;
        for (int i = 0; i < blocks; ++i)
        {
            fillSine (b, 48000.0, 1000.0, -23.0, (long long) i * block);
            p.processBlock (b, midi);
        }
        LoudnessReadings r;
        CHECK (p.pullReadings (r));
        CHECK (std::abs (r.integrated - (-23.0)) < 0.2);
    }

    // 3) One hour of continuous audio: values stay exact, nothing allocates, nothing drifts.
    {
        VintageVisualProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        juce::MidiBuffer midi;

        // 480 samples = 10 whole periods of 1 kHz at 48 kHz, so repeating the block gives a continuous tone.
        juce::AudioBuffer<float> periodic (2, 480);
        fillSine (periodic, 48000.0, 1000.0, -23.0, 0);
        juce::AudioBuffer<float> workPeriodic (2, 480);

        const long long blocks = 3600LL * 48000 / 480;
        const auto t0 = Clock::now();
#if defined (VV_HAS_ALLOCATION_TRACKING)
        allocationCount = 0;
        trackAllocations = true;
#endif
        for (long long i = 0; i < blocks; ++i)
        {
            workPeriodic.makeCopyOf (periodic, true);
            p.processBlock (workPeriodic, midi);
        }
#if defined (VV_HAS_ALLOCATION_TRACKING)
        trackAllocations = false;
        std::printf ("One hour of audio processed in %.1f s; allocations: %lld\n", microsSince (t0) / 1.0e6, allocationCount.load());
        CHECK (allocationCount.load() == 0);
#else
        std::printf ("One hour of audio processed in %.1f s\n", microsSince (t0) / 1.0e6);
#endif
        LoudnessReadings r;
        CHECK (p.pullReadings (r));
        CHECK (std::abs (r.integrated - (-23.0)) < 0.1);
        CHECK (std::abs (r.momentary - (-23.0)) < 0.1);
        CHECK (std::abs (r.shortTerm - (-23.0)) < 0.1);
        CHECK (r.loudnessRange < 0.2);
        CHECK (std::abs (r.truePeakDb - (-23.0)) < 0.3);
    }
}

static void testNoLeaksOnOpenClose()
{
#if defined (__GLIBC__)
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    fillSine (buffer, 48000.0, 997.0, -14.0, 0);
    for (int i = 0; i < 100; ++i) p.processBlock (buffer, midi);

    auto cycle = [&]
    {
        for (int panel = 0; panel < numPanels; ++panel)
        {
            p.panelIndex = panel;
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            e->setSize (760, 420);
            static_cast<VintageVisualEditor&> (*e).refreshNow();
            auto img = e->createComponentSnapshot (e->getLocalBounds());
        }
        p.showFloatingMeter();
        p.getFloatingWindow()->setDocked (true);
        p.getFloatingWindow()->setDocked (false);
        p.hideFloatingMeter();
    };

    for (int i = 0; i < 10; ++i) cycle();  // warm-up: font and image caches
    const auto before = mallinfo2().uordblks;
    for (int i = 0; i < 60; ++i) cycle();
    const auto after = mallinfo2().uordblks;
    const double growthKb = ((double) after - (double) before) / 1024.0;
    std::printf ("Heap growth after 60 open/close cycles (7 editors + floating window each): %.1f KB\n", growthKb);
    // Valgrind reports no definite leaks. The remaining slow growth (~1 KB per editor, ~3-5 KB per window) also appears
    // with a bare juce::ResizableWindow on Linux/X11, so it comes from the platform layer. This limit still catches
    // the mistake that matters: forgetting to release a view (hundreds of KB per cycle).
    CHECK (growthKb < 2048.0);
#else
    std::printf ("Leak test skipped: only implemented for glibc.\n");
#endif
}

static void testGridSlotsAndSkins()
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 400; ++block) { fillSine (buffer, 48000.0, 1000.0, -12.0, (long long) block * 512); p.processBlock (buffer, midi); }

    // Every layout shows the right number of panel selectors, all inside the window and not overlapping.
    for (int layout = 0; layout < numUserLayouts; ++layout)
    {
        p.layoutPreset = layout;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (1000, 600);
        auto& editor = static_cast<VintageVisualEditor&> (*e);
        editor.refreshNow();
        editor.refreshNow();

        juce::Array<juce::Component*> controls;
        collectControls (*e, controls);
        int slotBoxes = 0;
        juce::Array<juce::Rectangle<int>> boxes;
        for (auto* c : controls)
        {
            if (c->getTitle().startsWith ("Panel ")) ++slotBoxes;
            const auto b = e->getLocalArea (c->getParentComponent(), c->getBounds());
            CHECK (e->getLocalBounds().contains (b));
            for (auto& other : boxes) CHECK (! other.intersects (b));
            boxes.add (b);
            CHECK (c->getTitle().isNotEmpty());
        }
        CHECK (slotBoxes == slotCount (static_cast<LayoutPreset> (layout)));
        CHECK (editor.getView().getActiveSlotCount() == slotBoxes);
        CHECK (imageHasContent (e->createComponentSnapshot (e->getLocalBounds())));
    }

    // Swapping two panels (what dragging one onto another does) is stored and rebuilt.
    p.layoutPreset = 3;  // four panels
    p.slotPanels[0] = 0; p.slotPanels[1] = 1; p.slotPanels[2] = 2; p.slotPanels[3] = 3;
    {
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (1000, 600);
        auto& view = static_cast<VintageVisualEditor&> (*e).getView();
        view.swapSlots (0, 3);
        CHECK (p.slotPanels[0].load() == 3 && p.slotPanels[3].load() == 0);
        CHECK (view.getSlotKind (0) == 3 && view.getSlotKind (3) == 0);
        view.swapSlots (1, 1);   // same slot: nothing
        view.swapSlots (0, 9);   // out of range: nothing
        view.swapSlots (-1, 2);
        CHECK (p.slotPanels[1].load() == 1 && p.slotPanels[2].load() == 2);
    }

    // Skin button names the current skin; every skin renders; its menu opens and closes cleanly.
    p.layoutPreset = 0;
    for (int skin = 0; skin < numSkins; ++skin)
    {
        p.skinIndex = skin;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (760, 420);
        static_cast<VintageVisualEditor&> (*e).refreshNow();

        juce::Array<juce::Component*> controls;
        collectControls (*e, controls);
        for (auto* c : controls)
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getTitle() == "Skin")
                CHECK (b->getButtonText() == juce::String (skins[(size_t) skin].name));
        CHECK (imageHasContent (e->createComponentSnapshot (e->getLocalBounds())));
    }

    // Clicking the button must not crash (the menu itself opens with the message loop, which tests do not run).
    {
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (760, 420);
        juce::Array<juce::Component*> controls;
        collectControls (*e, controls);
        for (auto* c : controls)
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getTitle() == "Skin")
                b->triggerClick();
        juce::PopupMenu::dismissAllActiveMenus();
    }

    // Each menu row is drawn in its own skin's colours: surface colour in the background, swatches on the right.
    for (int skin = 0; skin < numSkins; ++skin)
    {
        const auto row = renderSkinMenuItem (skin, skin == 0);
        CHECK (row.getWidth() > 100 && row.getHeight() > 20);
        const auto& c = skins[(size_t) skin].colors;
        CHECK (row.getPixelAt (8, row.getHeight() / 2) == theme::colour (c.surface));  // background: that skin's surface
        const auto accentSwatch = row.getPixelAt (row.getWidth() - 4 * 19 + 16 + 3 + 8 - 6, row.getHeight() / 2);
        juce::ignoreUnused (accentSwatch);
        CHECK (imageHasContent (row));
    }
}

static void testFullGrid()
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 400; ++block) { fillSine (buffer, 48000.0, 1000.0, -12.0, (long long) block * 512); p.processBlock (buffer, midi); }

    p.layoutPreset = 1;  // two panels in the normal view
    p.showFloatingMeter();
    auto* w = p.getFloatingWindow();
    CHECK (w != nullptr);
    if (w == nullptr) return;
    CHECK (w->getView().getActiveSlotCount() == 2);

    w->setFull (true);
    auto& view = w->getView();
    view.refreshNow();
    CHECK (view.getActiveSlotCount() == 5);
    // FULL defaults: spectrogram, loudness, waveform, stereo, levels.
    CHECK (view.getSlotKind (0) == (int) PanelKind::spectrogram && view.getSlotKind (1) == (int) PanelKind::loudness);
    CHECK (view.getSlotKind (2) == (int) PanelKind::waveform && view.getSlotKind (3) == (int) PanelKind::goniometer);
    CHECK (view.getSlotKind (4) == (int) PanelKind::levels);
    CHECK (imageHasContent (w->createComponentSnapshot (w->getLocalBounds())));

    // Cost of the whole FULL grid (5 modules at once) per UI frame.
    {
        double total = 0.0;
        constexpr int frames = 40;
        for (int i = 0; i < frames + 5; ++i)
        {
            for (int b = 0; b < 2; ++b) { fillSine (buffer, 48000.0, 997.0, -14.0, (long long) (i * 2 + b) * 512); p.processBlock (buffer, midi); }
            const auto t0 = Clock::now();
            view.refreshNow();
            auto img = w->createComponentSnapshot (w->getLocalBounds());
            if (i >= 5) total += microsSince (t0) / 1000.0;
        }
        const double ms = total / frames;
        std::printf ("FULL grid (5 modules) at %dx%d: %.2f ms per frame (%.1f %% of one core at 30 Hz)\n", w->getWidth(), w->getHeight(), ms, ms * 3.0);
        CHECK (ms < 25.0);
    }

    // Editing the FULL grid does not touch the normal layouts.
    const int normalSlot0 = p.slotPanels[0].load();
    view.swapSlots (0, 2);
    CHECK (p.fullSlotPanels[0].load() == (int) PanelKind::waveform && p.fullSlotPanels[2].load() == (int) PanelKind::spectrogram);
    CHECK (p.slotPanels[0].load() == normalSlot0);

    // Leaving FULL restores the normal layout.
    w->setFull (false);
    view.refreshNow();
    CHECK (view.getActiveSlotCount() == 2);
    CHECK (view.getSlotKind (0) == normalSlot0);

    // The strip (DOCK) still works with a grid configured.
    w->setDocked (true);
    CHECK (w->getView().isCompact());
    w->setDocked (false);
    p.hideFloatingMeter();

    // The slot assignment and layout survive save/restore.
    p.layoutPreset = 2;
    p.slotPanels[2] = 11;
    p.fullSlotPanels[4] = 13;
    juce::MemoryBlock block;
    p.getStateInformation (block);
    VintageVisualProcessor q;
    q.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (q.layoutPreset.load() == 2 && q.slotPanels[2].load() == 11 && q.fullSlotPanels[4].load() == 13);
}

static void testDockStrip()
{
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0; block < 400; ++block) { fillSine (buffer, 48000.0, 1000.0, -12.0, (long long) block * 512); p.processBlock (buffer, midi); }

    p.showFloatingMeter();
    auto* w = p.getFloatingWindow();
    CHECK (w != nullptr);
    if (w == nullptr) return;

    const int normalSlot0 = p.slotPanels[0].load(), fullSlot0 = p.fullSlotPanels[0].load();
    w->setDocked (true);
    auto& view = w->getView();
    CHECK (view.isCompact());
    view.refreshNow();

    // Default strip: LUFS, spectrum and levels.
    CHECK (view.getActiveSlotCount() == 3);
    CHECK (view.getSlotKind (0) == (int) PanelKind::loudness && view.getSlotKind (1) == (int) PanelKind::spectrum && view.getSlotKind (2) == (int) PanelKind::levels);

    // From 1 up to 8 visualisers: each has its own labelled, reachable "..." selector, nothing overlaps or is clipped.
    for (int count = 1; count <= maxDockSlots; ++count)
    {
        p.dockCount = count;
        view.refreshNow();
        CHECK (view.getActiveSlotCount() == count);

        juce::Array<juce::Component*> controls;
        collectControls (*w, controls);
        juce::Array<juce::Rectangle<int>> boxes;
        int selectors = 0;
        for (auto* c : controls)
        {
            const auto b = w->getLocalArea (c->getParentComponent(), c->getBounds());
            CHECK (c->getTitle().isNotEmpty());
            CHECK (c->getWantsKeyboardFocus());
            CHECK (w->getLocalBounds().contains (b));
            for (auto& other : boxes) CHECK (! other.intersects (b));
            boxes.add (b);
            if (c->getTitle().startsWith ("Panel ")) ++selectors;
        }
        CHECK (selectors == count);
        CHECK (imageHasContent (w->createComponentSnapshot (w->getLocalBounds())));
    }

    // Every kind of visualiser can be shown in the strip.
    p.dockCount = 1;
    for (int kind = 0; kind < numPanels; ++kind)
    {
        p.dockSlotPanels[0] = kind;
        view.refreshNow();
        view.refreshNow();
        CHECK (view.getSlotKind (0) == kind);
    }

    // The "..." menu choice is stored for the strip only. Out-of-range slots are ignored.
    p.dockCount = 8;
    view.refreshNow();
    view.setSlotKind (2, (int) PanelKind::tuner);
    CHECK (p.dockSlotPanels[2].load() == (int) PanelKind::tuner && view.getSlotKind (2) == (int) PanelKind::tuner);
    view.setSlotKind (8, 1);
    view.setSlotKind (-1, 1);
    CHECK (p.dockSlotPanels[2].load() == (int) PanelKind::tuner);

    // Dragging one panel onto another swaps them (what the mouse does), anywhere in the row.
    for (int i = 0; i < maxDockSlots; ++i) p.dockSlotPanels[(size_t) i] = i;
    view.refreshNow();
    view.swapSlots (0, 7);
    CHECK (p.dockSlotPanels[0].load() == 7 && p.dockSlotPanels[7].load() == 0);
    CHECK (view.getSlotKind (0) == 7 && view.getSlotKind (7) == 0);
    view.swapSlots (3, 3);
    view.swapSlots (0, 8);
    CHECK (p.dockSlotPanels[3].load() == 3);

    // The strip never touches the normal layouts or the FULL grid.
    CHECK (p.slotPanels[0].load() == normalSlot0 && p.fullSlotPanels[0].load() == fullSlot0);

    // The "..." button opens its menu without crashing (the menu itself needs the message loop).
    {
        juce::Array<juce::Component*> controls;
        collectControls (*w, controls);
        for (auto* c : controls)
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getTitle() == "Panel 1 visualizer")
                b->triggerClick();
        juce::PopupMenu::dismissAllActiveMenus();
    }

    // Eight different visualisers at once stay inside the frame budget.
    {
        const int kinds[8] = { (int) PanelKind::spectrogram, (int) PanelKind::spectrum, (int) PanelKind::waveform, (int) PanelKind::scope,
                               (int) PanelKind::goniometer, (int) PanelKind::orb, (int) PanelKind::mandala, (int) PanelKind::signalMap };
        for (int i = 0; i < 8; ++i) p.dockSlotPanels[(size_t) i] = kinds[i];
        p.dockCount = 8;
        view.refreshNow();

        double total = 0.0;
        constexpr int frames = 40;
        for (int i = 0; i < frames + 5; ++i)
        {
            for (int b = 0; b < 2; ++b) { fillSine (buffer, 48000.0, 997.0, -14.0, (long long) (i * 2 + b) * 512); p.processBlock (buffer, midi); }
            const auto t0 = Clock::now();
            view.refreshNow();
            auto img = w->createComponentSnapshot (w->getLocalBounds());
            if (i >= 5) total += microsSince (t0) / 1000.0;
        }
        const double ms = total / frames;
        std::printf ("DOCK strip (8 visualisers) at %dx%d: %.2f ms per frame (%.1f %% of one core at 30 Hz)\n", w->getWidth(), w->getHeight(), ms, ms * 3.0);
        CHECK (ms < 25.0);
    }

    // The strip is resizable in height, the height is remembered, and it is restored when docking again.
    const int width = w->getWidth();
    w->setSize (width, 320);
    CHECK (w->getHeight() == 320 && p.dockHeight.load() == 320);
    w->setDocked (false);
    w->setDocked (true);
    CHECK (w->getHeight() == 320);
    CHECK (320 >= ui::dockMinHeight && FloatingWindow::stripHeight == ui::dockMinHeight);

    p.hideFloatingMeter();

    // The strip's settings survive save/restore and are clamped.
    p.dockCount = 5;
    p.dockSlotPanels[4] = 12;
    p.dockHeight = 222;
    juce::MemoryBlock block;
    p.getStateInformation (block);
    VintageVisualProcessor q;
    q.setStateInformation (block.getData(), (int) block.getSize());
    CHECK (q.dockCount.load() == 5 && q.dockSlotPanels[4].load() == 12 && q.dockHeight.load() == 222);

    juce::XmlElement wild ("VintageVisual");
    wild.setAttribute ("version", 2);
    wild.setAttribute ("dockCount", 99);
    wild.setAttribute ("dslot0", 77);
    wild.setAttribute ("dockHeight", 5);
    juce::MemoryBlock wildBlock;
    VintageVisualProcessor::copyXmlToBinary (wild, wildBlock);
    q.setStateInformation (wildBlock.getData(), (int) wildBlock.getSize());
    CHECK (q.dockCount.load() == maxDockSlots && q.dockSlotPanels[0].load() == numPanels - 1 && q.dockHeight.load() == ui::dockMinHeight);

    // A state saved by 0.1.0 (no strip settings) still loads and leaves the strip as it was.
    juce::XmlElement old ("VintageVisual");
    old.setAttribute ("version", 1);
    old.setAttribute ("skin", 2);
    juce::MemoryBlock oldBlock;
    VintageVisualProcessor::copyXmlToBinary (old, oldBlock);
    q.setStateInformation (oldBlock.getData(), (int) oldBlock.getSize());
    CHECK (q.skinIndex.load() == 2 && q.dockCount.load() == maxDockSlots);
}

static void testUserConfig()
{
    const auto file = VintageVisualProcessor::getUserConfigFile();
    file.deleteFile();

    // No saved configuration: a new instance starts from the factory defaults.
    {
        VintageVisualProcessor fresh;
        CHECK (! fresh.loadUserConfig());
        CHECK (fresh.skinIndex.load() == 0 && fresh.targetLufs.load() == -14.0f && fresh.layoutPreset.load() == 0 && fresh.dockCount.load() == 3);
    }

    // The user builds a configuration and saves it.
    {
        VintageVisualProcessor a;
        a.skinIndex = 5;
        a.targetLufs = -23.0f;
        a.layoutPreset = 3;
        a.slotPanels[2] = 11;
        a.fullSlotPanels[4] = 13;
        a.dockCount = 6;
        a.dockSlotPanels[5] = 9;
        a.dockHeight = 260;
        a.pinned = false;
        a.uiWidth = 900;
        a.uiHeight = 500;
        CHECK (a.saveUserConfig());
        CHECK (file.existsAsFile());
    }

    // A NEW instance opens already configured.
    {
        VintageVisualProcessor b;
        CHECK (b.skinIndex.load() == 5 && b.targetLufs.load() == -23.0f && b.layoutPreset.load() == 3);
        CHECK (b.slotPanels[2].load() == 11 && b.fullSlotPanels[4].load() == 13);
        CHECK (b.dockCount.load() == 6 && b.dockSlotPanels[5].load() == 9 && b.dockHeight.load() == 260);
        CHECK (! b.pinned.load() && b.uiWidth.load() == 900 && b.uiHeight.load() == 500);
    }

    // A project restored by the DAW keeps ITS state, not the saved configuration.
    {
        VintageVisualProcessor source;
        source.skinIndex = 1;
        source.dockCount = 2;
        juce::MemoryBlock block;
        source.getStateInformation (block);

        VintageVisualProcessor project;  // starts from the saved configuration (skin 5)...
        CHECK (project.skinIndex.load() == 5);
        project.setStateInformation (block.getData(), (int) block.getSize());  // ...then the host restores the project
        CHECK (project.skinIndex.load() == 1 && project.dockCount.load() == 2);
    }

    // Saving again replaces the file; no temporary file is left behind.
    {
        VintageVisualProcessor c;
        c.skinIndex = 7;
        CHECK (c.saveUserConfig());
        VintageVisualProcessor d;
        CHECK (d.skinIndex.load() == 7);
        CHECK (file.getParentDirectory().getNumberOfChildFiles (juce::File::findFiles, "*") == 1);
    }

    // A damaged, foreign, newer or out-of-range file never breaks a new instance.
    file.replaceWithText ("this is not xml");
    { VintageVisualProcessor e; CHECK (e.skinIndex.load() == 0 && ! e.loadUserConfig()); }

    file.replaceWithText ("<SomethingElse skin=\"3\"/>");
    { VintageVisualProcessor e; CHECK (e.skinIndex.load() == 0); }

    file.replaceWithText ("<VintageVisual version=\"99\" skin=\"3\"/>");
    { VintageVisualProcessor e; CHECK (e.skinIndex.load() == 0); }

    file.replaceWithText ("<VintageVisual version=\"2\" skin=\"77\" dockCount=\"99\" target=\"500\"/>");
    { VintageVisualProcessor e; CHECK (e.skinIndex.load() == numSkins - 1 && e.dockCount.load() == maxDockSlots && e.targetLufs.load() == 0.0f); }

    // The Save button: writes the file, confirms, and goes back to its normal label.
    file.deleteFile();
    {
        VintageVisualProcessor p;
        p.prepareToPlay (48000.0, 512);
        p.skinIndex = 4;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (760, 420);
        auto& view = static_cast<VintageVisualEditor&> (*e).getView();

        juce::Array<juce::Component*> controls;
        collectControls (*e, controls);
        juce::TextButton* save = nullptr;
        for (auto* c : controls)
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getTitle() == "Save configuration")
                save = b;

        CHECK (save != nullptr);
        if (save != nullptr)
        {
            CHECK (save->getButtonText() == "Save" && ! file.existsAsFile());
            save->triggerClick();
            CHECK (file.existsAsFile());
            CHECK (save->getButtonText() == "Saved");
            for (int i = 0; i < 80; ++i) view.refreshNow();
            CHECK (save->getButtonText() == "Save");

            VintageVisualProcessor next;
            CHECK (next.skinIndex.load() == 4);
        }
    }

    file.deleteFile();
}

// Optional: VV_LEAK_DIAG=1 prints heap growth per cycle for each UI scenario, to locate a leak.
static void diagnoseGrowth()
{
#if defined (__GLIBC__)
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    fillSine (buffer, 48000.0, 997.0, -14.0, 0);
    for (int i = 0; i < 100; ++i) p.processBlock (buffer, midi);

    auto measure = [&] (const char* name, const std::function<void()>& fn)
    {
        for (int i = 0; i < 10; ++i) fn();
        const auto before = mallinfo2().uordblks;
        for (int i = 0; i < 50; ++i) fn();
        std::printf ("  %-34s %8.2f KB per cycle\n", name, ((double) mallinfo2().uordblks - (double) before) / 1024.0 / 50.0);
    };

    measure ("editor only", [&] { std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor()); });
    for (int panel = 0; panel < numPanels; ++panel)
    {
        const auto label = juce::String ("editor + refresh + paint, panel ") + juce::String (panel);
        measure (label.toRawUTF8(), [&]
        {
            p.panelIndex = panel;
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            e->setSize (760, 420);
            static_cast<VintageVisualEditor&> (*e).refreshNow();
            auto img = e->createComponentSnapshot (e->getLocalBounds());
        });
    }
    p.panelIndex = 0;
    measure ("floating show/hide", [&] { p.showFloatingMeter(); p.hideFloatingMeter(); });
    measure ("floating + dock toggle", [&] { p.showFloatingMeter(); p.getFloatingWindow()->setDocked (true); p.getFloatingWindow()->setDocked (false); p.hideFloatingMeter(); });
    measure ("BARE juce window show/delete", [&]
    {
        juce::ResizableWindow w ("x", juce::Colours::black, true);
        w.setUsingNativeTitleBar (false);
        w.setSize (300, 200);
        w.setVisible (true);
    });
    measure ("BARE juce window + setAlwaysOnTop", [&]
    {
        juce::ResizableWindow w ("x", juce::Colours::black, true);
        w.setSize (300, 200);
        w.setVisible (true);
        w.setAlwaysOnTop (true);
        w.setAlwaysOnTop (false);
    });
    measure ("postAnnouncement only", [&]
    {
        juce::AccessibilityHandler::postAnnouncement ("Window kept on top", juce::AccessibilityHandler::AnnouncementPriority::medium);
    });
    measure ("floating + pin toggle", [&] { p.showFloatingMeter(); p.getFloatingWindow()->setPinned (false); p.getFloatingWindow()->setPinned (true); p.hideFloatingMeter(); });
#endif
}

// Optional: VV_SNAPSHOT_DIR=/some/dir writes PNGs of every skin (embedded editor and docked strip).
static void writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    VintageVisualProcessor p;
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    int nextBlock = 0;
    enum class Source { chord, pureNote, sweep };
    Source source = Source::chord;
    double sweepPhase = 0.0;
    auto feedBlocks = [&] (int count)
    {
        for (int k = 0; k < count; ++k, ++nextBlock)
        {
            for (int i = 0; i < 512; ++i)
            {
                const long long n = (long long) nextBlock * 512 + i;
                const double t = (double) n / 48000.0;
                constexpr double twoPi = 2.0 * 3.14159265358979;
                float l = 0.0f, r = 0.0f;

                if (source == Source::pureNote)  // A3 + 12 cents
                {
                    l = r = (float) (0.2 * std::sin (twoPi * 220.0 * std::pow (2.0, 12.0 / 1200.0) * t));
                }
                else if (source == Source::sweep)  // slow logarithmic sweep, 40 Hz .. 15 kHz, repeating
                {
                    const double f = 40.0 * std::pow (15000.0 / 40.0, std::fmod (t / 6.0, 1.0));
                    sweepPhase += twoPi * f / 48000.0;
                    l = r = (float) (0.3 * std::sin (sweepPhase) + 0.12 * std::sin (twoPi * 110.0 * t));
                }
                else
                {
                    const float tone = (float) (0.178 * std::sin (twoPi * 1000.0 * t));
                    const float extra = (float) (0.12 * std::sin (twoPi * 110.0 * t) + 0.06 * std::sin (twoPi * 4400.0 * t)
                                                 + 0.03 * std::sin (twoPi * 12000.0 * t));
                    l = tone + extra;
                    r = tone + 0.8f * extra;
                }

                buffer.setSample (0, i, l);
                buffer.setSample (1, i, r);
            }
            p.processBlock (buffer, midi);
        }
    };
    feedBlocks (48000 * 6 / 512);


    juce::PNGImageFormat png;
    auto save = [&] (const juce::Image& img, const juce::String& name)
    {
        juce::FileOutputStream out (dir.getChildFile (name));
        if (out.openedOk()) { out.setPosition (0); out.truncate(); png.writeImageToStream (img, out); }
    };

    {
        juce::Image menu (juce::Image::ARGB, 240, 34 * numSkins, true);
        juce::Graphics mg (menu);
        for (int skin = 0; skin < numSkins; ++skin)
            mg.drawImageAt (renderSkinMenuItem (skin, skin == 0), 0, skin * 34);
        save (menu, "skin_menu.png");
    }

    for (int skin = 0; skin < numSkins; ++skin)
    {
        p.skinIndex = skin;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        e->setSize (760, 420);
        static_cast<VintageVisualEditor&> (*e).refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds()), "editor_" + juce::String (skin) + ".png");

        for (int panel = 1; panel < numPanels; ++panel)
        {
            if (panel % numSkins != skin) continue;  // each panel is shown in one skin; every skin gets a FULL grid below
            p.panelIndex = panel;
            std::unique_ptr<juce::AudioProcessorEditor> pe (p.createEditor());
            pe->setSize (760, 420);
            const auto kind = static_cast<PanelKind> (panel);
            source = kind == PanelKind::tuner ? Source::pureNote : (kind == PanelKind::spectrogram ? Source::sweep : Source::chord);
            const int ticks = kind == PanelKind::spectrogram ? 420 : 6;
            for (int k = 0; k < ticks; ++k)
            {
                feedBlocks (3);  // keep audio flowing: follower panels only see audio after they open
                static_cast<VintageVisualEditor&> (*pe).refreshNow();
            }
            source = Source::chord;
            save (pe->createComponentSnapshot (pe->getLocalBounds()), "panel" + juce::String (panel) + "_" + juce::String (skin) + ".png");
        }
        p.panelIndex = 0;

        p.showFloatingMeter();
        auto* w = p.getFloatingWindow();
        w->setDocked (true);
        w->setSize (1100, FloatingWindow::stripHeight);
        w->getView().refreshNow();
        save (w->createComponentSnapshot (w->getLocalBounds()), "strip_" + juce::String (skin) + ".png");
        p.hideFloatingMeter();

        // FULL grid in this skin (spectrogram needs time to fill up).
        p.showFloatingMeter();
        w = p.getFloatingWindow();
        w->setFull (true);
        source = Source::sweep;
        for (int k = 0; k < 160; ++k)
        {
            feedBlocks (3);
            w->getView().refreshNow();
        }
        source = Source::chord;
        save (w->createComponentSnapshot (w->getLocalBounds()), "full_" + juce::String (skin) + ".png");
        p.hideFloatingMeter();

        if (skin == 0)
        {
            p.layoutPreset = 3;  // four panels
            std::unique_ptr<juce::AudioProcessorEditor> qe (p.createEditor());
            qe->setSize (1000, 600);
            for (int k = 0; k < 10; ++k) { feedBlocks (3); static_cast<VintageVisualEditor&> (*qe).refreshNow(); }
            save (qe->createComponentSnapshot (qe->getLocalBounds()), "quad_0.png");
            p.layoutPreset = 0;
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Keep every test away from the user's real saved configuration.
    VintageVisualProcessor::setConfigFileOverride (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                       .getChildFile ("VintageVisual-tests")
                                                       .getChildFile ("config.xml"));
    VintageVisualProcessor::getUserConfigFile().deleteFile();

    if (juce::SystemStats::getEnvironmentVariable ("VV_LEAK_SCENARIO", {}).isNotEmpty())
    {
        // Small, repeatable scenario for Valgrind: leaks that repeat 15 times stand out.
        VintageVisualProcessor p;
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer midi;
        fillSine (b, 48000.0, 997.0, -14.0, 0);
        for (int i = 0; i < 100; ++i) p.processBlock (b, midi);
        for (int i = 0; i < 15; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            e->setSize (760, 420);
            static_cast<VintageVisualEditor&> (*e).refreshNow();
            auto img = e->createComponentSnapshot (e->getLocalBounds());
        }
        for (int i = 0; i < 15; ++i)
        {
            p.showFloatingMeter();
            p.getFloatingWindow()->getView().refreshNow();
            p.hideFloatingMeter();
        }
        return 0;
    }

    if (juce::SystemStats::getEnvironmentVariable ("VV_LEAK_DIAG", {}).isNotEmpty())
    {
        std::printf ("Heap growth per cycle:\n");
        diagnoseGrowth();
        return 0;
    }

    if (const auto dir = juce::SystemStats::getEnvironmentVariable ("VV_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        writeSnapshots (juce::File (dir));
        return 0;
    }

    testProcessor();
    testLayouts();
    testState();
    testEditor();
    testFloatingWindow();
    testPanels();
    testGridSlotsAndSkins();
    testFullGrid();
    testDockStrip();
    testUserConfig();
    testNoAllocationInProcessBlock();
    testAudioThreadPerformance();
    testUiPerformance();
    testStress();
    testNoLeaksOnOpenClose();

    VintageVisualProcessor::getUserConfigFile().getParentDirectory().deleteRecursively();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
