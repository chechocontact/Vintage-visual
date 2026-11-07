#include "../Source/Core/AudioHistory.h"
#include "../Source/Core/LoudnessMeter.h"
#include "../Source/Core/DualSpectrum.h"
#include "../Source/Core/Fft.h"
#include "../Source/Core/Layouts.h"
#include "../Source/Core/Pitch.h"
#include "../Source/Core/LevelTracker.h"
#include "../Source/Core/Skins.h"
#include "../Source/Core/SpectrumAnalyzer.h"
#include "../Source/Core/TripleBuffer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace vintagevisual;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                      \
    do                                                                                   \
    {                                                                                    \
        ++checks;                                                                        \
        if (! (cond))                                                                    \
        {                                                                                \
            ++failures;                                                                  \
            std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                 \
        }                                                                                \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                \
    do                                                                                   \
    {                                                                                    \
        ++checks;                                                                        \
        const double a_ = (actual), e_ = (expected);                                     \
        if (! (std::abs (a_ - e_) <= (tol)))                                             \
        {                                                                                \
            ++failures;                                                                  \
            std::printf ("FAIL %s:%d  %s = %.4f, expected %.4f +/- %.4f\n",              \
                         __FILE__, __LINE__, #actual, a_, e_, (double) (tol));           \
        }                                                                                \
    } while (0)

static double dbToLinear (double db) { return std::pow (10.0, db / 20.0); }

struct Signal
{
    std::vector<float> left, right;
};

static Signal sine (double fs, double freq, double ampDb, double seconds, bool leftOnly = false, double phase = 0.0)
{
    const auto n = (size_t) (fs * seconds);
    Signal s { std::vector<float> (n), std::vector<float> (n) };
    const double amp = dbToLinear (ampDb);

    for (size_t i = 0; i < n; ++i)
    {
        const auto v = (float) (amp * std::sin (2.0 * pi * freq * (double) i / fs + phase));
        s.left[i] = v;
        s.right[i] = leftOnly ? 0.0f : v;
    }

    return s;
}

static void append (Signal& dst, const Signal& src)
{
    dst.left.insert (dst.left.end(), src.left.begin(), src.left.end());
    dst.right.insert (dst.right.end(), src.right.begin(), src.right.end());
}

static void run (LoudnessMeter& m, const Signal& s, int block = 512)
{
    const size_t n = s.left.size();

    for (size_t i = 0; i < n; i += (size_t) block)
    {
        const int len = (int) std::min ((size_t) block, n - i);
        const float* ch[2] = { s.left.data() + i, s.right.data() + i };
        m.process (ch, len);
    }
}

static void testKWeightingCoefficients()
{
    // Published BS.1770 coefficients for 48 kHz.
    const auto k = designKWeighting (48000.0);
    CHECK_NEAR (k.shelf.b0, 1.53512485958697, 1.0e-5);
    CHECK_NEAR (k.shelf.b1, -2.69169618940638, 1.0e-5);
    CHECK_NEAR (k.shelf.b2, 1.19839281085285, 1.0e-5);
    CHECK_NEAR (k.shelf.a1, -1.69065929318241, 1.0e-5);
    CHECK_NEAR (k.shelf.a2, 0.73248077421585, 1.0e-5);
    CHECK_NEAR (k.highPass.a1, -1.99004745483398, 1.0e-5);
    CHECK_NEAR (k.highPass.a2, 0.99007225036621, 1.0e-5);
}

static void testReferenceTones()
{
    // EBU Tech 3341: stereo 1 kHz at -23 dBFS reads -23.0 LUFS.
    LoudnessMeter m;
    m.prepare (48000.0, 2);
    run (m, sine (48000.0, 1000.0, -23.0, 20.0));
    CHECK_NEAR (m.getReadings().momentary, -23.0, 0.1);
    CHECK_NEAR (m.getReadings().shortTerm, -23.0, 0.1);
    CHECK_NEAR (m.getReadings().integrated, -23.0, 0.1);

    // BS.1770: 0 dBFS 997 Hz in one channel reads -3.01 LUFS.
    LoudnessMeter mono;
    mono.prepare (48000.0, 2);
    run (mono, sine (48000.0, 997.0, 0.0, 10.0, true));
    CHECK_NEAR (mono.getReadings().integrated, -3.01, 0.05);
}

static void testSampleRates()
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        LoudnessMeter m;
        m.prepare (fs, 2);
        run (m, sine (fs, 1000.0, -23.0, 10.0));
        CHECK_NEAR (m.getReadings().integrated, -23.0, 0.1);
    }
}

static void testGating()
{
    // Silence after a tone must not pull the integrated value down (absolute gate).
    {
        LoudnessMeter m;
        m.prepare (48000.0, 2);
        auto s = sine (48000.0, 1000.0, -23.0, 10.0);
        append (s, sine (48000.0, 1000.0, -200.0, 10.0));
        run (m, s);
        CHECK_NEAR (m.getReadings().integrated, -23.0, 0.1);
    }

    // Material more than 10 LU below the average is excluded (relative gate).
    {
        LoudnessMeter m;
        m.prepare (48000.0, 2);
        auto s = sine (48000.0, 1000.0, -20.0, 10.0);
        append (s, sine (48000.0, 1000.0, -40.0, 10.0));
        run (m, s);
        CHECK_NEAR (m.getReadings().integrated, -20.0, 0.1);
    }
}

static void testLoudnessRange()
{
    LoudnessMeter m;
    m.prepare (48000.0, 2);
    auto s = sine (48000.0, 1000.0, -20.0, 20.0);
    append (s, sine (48000.0, 1000.0, -30.0, 20.0));
    run (m, s);
    CHECK_NEAR (m.getReadings().loudnessRange, 10.0, 1.0);

    LoudnessMeter steady;
    steady.prepare (48000.0, 2);
    run (steady, sine (48000.0, 1000.0, -23.0, 20.0));
    CHECK_NEAR (steady.getReadings().loudnessRange, 0.0, 0.2);
}

static void testTruePeak()
{
    // fs/4 with 45 degree phase: samples are +/-0.707 but the reconstructed peak is 1.0.
    LoudnessMeter m;
    m.prepare (44100.0, 2);
    run (m, sine (44100.0, 11025.0, 0.0, 1.0, false, pi / 4.0));
    const double tp = m.getReadings().truePeakDb;
    CHECK (tp > -0.4 && tp < 0.2);  // EBU Tech 3341 tolerance: -0.4 / +0.2 dB
    CHECK (tp > -3.01 + 2.0);       // clearly above the sample peak (-3.01 dB)

    LoudnessMeter loud;
    loud.prepare (48000.0, 2);
    run (loud, sine (48000.0, 1000.0, -6.0, 1.0));
    CHECK_NEAR (loud.getReadings().truePeakDb, -6.0, 0.2);
}

static void testBlockSizeIndependence()
{
    const auto s = sine (48000.0, 1000.0, -18.0, 12.0);
    double reference = 0.0;
    bool first = true;

    for (int block : { 1, 64, 479, 4096 })
    {
        LoudnessMeter m;
        m.prepare (48000.0, 2);
        run (m, s, block);

        if (first)
        {
            reference = m.getReadings().integrated;
            first = false;
        }
        else
        {
            CHECK_NEAR (m.getReadings().integrated, reference, 1.0e-6);
        }
    }
}

static void testSilenceAndReset()
{
    LoudnessMeter m;
    m.prepare (48000.0, 2);
    Signal silence { std::vector<float> (48000 * 5, 0.0f), std::vector<float> (48000 * 5, 0.0f) };
    run (m, silence);
    CHECK (m.getReadings().momentary == silenceLufs);
    CHECK (m.getReadings().integrated == silenceLufs);
    CHECK (m.getReadings().truePeakDb <= -120.0);

    run (m, sine (48000.0, 1000.0, -10.0, 5.0));
    CHECK (m.getReadings().integrated > -20.0);
    m.reset();
    CHECK (m.getReadings().integrated == silenceLufs);
    CHECK (m.getReadings().truePeakDb <= -120.0);
}

static void testNonFiniteInput()
{
    LoudnessMeter m;
    m.prepare (48000.0, 2);
    Signal s = sine (48000.0, 1000.0, -23.0, 2.0);
    s.left[100] = std::nanf ("");
    s.right[200] = INFINITY;
    run (m, s);
    CHECK (std::isfinite (m.getReadings().momentary));
    CHECK (std::isfinite (m.getReadings().truePeakDb));
}


struct Packet
{
    long long a = 0, b = 0, c = 0, d = 0;
};

static void testTripleBuffer()
{
    TripleBuffer<Packet> tb;
    Packet out;
    CHECK (! tb.pull (out));  // nothing published yet

    tb.push ({ 1, 1, 1, 1 });
    tb.push ({ 2, 2, 2, 2 });  // overwrites: only the latest matters
    CHECK (tb.pull (out) && out.a == 2);
    CHECK (! tb.pull (out));

    // Two real threads: the reader must never see a torn value or time going backwards.
    TripleBuffer<Packet> shared;
    std::atomic<bool> stop { false };
    std::thread writer ([&]
    {
        for (long long i = 1; i <= 2000000; ++i)
            shared.push ({ i, i, i, i });
        stop = true;
    });

    long long last = 0;
    bool torn = false, backwards = false;
    while (! stop)
    {
        Packet p;
        if (shared.pull (p))
        {
            if (p.a != p.b || p.b != p.c || p.c != p.d) torn = true;
            if (p.a < last) backwards = true;
            last = p.a;
        }
    }
    writer.join();
    CHECK (! torn);
    CHECK (! backwards);
    CHECK (last > 0);
}

static void testAudioHistory()
{
    AudioHistory h;
    h.prepare (4096);
    CHECK (h.getCapacity() == 4096);

    std::vector<float> l (1000), r (1000), outL (1000), outR (1000);
    for (int i = 0; i < 1000; ++i) { l[(size_t) i] = (float) i; r[(size_t) i] = (float) -i; }
    h.push (l.data(), r.data(), 1000);

    CHECK (h.copyLatest (outL.data(), outR.data(), 10) == 10);
    CHECK (outL[0] == 990.0f && outL[9] == 999.0f && outR[9] == -999.0f);

    std::uint64_t cursor = 0, dropped = 0;
    CHECK (h.readNew (cursor, outL.data(), outR.data(), 400, &dropped) == 400);
    CHECK (outL[0] == 0.0f && outL[399] == 399.0f);
    CHECK (h.readNew (cursor, outL.data(), outR.data(), 1000, &dropped) == 600);
    CHECK (outL[0] == 400.0f && dropped == 0);

    // A reader that falls too far behind skips ahead and reports the loss.
    for (int k = 0; k < 10; ++k) h.push (l.data(), r.data(), 1000);
    std::uint64_t lagging = 0;
    const int got = h.readNew (lagging, outL.data(), outR.data(), 1000, &dropped);
    CHECK (got == 1000);
    CHECK (dropped > 0);
    CHECK (h.getTotalFramesWritten() == 11000);
}

static void testSkinContrast()
{
    for (const auto& skin : skins)
    {
        const auto& c = skin.colors;
        // WCAG 2.2 AA: 4.5:1 for text, 3:1 for components and graphics.
        CHECK (contrastRatio (c.textPrimary, c.surface) >= 4.5);
        CHECK (contrastRatio (c.textSecondary, c.surface) >= 4.5);
        CHECK (contrastRatio (c.textPrimary, c.control) >= 4.5);
        CHECK (contrastRatio (c.textPrimary, c.controlHover) >= 4.5);
        CHECK (contrastRatio (c.textPrimary, c.controlPressed) >= 4.5);
        CHECK (contrastRatio (c.textOnAccent, c.accent) >= 4.5);
        CHECK (contrastRatio (c.controlBorder, c.surface) >= 3.0);
        CHECK (contrastRatio (c.accent, c.surface) >= 3.0);
        CHECK (contrastRatio (c.focus, c.surface) >= 3.0);
        CHECK (contrastRatio (c.warning, c.surface) >= 4.5);
        CHECK (contrastRatio (c.displayText, c.displayBackground) >= 4.5);
        CHECK (contrastRatio (c.displayDim, c.displayBackground) >= 4.5);
        CHECK (contrastRatio (c.traceLeft, c.displayBackground) >= 3.0);
        CHECK (contrastRatio (c.traceRight, c.displayBackground) >= 3.0);
    }
}

static void testFft()
{
    Fft fft (10);  // 1024
    std::vector<float> re (1024, 0.0f), im (1024, 0.0f);
    re[0] = 1.0f;  // impulse -> flat spectrum
    fft.forward (re.data(), im.data());
    bool flat = true;
    for (int k = 0; k < 1024; ++k)
        if (std::abs (re[(size_t) k] - 1.0f) > 1.0e-5f || std::abs (im[(size_t) k]) > 1.0e-5f) flat = false;
    CHECK (flat);

    // A cosine on bin 37 puts all its energy in bins 37 and 1024-37.
    for (int i = 0; i < 1024; ++i) { re[(size_t) i] = (float) std::cos (2.0 * pi * 37.0 * i / 1024.0); im[(size_t) i] = 0.0f; }
    fft.forward (re.data(), im.data());
    CHECK_NEAR (re[37], 512.0, 0.01);
    CHECK_NEAR (re[1024 - 37], 512.0, 0.01);
    CHECK_NEAR (std::abs (re[38]) + std::abs (re[36]) + std::abs (im[37]), 0.0, 0.05);
}

static void testSpectrumAnalyzer()
{
    constexpr double fs = 48000.0;
    constexpr int order = 13, n = 1 << order;
    SpectrumAnalyzer a;
    a.prepare (fs, order, 160);
    CHECK (a.getFftSize() == n);

    // Bin-centred sine, amplitude 0.5 (-6.02 dB) at 85 * fs / 4096 ~ 996 Hz (bin 170 of 8192).
    const double f = 170.0 * fs / (double) n;
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2.0 * pi * f * (double) i / fs));
    a.analyse (x.data());

    const auto& db = a.getBandsDb();
    const int peak = (int) (std::max_element (db.begin(), db.end()) - db.begin());
    CHECK_NEAR (db[(size_t) peak], -6.02, 0.5);
    CHECK (std::abs (a.getBandFrequency (peak) / f - 1.0) < 0.06);
    // Far-away bands are well below the peak.
    CHECK (db[0] < db[(size_t) peak] - 50.0f);
    CHECK (db.back() < db[(size_t) peak] - 50.0f);

    // Two tones: both show up.
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = (float) (0.5 * std::sin (2.0 * pi * 105.46875 * (double) i / fs) + 0.25 * std::sin (2.0 * pi * 9984.375 * (double) i / fs));
    a.analyse (x.data());
    int lowBand = 0, highBand = 0;
    for (int b = 0; b < a.getNumBands(); ++b)
    {
        if (a.getBandFrequency (b) < 1000.0 && a.getBandsDb()[(size_t) b] > a.getBandsDb()[(size_t) lowBand]) lowBand = b;
        if (a.getBandFrequency (b) > 1000.0 && a.getBandsDb()[(size_t) b] > a.getBandsDb()[(size_t) highBand]) highBand = b;
    }
    CHECK (std::abs (a.getBandFrequency (lowBand) / 105.47 - 1.0) < 0.06);
    CHECK (std::abs (a.getBandFrequency (highBand) / 9984.4 - 1.0) < 0.06);
    CHECK (a.getBandsDb()[(size_t) lowBand] > a.getBandsDb()[(size_t) highBand]);  // 0.5 vs 0.25

    // Silence sits on the floor.
    std::fill (x.begin(), x.end(), 0.0f);
    a.analyse (x.data());
    CHECK (*std::max_element (a.getBandsDb().begin(), a.getBandsDb().end()) <= SpectrumAnalyzer::floorDb + 0.01f);

    // Works at other sample rates and keeps the top band below Nyquist.
    for (double rate : { 44100.0, 96000.0 })
    {
        SpectrumAnalyzer b;
        b.prepare (rate, 12, 100);
        CHECK (b.getBandFrequency (99) < rate / 2.0);
    }
}

static void feed (LevelTracker& t, const Signal& s, int block = 512)
{
    for (size_t i = 0; i < s.left.size(); i += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, s.left.size() - i);
        t.process (s.left.data() + i, s.right.data() + i, n);
    }
}

static void testLevelTracker()
{
    LevelTracker t;
    t.prepare (48000.0);

    // Full-scale sine: peak 0 dB, RMS -3.01 dB, VU +15 (0 VU = -18 dBFS RMS).
    feed (t, sine (48000.0, 1000.0, 0.0, 2.0));
    CHECK_NEAR (t.getLeft().peakDb, 0.0, 0.05);
    CHECK_NEAR (t.getLeft().rmsDb, -3.01, 0.1);
    CHECK_NEAR (t.getLeft().vuDb, 14.99, 0.2);
    CHECK_NEAR (t.getCorrelation(), 1.0, 0.01);

    // A sine whose RMS is -18 dBFS reads 0 VU.
    LevelTracker v;
    v.prepare (48000.0);
    feed (v, sine (48000.0, 1000.0, -18.0 + 3.0103, 2.0));
    CHECK_NEAR (v.getLeft().vuDb, 0.0, 0.2);
    CHECK_NEAR (v.getLeft().rmsDb, -18.0, 0.1);

    // VU ballistics: not instant, reaches ~99% (-0.1 dB) after 300 ms.
    LevelTracker b;
    b.prepare (48000.0);
    feed (b, sine (48000.0, 1000.0, -18.0 + 3.0103, 0.1));
    CHECK (b.getLeft().vuDb < -2.0);
    feed (b, sine (48000.0, 1000.0, -18.0 + 3.0103, 0.2));
    CHECK (b.getLeft().vuDb > -0.6 && b.getLeft().vuDb < 0.1);

    // Peak falls after the signal stops; the hold marker stays for 1.5 s.
    feed (t, sine (48000.0, 1000.0, -200.0, 1.0));
    CHECK (t.getLeft().peakDb < -20.0 && t.getLeft().peakDb > -30.0);
    CHECK_NEAR (t.getLeft().holdDb, 0.0, 0.05);
    feed (t, sine (48000.0, 1000.0, -200.0, 1.0));
    CHECK (t.getLeft().holdDb < -3.0);

    // Correlation: inverted = -1, unrelated ~ 0.
    Signal inverted = sine (48000.0, 1000.0, -6.0, 2.0);
    for (auto& x : inverted.right) x = -x;
    LevelTracker c;
    c.prepare (48000.0);
    feed (c, inverted);
    CHECK_NEAR (c.getCorrelation(), -1.0, 0.01);

    Signal unrelated = sine (48000.0, 1000.0, -6.0, 3.0);
    unrelated.right = sine (48000.0, 1370.0, -6.0, 3.0).right;
    LevelTracker u;
    u.prepare (48000.0);
    feed (u, unrelated);
    CHECK (std::abs (u.getCorrelation()) < 0.15);

    // Silence and bad input.
    LevelTracker q;
    q.prepare (48000.0);
    Signal silence { std::vector<float> (4800, 0.0f), std::vector<float> (4800, 0.0f) };
    silence.left[10] = std::nanf ("");
    feed (q, silence);
    CHECK (q.getLeft().rmsDb <= LevelTracker::floorDb + 0.01f);
    CHECK (q.getCorrelation() == 0.0f);
}

static void testDualSpectrum()
{
    constexpr double fs = 48000.0;
    DualSpectrum dual;
    dual.prepare (fs, 256);
    const int n = dual.getLongSize();
    CHECK (n == 16384 && dual.getShortSize() == 2048);

    // Two bass tones 10 Hz apart: only the long analysis separates them.
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = (float) (0.4 * std::sin (2.0 * pi * 40.0 * i / fs) + 0.4 * std::sin (2.0 * pi * 50.0 * i / fs));
    dual.analyse (x.data());

    auto peakNear = [&] (const std::vector<float>& db, const auto& freqOf, double f)
    {
        float best = SpectrumAnalyzer::floorDb;
        for (int b = 0; b < (int) db.size(); ++b)
            if (std::abs (freqOf (b) / f - 1.0) < 0.03) best = std::max (best, db[(size_t) b]);
        return best;
    };
    auto freqOf = [&] (int b) { return dual.getBandFrequency (b); };
    const float p40 = peakNear (dual.getBandsDb(), freqOf, 40.0), p50 = peakNear (dual.getBandsDb(), freqOf, 50.0);
    float valley = 0.0f;
    {
        float lowest = 1.0e9f;
        for (int b = 0; b < dual.getNumBands(); ++b)
            if (dual.getBandFrequency (b) > 43.0 && dual.getBandFrequency (b) < 47.5) lowest = std::min (lowest, dual.getBandsDb()[(size_t) b]);
        valley = lowest;
    }
    CHECK (std::min (p40, p50) - valley > 8.0f);  // a clear dip between the two tones

    SpectrumAnalyzer shortOnly;
    shortOnly.prepare (fs, 11, 256);
    shortOnly.analyse (x.data() + (n - 2048));
    float shortValley = 1.0e9f, shortPeak = -1.0e9f;
    for (int b = 0; b < shortOnly.getNumBands(); ++b)
    {
        const double f = shortOnly.getBandFrequency (b);
        if (f > 43.0 && f < 47.5) shortValley = std::min (shortValley, shortOnly.getBandsDb()[(size_t) b]);
        if (f > 38.0 && f < 52.0) shortPeak = std::max (shortPeak, shortOnly.getBandsDb()[(size_t) b]);
    }
    CHECK (shortPeak - shortValley < 3.0f);  // the short FFT alone cannot separate them

    // A high tone keeps its level; a tone inside the cross-fade zone stays continuous.
    for (double f : { 6000.0, 440.0 })
    {
        for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2.0 * pi * f * i / fs));
        dual.analyse (x.data());
        CHECK_NEAR (peakNear (dual.getBandsDb(), freqOf, f), -6.02, 1.6);
    }
}

static void testPitchDetector()
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        PitchDetector d;
        d.prepare (fs);
        std::vector<float> x ((size_t) d.getRequiredSamples());

        for (double f : { 41.2, 82.41, 110.0, 261.63, 440.0, 1046.5, 1760.0 })
        {
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (0.5 * std::sin (2.0 * pi * f * (double) i / fs));
            const auto r = d.detect (x.data());
            CHECK (r.valid);
            CHECK (std::abs (r.frequencyHz / f - 1.0) < 0.004);
            CHECK (r.confidence > 0.8);
        }
    }

    PitchDetector d;
    d.prepare (48000.0);
    std::vector<float> x ((size_t) d.getRequiredSamples());

    // Harmonically rich (sawtooth 110 Hz) still reports the fundamental.
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.4 * (2.0 * std::fmod (110.0 * (double) i / 48000.0, 1.0) - 1.0));
    auto r = d.detect (x.data());
    CHECK (r.valid && std::abs (r.frequencyHz / 110.0 - 1.0) < 0.01);

    // Silence and noise are rejected.
    std::fill (x.begin(), x.end(), 0.0f);
    CHECK (! d.detect (x.data()).valid);
    unsigned seed = 12345;
    for (auto& v : x) { seed = seed * 1664525u + 1013904223u; v = ((float) (seed >> 8) / 8388608.0f - 1.0f) * 0.3f; }
    CHECK (! d.detect (x.data()).valid);

    // Note naming: A4 = 440, +20 cents, E2, C#5.
    auto a4 = describeNote (440.0);
    CHECK (a4.midi == 69 && a4.pitchClass == 9 && a4.octave == 4 && std::abs (a4.cents) < 0.001);
    CHECK_NEAR (describeNote (440.0 * std::pow (2.0, 20.0 / 1200.0)).cents, 20.0, 0.01);
    auto e2 = describeNote (82.407);
    CHECK (std::string (noteName (e2.pitchClass)) == "E" && e2.octave == 2);
    auto cs5 = describeNote (554.37);
    CHECK (std::string (noteName (cs5.pitchClass)) == "C#" && cs5.octave == 5);
    CHECK (describeNote (27.5).octave == 0);  // A0, lowest piano key
    CHECK_NEAR (describeNote (432.0, 432.0).cents, 0.0, 0.001);  // alternative reference pitch
}

static void testDockLayouts()
{
    CHECK (maxDockSlots == 8);

    for (int count = 1; count <= maxDockSlots; ++count)
    {
        for (int width : { 560, 1000, 1366, 1920, 3840 })
        {
            const int gap = 8, height = 142;
            const auto slots = computeDockSlots (count, width, height, gap);
            int expectedX = 0, minW = 1 << 30, maxW = 0;

            for (int i = 0; i < count; ++i)
            {
                const auto& a = slots[(size_t) i];
                CHECK (a.x == expectedX);          // contiguous, in order, `gap` apart
                CHECK (a.y == 0 && a.h == height);
                CHECK (a.w > 0);
                minW = std::min (minW, a.w);
                maxW = std::max (maxW, a.w);
                expectedX = a.x + a.w + gap;
            }

            CHECK (expectedX - gap == width);      // the row ends exactly at the right edge
            CHECK (maxW - minW <= 1);              // equal widths (to the pixel)
        }
    }

    // Out-of-range counts are clamped.
    const auto zero = computeDockSlots (0, 800, 100, 8);
    CHECK (zero[0].w == 800 && zero[1].w == 0);
    const auto many = computeDockSlots (99, 800, 100, 8);
    CHECK (many[7].w > 0 && many[7].x + many[7].w == 800);
}

static void testLayouts()
{
    const LayoutPreset presets[] = { LayoutPreset::single, LayoutPreset::twoColumns, LayoutPreset::threeSplit, LayoutPreset::quad, LayoutPreset::full5 };

    for (auto preset : presets)
    {
        for (auto size : { std::pair { 560, 200 }, std::pair { 760, 300 }, std::pair { 1600, 900 }, std::pair { 3000, 1400 } })
        {
            const int gap = 8, n = slotCount (preset);
            const auto slots = computeSlots (preset, size.first, size.second, gap);
            long long covered = 0;

            for (int i = 0; i < n; ++i)
            {
                const auto& a = slots[(size_t) i];
                CHECK (a.w > 0 && a.h > 0);
                CHECK (a.x >= 0 && a.y >= 0 && a.x + a.w <= size.first && a.y + a.h <= size.second);
                covered += (long long) a.w * a.h;

                for (int j = i + 1; j < n; ++j)
                {
                    const auto& b = slots[(size_t) j];
                    const bool apart = a.x + a.w + gap <= b.x || b.x + b.w + gap <= a.x || a.y + a.h + gap <= b.y || b.y + b.h + gap <= a.y;
                    CHECK (apart);  // no overlap, and at least `gap` between neighbours
                }
            }

            CHECK ((double) covered >= 0.85 * size.first * size.second);  // the grid uses the space
        }
    }

    CHECK (slotCount (LayoutPreset::single) == 1 && slotCount (LayoutPreset::full5) == 5 && maxSlots == 5);
    CHECK (std::string (layoutName (LayoutPreset::quad)) == "Four");
}

int main()
{
    testKWeightingCoefficients();
    testReferenceTones();
    testSampleRates();
    testGating();
    testLoudnessRange();
    testTruePeak();
    testBlockSizeIndependence();
    testSilenceAndReset();
    testNonFiniteInput();
    testTripleBuffer();
    testAudioHistory();
    testSkinContrast();
    testFft();
    testSpectrumAnalyzer();
    testLevelTracker();
    testLayouts();
    testDockLayouts();
    testDualSpectrum();
    testPitchDetector();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
