// Stand-alone tests for the JUCE-independent DSP / decision engine.
// Build with: cmake -B build -DSAM_BUILD_PLUGIN=OFF && cmake --build build && ctest --test-dir build

#include "../Source/dsp/Analyzer.h"
#include "../Source/dsp/ChannelStrip.h"
#include "../Source/dsp/MixEngine.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace sam;

static int failures = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (! (cond))                                                            \
        {                                                                        \
            std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                       \
    do                                                                                              \
    {                                                                                               \
        const double va = (a), vb = (b);                                                            \
        if (! (std::abs (va - vb) <= (tol)))                                                        \
        {                                                                                           \
            std::printf ("  FAIL %s:%d  %s = %.4f, expected %.4f +/- %.4f\n", __FILE__, __LINE__,  \
                         #a, va, vb, (double) (tol));                                               \
            ++failures;                                                                             \
        }                                                                                           \
    } while (0)

constexpr double SR = 48000.0;

static std::vector<float> sine (double freq, double amp, double seconds)
{
    std::vector<float> v ((size_t) (seconds * SR));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = (float) (amp * std::sin (2.0 * kPi * freq * (double) i / SR));
    return v;
}

static std::vector<float> noise (double amp, double seconds, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    std::vector<float> v ((size_t) (seconds * SR));
    for (auto& x : v)
        x = (float) amp * nd (rng);
    return v;
}

/** Pink noise (Paul Kellet's filter): equal energy per octave, like real music. */
static std::vector<float> pink (double amp, double seconds, unsigned seed = 1)
{
    auto w = noise (1.0, seconds, seed);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (auto& x : w)
    {
        const double white = x;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        x = (float) (amp * 0.11 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362));
        b6 = white * 0.115926;
    }
    return w;
}

static std::vector<float> filtered (std::vector<float> v, FilterType t, double f, double q, double g, int passes = 1)
{
    for (int p = 0; p < passes; ++p)
    {
        Biquad b;
        b.setCoeffs (BiquadCoeffs::make (t, SR, f, q, g));
        for (auto& x : v)
            x = b.process (x);
    }
    return v;
}

static AnalysisResult analyse (const std::vector<float>& l, const std::vector<float>& r, Analyzer* keep = nullptr)
{
    Analyzer local;
    Analyzer& an = keep != nullptr ? *keep : local;
    an.prepare (SR);
    for (size_t i = 0; i < l.size(); i += 512)
    {
        const int n = (int) std::min<size_t> (512, l.size() - i);
        an.process (l.data() + i, r.data() + i, n);
    }
    return an.finalize();
}

static double rmsDb (const std::vector<float>& v, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i)
        s += (double) v[i] * v[i];
    return 10.0 * std::log10 (s / (double) (v.size() - from) + 1e-20);
}

static void runStrip (ChannelStrip& strip, std::vector<float>& l, std::vector<float>& r, const Macros& m,
                      const LinkInputs& link = {})
{
    for (size_t i = 0; i < l.size(); i += 256)
    {
        const int n = (int) std::min<size_t> (256, l.size() - i);
        float* ch[2] = { l.data() + i, r.data() + i };
        strip.process (ch, 2, n, m, link);
    }
}

//==============================================================================
static void testBiquad()
{
    auto c = BiquadCoeffs::make (FilterType::Peak, SR, 1000, 1.0, 6.0);
    CHECK_NEAR (c.magnitudeDb (1000, SR), 6.0, 0.01);
    CHECK_NEAR (c.magnitudeDb (50, SR), 0.0, 0.1);

    auto hs = BiquadCoeffs::make (FilterType::HighShelf, SR, 5000, 0.7, -4.0);
    CHECK_NEAR (hs.magnitudeDb (18000, SR), -4.0, 0.3);
    CHECK_NEAR (hs.magnitudeDb (100, SR), 0.0, 0.05);

    auto hp = BiquadCoeffs::make (FilterType::HighPass, SR, 100, 0.707, 0);
    CHECK_NEAR (hp.magnitudeDb (100, SR), -3.0, 0.1);
    CHECK (hp.magnitudeDb (25, SR) < -20.0);
}

static void testLoudness()
{
    // A 1 kHz sine of amplitude A on both channels reads ~20log10(A) LUFS (BS.1770 calibration)
    auto s = sine (1000, 0.1, 10.0);
    const auto s2 = s; // distinct buffers => treated as a stereo pair
    auto res = analyse (s, s2);
    CHECK (res.valid);
    CHECK_NEAR (res.integratedLufs, -20.0, 0.3);
    CHECK_NEAR (res.peakDb, -20.0, 0.05);
    CHECK_NEAR (res.correlation, 1.0, 1e-3);
    CHECK_NEAR (res.crestDb, 3.01, 0.2);

    LoudnessMeter lm;
    lm.prepare (SR);
    lm.process (s.data(), s2.data(), (int) s.size());
    CHECK_NEAR (lm.shortTermLufs(), -20.0, 0.3);
}

static void testSpectrum()
{
    auto s = sine (1000, 1.0, 5.0);
    auto res = analyse (s, s);
    // full-scale sine: mean square 0.5 -> -3 dB in the 1 kHz band, far less elsewhere
    CHECK_NEAR (res.bandDb[17], -3.0, 0.5);
    CHECK (res.bandDb[10] < -40.0);
    CHECK_NEAR (res.centroidHz, 1000.0, 30.0);
}

static void testClassifier()
{
    // kick-like: 55 Hz decaying bursts, 2 per second
    std::vector<float> kick ((size_t) (8 * SR), 0.0f);
    for (size_t start = 0; start < kick.size(); start += (size_t) (SR / 2))
        for (size_t i = 0; i < (size_t) (0.3 * SR) && start + i < kick.size(); ++i)
        {
            const double t = (double) i / SR;
            kick[start + i] = (float) (0.9 * std::exp (-t * 18.0) * std::sin (2 * kPi * 55 * t));
        }
    auto rk = analyse (kick, kick);
    std::printf ("  kick -> %s (low %.2f crest %.1f)\n", roleName (rk.detectedRole), rk.lowRatio, rk.crestDb);
    CHECK (rk.detectedRole == Role::Kick);

    // sustained 45 Hz tone with a little harmonic content: 808 / bass
    auto sub = sine (45, 0.5, 8.0);
    auto h2 = sine (90, 0.1, 8.0);
    for (size_t i = 0; i < sub.size(); ++i)
        sub[i] += h2[i];
    auto rs = analyse (sub, sub);
    std::printf ("  sub  -> %s\n", roleName (rs.detectedRole));
    CHECK (rs.detectedRole == Role::Bass808 || rs.detectedRole == Role::Bass);

    // bright noise: hats
    auto hats = filtered (noise (0.2, 6.0), FilterType::HighPass, 6000, 0.707, 0, 2);
    auto rh = analyse (hats, hats);
    std::printf ("  hats -> %s (high %.2f)\n", roleName (rh.detectedRole), rh.highRatio);
    CHECK (rh.detectedRole == Role::HatsPerc);

    // wide full-range noise with a pink-ish tilt: a mix
    auto nl = filtered (pink (0.2, 8.0, 3), FilterType::LowShelf, 120, 0.7, 6);
    auto nr = filtered (pink (0.2, 8.0, 4), FilterType::LowShelf, 120, 0.7, 6);
    auto common = filtered (pink (0.2, 8.0, 5), FilterType::LowShelf, 120, 0.7, 6);
    for (size_t i = 0; i < nl.size(); ++i)
    {
        nl[i] = 0.4f * nl[i] + common[i];
        nr[i] = 0.4f * nr[i] + common[i];
    }
    auto rm = analyse (nl, nr);
    std::printf ("  mix  -> %s (corr %.2f low %.2f mid %.2f high %.3f)\n", roleName (rm.detectedRole), rm.correlation,
                 rm.lowRatio, rm.midRatio, rm.highRatio);
    CHECK (rm.detectedRole == Role::Master);
}

static double eqGainAround (const MixSettings& s, double f)
{
    double total = 0.0;
    for (const auto& b : s.eq)
    {
        if (b.type == EqBandType::Off)
            continue;
        auto t = b.type == EqBandType::LowShelf ? FilterType::LowShelf
               : b.type == EqBandType::HighShelf ? FilterType::HighShelf
                                                 : FilterType::Peak;
        total += BiquadCoeffs::make (t, SR, b.freq, b.q, b.gainDb).magnitudeDb (f, SR);
    }
    return total;
}

static void testMixEngineEq()
{
    // A muddy, dull "vocal": pink-ish noise with +8 dB at 300 Hz and highs rolled off
    auto base = filtered (pink (0.1, 10.0, 7), FilterType::LowPass, 3000, 0.707, 0, 2);
    base = filtered (base, FilterType::Peak, 300, 1.0, 8.0);
    base = filtered (base, FilterType::HighPass, 120, 0.707, 0);
    const auto baseR = base; // stereo track carrying a mono vocal, as in FL Studio
    auto res = analyse (base, baseR);

    MixEngine::Request req;
    req.role = Role::LeadVocal;
    req.genre = Genre::Pop;
    const auto s = MixEngine::compute (res, req);

    for (auto& line : MixEngine::describe (s))
        std::printf ("    %s\n", line.c_str());

    CHECK (s.fromAnalysis);
    CHECK (eqGainAround (s, 300) < -1.0);   // removes mud
    CHECK (eqGainAround (s, 10000) > 1.0);  // adds air to the dull top
    CHECK (s.deessOn);
    CHECK (s.hpfHz >= 70.0f && s.hpfHz <= 120.0f);
    CHECK (s.compThreshDb < res.loudRmsDb + s.inputTrimDb); // compressor will actually work
    // auto-balance: with dynamics/colour off, the processed vocal should land near -16 LUFS (pop lead vocal)
    {
        ChannelStrip strip;
        strip.prepare (SR, 256, 2);
        strip.setSettings (s);
        Macros m;
        m.dynamics = 0.0f;
        m.warmth = 0.0f;
        auto l = base, r = base;
        runStrip (strip, l, r, m);
        std::vector<float> tl (l.begin() + (long) SR, l.end());
        const auto tr = tl;
        auto out = analyse (tl, tr);
        std::printf ("  vocal balanced to %.1f LUFS (in %.1f)\n", out.integratedLufs, res.integratedLufs);
        CHECK_NEAR (out.integratedLufs, -16.0, 1.0);
    }

    // no analysis -> sane preset
    AnalysisResult none;
    const auto p = MixEngine::compute (none, req);
    CHECK (! p.fromAnalysis);
    CHECK (p.inputTrimDb == 0.0f);
    for (const auto& b : p.eq)
        CHECK (std::abs (b.gainDb) <= 4.0f);
}

static void testMasterLoudness()
{
    auto nl = filtered (pink (0.6, 12.0, 11), FilterType::LowShelf, 120, 0.7, 6);
    auto nr = filtered (pink (0.6, 12.0, 12), FilterType::LowShelf, 120, 0.7, 6);
    auto res = analyse (nl, nr);

    MixEngine::Request req;
    req.role = Role::Master;
    req.genre = Genre::HipHopTrap;
    req.loudness = LoudnessTarget::Streaming14;
    auto s = MixEngine::compute (res, req);
    CHECK (s.limiterOn);
    CHECK_NEAR (s.targetLufs, -14.0, 0.01);

    ChannelStrip strip;
    strip.prepare (SR, 256, 2);
    strip.setSettings (s);
    Macros m;
    runStrip (strip, nl, nr, m);

    float peak = 0;
    for (size_t i = 0; i < nl.size(); ++i)
        peak = std::max (peak, std::max (std::abs (nl[i]), std::abs (nr[i])));
    std::printf ("  master: in %.1f LUFS, drive %+.1f dB, out peak %.2f dBFS\n", res.integratedLufs, s.limiterDriveDb,
                 gainToDb (peak));
    CHECK (gainToDb (peak) <= -1.0f + 1e-3f);

    // measure the processed loudness (skip the first 2 s settling)
    std::vector<float> tl (nl.begin() + (long) (2 * SR), nl.end()), tr (nr.begin() + (long) (2 * SR), nr.end());
    auto out = analyse (tl, tr);
    std::printf ("  master: out %.1f LUFS (target %.1f)\n", out.integratedLufs, s.targetLufs);
    CHECK_NEAR (out.integratedLufs, s.targetLufs, 2.0);
}

static void testQuietMasterGetsLoud()
{
    // A quiet, dynamic mix must be pushed to the club target by the limiter without exceeding the ceiling
    auto nl = filtered (pink (0.15, 12.0, 41), FilterType::LowShelf, 120, 0.7, 6);
    auto nr = filtered (pink (0.15, 12.0, 42), FilterType::LowShelf, 120, 0.7, 6);
    auto common = filtered (pink (0.15, 12.0, 43), FilterType::LowShelf, 120, 0.7, 6);
    for (size_t i = 0; i < nl.size(); ++i)
    {
        nl[i] = 0.5f * nl[i] + common[i];
        nr[i] = 0.5f * nr[i] + common[i];
    }
    Analyzer an;
    auto res = analyse (nl, nr, &an);

    MixEngine::Request req;
    req.role = Role::Master;
    req.loudness = LoudnessTarget::Club7;
    auto s = MixEngine::compute (res, req);
    const float predicted = s.limiterDriveDb;
    const float calibratedLufs = MixEngine::calibrate (s, an.capturedLeft().data(), an.capturedRight().data(),
                                                       an.capturedSamples(), SR);
    std::printf ("  quiet master: predicted drive %+.1f dB, calibrated %+.1f dB (render says %.2f LUFS)\n", predicted,
                 s.limiterDriveDb, calibratedLufs);
    CHECK (s.calibrated);

    ChannelStrip strip;
    strip.prepare (SR, 256, 2);
    strip.setSettings (s);
    runStrip (strip, nl, nr, Macros {});

    float peak = 0;
    for (size_t i = 0; i < nl.size(); ++i)
        peak = std::max (peak, std::max (std::abs (nl[i]), std::abs (nr[i])));
    std::vector<float> tl (nl.begin() + (long) (2 * SR), nl.end()), tr (nr.begin() + (long) (2 * SR), nr.end());
    auto out = analyse (tl, tr);
    std::printf ("  quiet master: in %.1f LUFS, drive %+.1f dB -> out %.1f LUFS, peak %.2f dBFS, limiter GR %.1f dB\n",
                 res.integratedLufs, s.limiterDriveDb, out.integratedLufs, gainToDb (peak), strip.limiterGrDb.load());
    CHECK (s.limiterDriveDb > 6.0f);
    CHECK (gainToDb (peak) <= -1.0f + 1e-3f);
    CHECK_NEAR (out.integratedLufs, -7.0, 1.0);
}

static void testTrackCalibration()
{
    // A spiky drum-like signal: compression + saturation change its loudness a lot,
    // calibration must still land it on the balance target.
    auto x = pink (0.4, 10.0, 51);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = std::fmod ((double) i / SR, 0.25);
        x[i] *= (float) (0.05 + std::exp (-t * 30.0));
    }
    const auto xr = x;
    Analyzer an;
    auto res = analyse (x, xr, &an);

    MixEngine::Request req;
    req.role = Role::Snare;
    auto s = MixEngine::compute (res, req);
    const float before = s.inputTrimDb;
    const float measured = MixEngine::calibrate (s, an.capturedLeft().data(), an.capturedRight().data(),
                                                 an.capturedSamples(), SR);
    std::printf ("  snare: trim %+.1f -> %+.1f dB, renders at %.2f LUFS (target %.1f)\n", before, s.inputTrimDb,
                 measured, s.calibrationTargetLufs);
    CHECK (s.calibrated);
    CHECK_NEAR (measured, s.calibrationTargetLufs, 0.3);
}

static void testLimiter()
{
    Limiter lim;
    lim.prepare (SR, 2, 5.0f);
    auto l = noise (0.8, 3.0, 21), r = noise (0.8, 3.0, 22);
    const float ceil = dbToGain (-1.0f);
    for (size_t i = 0; i < l.size(); i += 333)
    {
        const int n = (int) std::min<size_t> (333, l.size() - i);
        float* ch[2] = { l.data() + i, r.data() + i };
        lim.process (ch, 2, n, true, ceil, 100.0f);
    }
    float peak = 0;
    for (size_t i = 0; i < l.size(); ++i)
        peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));
    CHECK (peak <= ceil + 1e-6f);

    // inactive limiter == pure delay of latency() samples
    Limiter d;
    d.prepare (SR, 1, 5.0f);
    auto x = noise (0.5, 0.1, 5);
    auto y = x;
    float* ch[1] = { y.data() };
    d.process (ch, 1, (int) y.size(), false, 1.0f, 100.0f);
    const int lat = d.latency();
    CHECK (lat == 239);
    double err = 0;
    for (size_t i = (size_t) lat; i < y.size(); ++i)
        err = std::max (err, (double) std::abs (y[i] - x[i - (size_t) lat]));
    CHECK (err < 1e-7);
}

static void testStripTransparentAndStable()
{
    // Amount 0 must be (nearly) transparent
    MixEngine::Request req;
    req.role = Role::LeadVocal;
    auto s = MixEngine::compute (AnalysisResult {}, req);

    ChannelStrip strip;
    strip.prepare (SR, 256, 2);
    strip.setSettings (s);
    Macros m;
    m.amount = 0.0f;
    auto l = sine (1000, 0.3, 2.0), r = sine (1000, 0.3, 2.0);
    const double inDb = rmsDb (l);
    runStrip (strip, l, r, m);
    CHECK_NEAR (rmsDb (l, 4800), inDb, 0.1);

    // Full processing on hot noise: finite output, compressor engaged
    ChannelStrip s2;
    s2.prepare (SR, 256, 2);
    auto vs = MixEngine::compute (AnalysisResult {}, req);
    s2.setSettings (vs);
    Macros full;
    auto nl = noise (0.3, 3.0, 8), nr = noise (0.3, 3.0, 9);
    runStrip (s2, nl, nr, full);
    bool finite = true;
    for (size_t i = 0; i < nl.size(); ++i)
        finite &= std::isfinite (nl[i]) && std::isfinite (nr[i]);
    CHECK (finite);
    CHECK (s2.compGrDb.load() > 1.0f);

    // Mix = 0 -> exact dry signal delayed by the plugin latency
    ChannelStrip s3;
    s3.prepare (SR, 256, 2);
    s3.setSettings (vs);
    Macros dry;
    dry.mix = 0.0f;
    auto dl = noise (0.3, 0.5, 10), dr = dl;
    const auto orig = dl;
    runStrip (s3, dl, dr, dry);
    const int lat = s3.latencySamples();
    double err = 0;
    for (size_t i = (size_t) lat; i < dl.size(); ++i)
        err = std::max (err, (double) std::abs (dl[i] - orig[i - (size_t) lat]));
    CHECK (err < 1e-6);
}

static void testDucking()
{
    MixEngine::Request req;
    req.role = Role::Bass808;
    auto s = MixEngine::compute (AnalysisResult {}, req);
    CHECK (s.duckFromKick);

    ChannelStrip strip;
    strip.prepare (SR, 256, 2);
    strip.setSettings (s);
    Macros m;
    auto l = sine (50, 0.3, 1.0), r = l;
    LinkInputs link;
    link.kickEnvelope = 1.0f;
    runStrip (strip, l, r, m, link);
    CHECK_NEAR (strip.duckGrDb.load(), s.duckDepthDb, 0.2);
}

static void testSerialisation()
{
    auto s = sine (440, 0.2, 3.0);
    auto res = analyse (s, s);
    std::vector<float> buf (AnalysisResult::kSerialisedSize);
    res.serialise (buf.data());
    AnalysisResult back;
    CHECK (back.deserialise (buf.data(), (int) buf.size()));
    CHECK (back.valid == res.valid);
    CHECK_NEAR (back.integratedLufs, res.integratedLufs, 1e-6);
    CHECK_NEAR (back.bandDb[20], res.bandDb[20], 1e-6);
    CHECK (back.detectedRole == res.detectedRole);
    CHECK (! back.deserialise (buf.data(), 3));
}

static void testAllRolesAndGenres()
{
    auto nl = filtered (noise (0.1, 4.0, 31), FilterType::LowShelf, 200, 0.7, 6);
    auto res = analyse (nl, nl);
    for (int r = 0; r < (int) Role::NumRoles; ++r)
        for (int g = 0; g < (int) Genre::NumGenres; ++g)
        {
            MixEngine::Request req;
            req.role = (Role) r;
            req.genre = (Genre) g;
            auto s = MixEngine::compute (res, req);
            CHECK (s.role != Role::Auto);
            CHECK (std::isfinite (s.compThreshDb) && s.compRatio >= 1.0f);
            CHECK (s.hpfHz >= 10.0f && s.hpfHz < 1000.0f);
            for (const auto& b : s.eq)
                CHECK (std::isfinite (b.gainDb) && b.gainDb <= 4.0f && b.gainDb >= -6.0f);
        }
}

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] = {
        { "biquad", testBiquad },
        { "loudness", testLoudness },
        { "spectrum", testSpectrum },
        { "classifier", testClassifier },
        { "mix engine eq", testMixEngineEq },
        { "master loudness", testMasterLoudness },
        { "quiet master -> club loudness", testQuietMasterGetsLoud },
        { "track calibration", testTrackCalibration },
        { "limiter", testLimiter },
        { "strip transparency/stability", testStripTransparentAndStable },
        { "kick ducking", testDucking },
        { "serialisation", testSerialisation },
        { "all roles x genres", testAllRolesAndGenres },
    };

    for (const auto& [name, fn] : tests)
    {
        std::printf ("[%s]\n", name);
        fn();
    }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
