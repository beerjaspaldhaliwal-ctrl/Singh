#include "MixEngine.h"
#include "Analyzer.h"
#include "Biquad.h"
#include "ChannelStrip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sam
{
namespace
{
    // clang-format off
    //                     range           tilt   target character                                                  hpf min/max   ratio att  rel  GR   knee mix   deess sat   width mono  balance res    duck   unmask lim
    const RoleProfile kProfiles[] = {
        /* Auto (unused)*/ { 50, 14000,   -2.5f, { { 300, -1.5f, 1.0f }, { 2500, 1.0f, 1.0f } },                 40,  80,  2.5f, 10, 100, 2.0f, 6, 1.0f, false, 0.15f, 1.00f, 0,    -21, false, false, 0,    false },
        /* Lead vocal  */  { 90, 16000,   -2.5f, { { 200, 2.0f, 0.8f }, { 400, -1.5f, 0.6f }, { 3000, 2.0f, 0.8f }, { 12000, 2.0f, 0.8f } },
                                                                                                                  70, 120,  4.0f,  5,  80, 5.0f, 6, 1.0f, true,  0.25f, 1.00f, 0,    -16, true,  false, 0,    false },
        /* Backing vox */  { 140, 16000,  -2.0f, { { 250, 1.0f, 1.0f }, { 3000, 1.0f, 1.0f }, { 10000, 2.0f, 1.0f } },
                                                                                                                 120, 200,  4.0f,  5, 100, 6.0f, 6, 1.0f, true,  0.20f, 1.30f, 150,  -22, true,  false, 1.5f, false },
        /* Kick        */  { 30, 10000,   -4.0f, { { 55, 6.0f, 0.7f }, { 350, -4.0f, 0.8f }, { 3500, 3.0f, 0.7f } },  20,  35,  4.0f, 15,  60, 3.0f, 4, 1.0f, false, 0.35f, 1.00f, 150,  -16, true,  false, 0,    false },
        /* Snare/clap  */  { 100, 16000,  -2.0f, { { 200, 4.0f, 0.6f }, { 500, -2.0f, 0.7f }, { 5000, 2.0f, 1.0f } }, 80, 140,  4.0f,  8,  80, 4.0f, 4, 1.0f, false, 0.30f, 1.00f, 0,    -18, true,  false, 0,    false },
        /* Hats/perc   */  { 300, 20000,   0.0f, { { 800, -3.0f, 1.0f }, { 10000, 2.0f, 1.0f } },                250, 450,  3.0f,  3,  50, 3.0f, 4, 1.0f, false, 0.10f, 1.10f, 200,  -24, true,  false, 0,    false },
        /* Drum bus    */  { 30, 18000,   -3.0f, { { 60, 3.0f, 0.8f }, { 400, -2.0f, 0.8f }, { 4000, 1.5f, 1.0f }, { 12000, 1.0f, 1.0f } },
                                                                                                                  20,  35,  4.0f, 20, 100, 4.0f, 6, 0.6f, false, 0.30f, 1.00f, 120,  -14, true,  false, 0,    false },
        /* Bass        */  { 30, 5000,    -5.0f, { { 60, 3.0f, 1.0f }, { 250, -2.0f, 0.8f }, { 900, 1.5f, 0.8f } },  25,  40,  4.0f, 20, 120, 4.0f, 6, 1.0f, false, 0.40f, 1.00f, 200,  -18, false, true,  0,    false },
        /* 808         */  { 25, 4000,    -6.0f, { { 50, 4.0f, 0.8f }, { 300, -3.0f, 0.8f }, { 1000, 1.0f, 1.0f } }, 20,  30,  3.0f, 25, 150, 3.0f, 6, 1.0f, false, 0.50f, 1.00f, 250,  -16, false, true,  0,    false },
        /* Guitar      */  { 80, 12000,   -2.5f, { { 300, -2.0f, 0.8f }, { 2500, 2.0f, 0.8f } },                 70, 120,  3.0f, 10, 100, 3.0f, 6, 1.0f, false, 0.20f, 1.10f, 150,  -21, true,  false, 2.0f, false },
        /* Keys/piano  */  { 50, 14000,   -2.5f, { { 300, -1.5f, 1.0f }, { 2500, 1.0f, 1.0f }, { 10000, 1.0f, 1.0f } },
                                                                                                                  40,  90,  2.5f, 10, 100, 2.0f, 6, 1.0f, false, 0.15f, 1.10f, 150,  -21, false, false, 2.0f, false },
        /* Synth lead  */  { 150, 16000,  -2.0f, { { 400, -2.0f, 1.0f }, { 3000, 2.0f, 1.0f } },                 100, 180,  3.0f,  8, 100, 3.0f, 6, 1.0f, false, 0.20f, 1.10f, 150,  -20, false, false, 1.5f, false },
        /* Pad/strings */  { 150, 14000,  -3.0f, { { 400, -2.0f, 1.0f }, { 2500, -1.5f, 0.8f }, { 10000, 1.0f, 1.0f } },
                                                                                                                 120, 220,  2.0f, 20, 150, 2.0f, 8, 1.0f, false, 0.15f, 1.30f, 200,  -25, false, false, 3.0f, false },
        /* FX/ambience */  { 200, 18000,  -1.0f, { { 500, -2.0f, 1.0f } },                                      150, 300,  2.0f, 15, 150, 2.0f, 8, 1.0f, false, 0.10f, 1.30f, 200,  -27, false, false, 2.0f, false },
        /* Master      */  { 25, 18000,   -2.5f, { { 60, 2.0f, 1.0f }, { 300, -1.0f, 1.0f }, { 3000, 0.5f, 1.0f }, { 12000, 1.0f, 1.0f } },
                                                                                                                  18,  25,  2.0f, 30, 150, 1.5f, 8, 1.0f, false, 0.15f, 1.05f, 120,  -14, false, false, 0,    true  },
    };

    //                      tonal bumps                                       comp  sat   width master  lpf
    const GenreProfile kGenres[] = {
        /* Trap    */ { { { 50, 2.0f, 1.0f }, { 10000, 1.0f, 1.0f } },                1.10f, 1.10f, 1.05f, -8.0f,  30000 },
        /* Pop     */ { { { 3000, 1.0f, 1.0f }, { 12000, 1.5f, 1.0f } },              1.00f, 0.90f, 1.05f, -9.0f,  30000 },
        /* EDM     */ { { { 60, 2.0f, 1.0f }, { 10000, 1.5f, 1.0f } },                1.20f, 1.10f, 1.15f, -7.5f,  30000 },
        /* Rock    */ { { { 200, 1.0f, 1.0f }, { 2000, 1.0f, 1.0f } },                1.10f, 1.30f, 1.00f, -9.0f,  30000 },
        /* R&B     */ { { { 100, 1.5f, 1.0f }, { 12000, -0.5f, 1.0f } },              0.90f, 1.00f, 1.05f, -10.0f, 30000 },
        /* Lo-Fi   */ { { { 300, 1.0f, 1.0f }, { 12000, -4.0f, 1.2f } },              0.80f, 1.60f, 0.90f, -12.0f, 11000 },
        /* Punjabi */ { { { 120, 1.5f, 1.0f }, { 3500, 1.5f, 1.0f } },                1.10f, 1.10f, 1.05f, -8.5f,  30000 },
        /* Acoustic*/ { { { 250, 0.5f, 1.0f }, { 8000, 1.0f, 1.0f } },                0.70f, 0.60f, 1.00f, -12.0f, 30000 },
    };
    // clang-format on

    float bump (const Bump& b, float f)
    {
        const float x = std::log2 (f / b.freq) / b.widthOct;
        return b.gainDb * std::exp (-0.5f * x * x);
    }

    float balanceOffset (Role r, Genre g)
    {
        switch (g)
        {
            case Genre::HipHopTrap:
                if (r == Role::Bass808 || r == Role::Kick) return 1.0f;
                if (r == Role::LeadVocal) return 0.5f;
                break;
            case Genre::EDM:
                if (r == Role::SynthLead || r == Role::Kick) return 1.5f;
                if (r == Role::LeadVocal) return -1.0f;
                break;
            case Genre::Rock:
                if (r == Role::Guitar) return 2.0f;
                if (r == Role::Snare) return 1.0f;
                break;
            case Genre::Acoustic:
                if (r == Role::Guitar || r == Role::Keys) return 3.0f;
                if (r == Role::Kick || r == Role::Bass808) return -3.0f;
                break;
            case Genre::PunjabiBhangra:
                if (r == Role::HatsPerc || r == Role::DrumBus) return 1.5f; // dhol / tumbi forward
                if (r == Role::LeadVocal) return 1.0f;
                break;
            case Genre::LoFi:
                if (r == Role::Keys) return 2.0f;
                if (r == Role::LeadVocal) return -1.0f;
                break;
            default:
                break;
        }
        return 0.0f;
    }

    float masterTarget (LoudnessTarget t, Genre g)
    {
        switch (t)
        {
            case LoudnessTarget::Streaming14: return -14.0f;
            case LoudnessTarget::Loud11:      return -11.0f;
            case LoudnessTarget::Loud9:       return -9.0f;
            case LoudnessTarget::Club7:       return -7.0f;
            default:                          return genreProfile (g).masterLufs;
        }
    }

    float rangeWeight (float f, float lo, float hi)
    {
        // 1 inside the range, fading to 0 over 1/2 octave outside it
        const float below = std::log2 (lo / f), above = std::log2 (f / hi);
        const float d = std::max (below, above);
        return std::clamp (1.0f - d * 2.0f, 0.0f, 1.0f);
    }
    BiquadCoeffs toCoeffs (const EqBand& b, double sr)
    {
        const auto t = b.type == EqBandType::LowShelf ? FilterType::LowShelf
                     : b.type == EqBandType::HighShelf ? FilterType::HighShelf
                                                       : FilterType::Peak;
        return BiquadCoeffs::make (t, sr, b.freq, b.q, b.gainDb);
    }

    /** Predicted change of (K-weighted) loudness caused by the filters and EQ. */
    float eqLoudnessDeltaDb (const MixSettings& s, const AnalysisResult& a)
    {
        constexpr double sr = 48000.0;
        BiquadCoeffs k1, k2;
        makeKWeighting (sr, k1, k2);
        const auto hp = BiquadCoeffs::make (FilterType::HighPass, sr, s.hpfHz, 0.5411961, 0.0);
        const auto hp2 = BiquadCoeffs::make (FilterType::HighPass, sr, s.hpfHz, 1.3065630, 0.0);
        const auto lp = BiquadCoeffs::make (FilterType::LowPass, sr, s.lpfHz, 0.707, 0.0);

        // stereo processing changes loudness too: width scales the side signal,
        // "mono below" removes it at low frequencies
        auto share = [] (float sideToMidDb)
        {
            const double r = std::pow (10.0, sideToMidDb / 10.0);
            return r / (1.0 + r);
        };
        const double sideAll = share (a.sideToMidDb), sideLow = share (a.lowSideToMidDb);

        double before = 0.0, after = 0.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const double f = kBandCentres[(size_t) b];
            if (f > sr * 0.45)
                continue;
            const double kw = k1.magnitude (f, sr) * k2.magnitude (f, sr);
            const double p = std::pow (10.0, a.bandDb[(size_t) b] / 10.0) * kw * kw;
            double g = hp.magnitude (f, sr) * hp2.magnitude (f, sr);
            if (s.lpfHz < 20000.0f)
                g *= lp.magnitude (f, sr);
            for (const auto& e : s.eq)
                if (e.type != EqBandType::Off)
                    g *= toCoeffs (e, sr).magnitude (f, sr);
            const double side = f < s.monoBelowHz ? sideLow : sideAll;
            const double w = f < s.monoBelowHz ? 0.0 : (double) s.width;
            const double stereo = (1.0 - side) + side * w * w;
            before += p;
            after += p * g * g * stereo;
        }
        return before > 0.0 && after > 0.0 ? (float) (10.0 * std::log10 (after / before)) : 0.0f;
    }
} // namespace

const RoleProfile& roleProfile (Role r)
{
    const int i = std::clamp ((int) r, 0, (int) Role::NumRoles - 1);
    return kProfiles[i];
}

const GenreProfile& genreProfile (Genre g)
{
    const int i = std::clamp ((int) g, 0, (int) Genre::NumGenres - 1);
    return kGenres[i];
}

float targetCurveDb (Role r, Genre g, float f)
{
    const auto& p = roleProfile (r);
    const auto& gp = genreProfile (g);
    float v = p.tiltDbPerOct * std::log2 (f / 1000.0f);
    for (const auto& b : p.targetBumps)
        v += bump (b, f);
    for (const auto& b : gp.tonalBumps)
        v += bump (b, f);
    return v;
}

//==============================================================================
MixSettings MixEngine::compute (const AnalysisResult& a, const Request& req)
{
    MixSettings s;
    const Role role = req.role == Role::Auto ? (a.valid ? a.detectedRole : Role::Keys) : req.role;
    const auto& P = roleProfile (role);
    const auto& G = genreProfile (req.genre);

    s.role = role;
    s.fromAnalysis = a.valid;

    // ---------------------------------------------------------------- filters
    s.hpfHz = std::sqrt (P.hpfMin * P.hpfMax);
    s.lpfHz = std::min (22000.0f, G.lpfHz);
    if (req.genre == Genre::LoFi && role == Role::Master)
        s.lpfHz = 14000.0f;

    std::array<float, kNumBands> smooth {}, weight {}, diff {};
    if (a.valid)
    {
        // 1-octave-ish smoothing of the measured spectrum
        for (int b = 0; b < kNumBands; ++b)
        {
            static const float k[] = { 1, 2, 3, 2, 1 };
            float sum = 0, wsum = 0;
            for (int o = -2; o <= 2; ++o)
            {
                const int i = b + o;
                if (i < 0 || i >= kNumBands)
                    continue;
                sum += k[o + 2] * a.bandDb[(size_t) i];
                wsum += k[o + 2];
            }
            smooth[(size_t) b] = sum / wsum;
        }

        float maxInRange = -200.0f;
        for (int b = 0; b < kNumBands; ++b)
            if (rangeWeight (kBandCentres[(size_t) b], P.rangeLo, P.rangeHi) > 0.5f)
                maxInRange = std::max (maxInRange, smooth[(size_t) b]);

        // Adaptive HPF: just below where the source's real content begins
        for (int b = 0; b < kNumBands; ++b)
        {
            if (smooth[(size_t) b] >= maxInRange - 24.0f)
            {
                s.hpfHz = std::clamp (kBandCentres[(size_t) b] * 0.6f, P.hpfMin, P.hpfMax);
                break;
            }
        }

        float mM = 0, mT = 0, wSum = 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float f = kBandCentres[(size_t) b];
            float w = rangeWeight (f, std::max (P.rangeLo, s.hpfHz * 1.5f), std::min (P.rangeHi, s.lpfHz * 0.7f));
            if (smooth[(size_t) b] < maxInRange - 40.0f)
                w = 0.0f; // nothing there: never boost noise
            weight[(size_t) b] = w;
            mM += w * smooth[(size_t) b];
            mT += w * targetCurveDb (role, req.genre, f);
            wSum += w;
        }
        if (wSum > 0)
        {
            mM /= wSum;
            mT /= wSum;
        }
        for (int b = 0; b < kNumBands; ++b)
            diff[(size_t) b] = (targetCurveDb (role, req.genre, kBandCentres[(size_t) b]) - mT) - (smooth[(size_t) b] - mM);
    }

    // ---------------------------------------------------------------- EQ
    int nb = 0;
    auto addBand = [&] (EqBandType t, float f, float g, float q)
    {
        if (nb < MixSettings::kMaxBands && std::abs (g) >= 0.5f)
            s.eq[(size_t) nb++] = { t, f, g, q };
    };

    if (a.valid)
    {
        struct Region { EqBandType type; float nominal; float q; };
        const Region regions[] = { { EqBandType::LowShelf, 100.0f, 0.7f }, { EqBandType::Peak, 250.0f, 1.0f },
                                   { EqBandType::Peak, 700.0f, 1.0f },      { EqBandType::Peak, 2000.0f, 1.0f },
                                   { EqBandType::Peak, 4500.0f, 1.2f },     { EqBandType::HighShelf, 10000.0f, 0.7f } };

        for (const auto& r : regions)
        {
            if (r.nominal < P.rangeLo * 1.5f || r.nominal > P.rangeHi / 1.5f || r.nominal < s.hpfHz * 2.0f)
                continue; // the filters own this region; never fight them with an EQ boost

            float g = 0.0f, f = r.nominal;
            if (r.type == EqBandType::Peak)
            {
                // centre the band on the strongest deviation within +/- 1/2 octave
                float best = -1.0f, sum = 0.0f, ws = 0.0f;
                int bestB = -1;
                for (int b = 0; b < kNumBands; ++b)
                {
                    const float oct = std::abs (std::log2 (kBandCentres[(size_t) b] / r.nominal));
                    if (oct <= 0.5f && weight[(size_t) b] > 0.0f && std::abs (diff[(size_t) b]) * weight[(size_t) b] > best)
                    {
                        best = std::abs (diff[(size_t) b]) * weight[(size_t) b];
                        bestB = b;
                    }
                }
                if (bestB < 0)
                    continue;
                for (int b = std::max (0, bestB - 1); b <= std::min (kNumBands - 1, bestB + 1); ++b)
                {
                    sum += diff[(size_t) b] * weight[(size_t) b];
                    ws += weight[(size_t) b];
                }
                f = kBandCentres[(size_t) bestB];
                g = ws > 0 ? sum / ws : 0.0f;
            }
            else
            {
                float sum = 0.0f, ws = 0.0f;
                for (int b = 0; b < kNumBands; ++b)
                {
                    const float fc = kBandCentres[(size_t) b];
                    const bool in = r.type == EqBandType::LowShelf ? fc <= r.nominal * 1.3f : fc >= r.nominal * 0.8f;
                    if (in)
                    {
                        sum += diff[(size_t) b] * weight[(size_t) b];
                        ws += weight[(size_t) b];
                    }
                }
                if (ws < 0.75f)
                    continue;
                g = sum / ws;
            }
            if (role == Role::Master)
                addBand (r.type, f, std::clamp (g * 0.5f, -3.0f, 2.5f), r.q); // mastering moves stay gentle
            else
                addBand (r.type, f, std::clamp (g * 0.7f, -6.0f, 4.0f), r.q);
        }

        if (P.resonanceSuppression)
        {
            int added = 0;
            for (int i = 0; i < a.numResonances && added < 2; ++i)
            {
                const float f = a.resonanceHz[(size_t) i];
                if (f < P.rangeLo || f > P.rangeHi)
                    continue;
                addBand (EqBandType::Peak, f, -std::min (5.0f, (a.resonanceExcessDb[(size_t) i] - 4.0f) * 0.8f), 4.0f);
                ++added;
            }
        }
    }
    else
    {
        // No analysis yet: a gentle, genre-aware preset curve for the chosen source
        for (const auto& b : P.targetBumps)
            if (b.freq >= P.rangeLo && b.freq <= P.rangeHi)
                addBand (EqBandType::Peak, b.freq, b.gainDb * 0.4f, std::clamp (0.7f / b.widthOct, 0.5f, 3.0f));
        for (const auto& b : G.tonalBumps)
            if (b.freq >= P.rangeLo && b.freq <= P.rangeHi)
                addBand (b.freq < 500 ? EqBandType::LowShelf : EqBandType::HighShelf, b.freq, b.gainDb * 0.4f, 0.7f);
    }

    // ---------------------------------------------------------------- colour & space
    s.saturation = P.saturation * G.satScale;
    if (a.valid && a.crestDb < 8.0f)
        s.saturation *= 0.5f;
    s.saturation = std::clamp (s.saturation, 0.0f, 1.0f);

    s.width = P.width > 1.0f ? P.width * G.widthScale : P.width;
    s.monoBelowHz = P.monoBelowHz;
    if (a.valid && a.correlation < 0.3f)
    {
        s.width = std::min (s.width, 0.9f);       // phasey source: tighten it
        s.monoBelowHz = std::max (s.monoBelowHz, 150.0f);
    }

    // ---------------------------------------------------------------- gain staging / balance
    // The EQ changes loudness too, so predict its effect on the measured spectrum.
    const float eqDelta = a.valid ? eqLoudnessDeltaDb (s, a) : 0.0f;
    if (a.valid && req.autoBalance && role != Role::Master && a.integratedLufs > -65.0f)
        s.inputTrimDb = std::clamp (P.balanceLufs + balanceOffset (role, req.genre) - a.integratedLufs - eqDelta,
                                    -24.0f, 24.0f);

    s.satRefDb = a.valid ? std::clamp (a.peakDb + s.inputTrimDb + eqDelta, -30.0f, 0.0f) : -6.0f;

    // ---------------------------------------------------------------- dynamics
    s.compRatio = P.compRatio;
    s.compAttackMs = P.compAttackMs;
    s.compReleaseMs = P.compReleaseMs;
    s.compKneeDb = P.compKneeDb;
    s.compMix = P.compMix;

    float gr = P.compTargetGrDb * G.compScale;
    float level = -16.0f;
    if (a.valid)
    {
        level = a.loudRmsDb + s.inputTrimDb + eqDelta;
        if (a.crestDb > 18.0f) gr *= 1.25f;      // very spiky: control it more
        else if (a.crestDb < 8.0f) gr *= 0.5f;   // already dense: leave it breathing
    }
    s.compThreshDb = std::clamp (level - gr / (1.0f - 1.0f / s.compRatio), -50.0f, -2.0f);
    s.compMakeupDb = gr * 0.6f * s.compMix;

    if (P.deess)
    {
        s.deessOn = true;
        s.deessHz = 6500.0f;
        if (a.valid)
        {
            int best = -1;
            for (int b = 0; b < kNumBands; ++b)
                if (kBandCentres[(size_t) b] >= 5000.0f && kBandCentres[(size_t) b] <= 10000.0f
                    && (best < 0 || a.bandDb[(size_t) b] > a.bandDb[(size_t) best]))
                    best = b;
            if (best >= 0)
                s.deessHz = kBandCentres[(size_t) best];
            s.deessThreshDb = std::clamp (a.sibilanceDb + 6.0f, -20.0f, -4.0f);
        }
        s.deessRangeDb = 6.0f;
    }

    s.duckFromKick = P.duckFromKick;
    s.duckDepthDb = P.duckFromKick ? (req.genre == Genre::HipHopTrap || req.genre == Genre::EDM ? 5.0f : 3.5f) : 0.0f;
    s.unmaskDepthDb = P.unmaskDepthDb;
    if (role == Role::LeadVocal)
    {
        s.presenceHz = 3000.0f;
        if (a.valid)
        {
            // strongest band once the natural -3 dB/oct roll-off is compensated
            int best = -1;
            float bestV = -1.0e9f;
            for (int b = 0; b < kNumBands; ++b)
            {
                const float f = kBandCentres[(size_t) b];
                const float v = smooth[(size_t) b] + 3.0f * std::log2 (f / 1000.0f);
                if (f >= 1000.0f && f <= 5000.0f && v > bestV)
                {
                    bestV = v;
                    best = b;
                }
            }
            if (best >= 0)
                s.presenceHz = kBandCentres[(size_t) best];
        }
    }

    // ---------------------------------------------------------------- master loudness
    s.limiterOn = P.limiter;
    s.targetLufs = masterTarget (req.loudness, req.genre);
    if (a.valid && req.autoBalance && role != Role::Master && a.integratedLufs > -65.0f)
    {
        s.calibrationWanted = true;
        s.calibrationTargetLufs = P.balanceLufs + balanceOffset (role, req.genre);
    }
    if (s.limiterOn && a.valid && a.integratedLufs > -65.0f)
    {
        s.calibrationWanted = true;
        s.calibrationTargetLufs = s.targetLufs;
        float drive = s.targetLufs - (a.integratedLufs + eqDelta);
        if (drive > 3.0f)
            drive += 0.7f; // limiting costs a little loudness
        s.limiterDriveDb = std::clamp (drive, -6.0f, 15.0f);
    }

    return s;
}

float MixEngine::calibrate (MixSettings& s, const float* left, const float* right, int numSamples, double sr,
                           int maxIterations)
{
    if (! s.calibrationWanted || numSamples < (int) sr) // need at least a second of audio
        return -70.0f;

    std::vector<float> l ((size_t) numSamples), r ((size_t) numSamples);
    const bool master = s.limiterOn;
    const float target = s.calibrationTargetLufs;

    auto render = [&] (const MixSettings& st)
    {
        std::copy (left, left + numSamples, l.begin());
        std::copy (right != nullptr ? right : left, (right != nullptr ? right : left) + numSamples, r.begin());
        ChannelStrip strip;
        strip.prepare (sr, 512, 2);
        strip.setSettings (st);
        const Macros m;
        for (int i = 0; i < numSamples; i += 512)
        {
            const int n = std::min (512, numSamples - i);
            float* ch[2] = { l.data() + i, r.data() + i };
            strip.process (ch, 2, n, m, LinkInputs {});
        }
        const int skip = std::min (numSamples / 4, strip.latencySamples() + (int) (0.2 * sr));
        return measureIntegratedLufs (l.data() + skip, r.data() + skip, numSamples - skip, sr);
    };

    float measured = render (s);
    float prevGain = 0.0f, prevMeasured = 0.0f;
    bool havePrev = false;

    for (int it = 0; it < maxIterations && std::abs (target - measured) > 0.25f && measured > -69.0f; ++it)
    {
        const float gain = master ? s.limiterDriveDb : s.inputTrimDb;
        float step = target - measured;

        // secant step: limiting makes loudness grow slower than gain
        if (havePrev && std::abs (gain - prevGain) > 0.05f)
        {
            const float slope = (measured - prevMeasured) / (gain - prevGain);
            if (slope > 0.1f && slope < 2.0f)
                step /= slope;
        }
        step = std::clamp (step, -12.0f, 12.0f);
        prevGain = gain;
        prevMeasured = measured;
        havePrev = true;

        if (master)
        {
            s.limiterDriveDb = std::clamp (s.limiterDriveDb + step, -12.0f, 24.0f);
        }
        else
        {
            const float newTrim = std::clamp (s.inputTrimDb + step, -30.0f, 30.0f);
            const float applied = newTrim - s.inputTrimDb;
            s.inputTrimDb = newTrim;
            s.compThreshDb = std::clamp (s.compThreshDb + applied, -60.0f, 0.0f); // keep the same compression
            s.satRefDb = std::clamp (s.satRefDb + applied, -30.0f, 0.0f);
        }
        measured = render (s);
    }

    s.calibrated = std::abs (target - measured) <= 1.0f;
    return measured;
}

std::vector<std::string> MixEngine::describe (const MixSettings& s)
{
    std::vector<std::string> out;
    char buf[160];

    auto fmtHz = [] (float f)
    {
        char b[32];
        if (f >= 1000.0f)
            std::snprintf (b, sizeof (b), "%.1f kHz", f / 1000.0f);
        else
            std::snprintf (b, sizeof (b), "%.0f Hz", f);
        return std::string (b);
    };

    if (std::abs (s.inputTrimDb) >= 0.1f || (s.calibrationWanted && ! s.limiterOn))
    {
        if (s.calibrationWanted && ! s.limiterOn)
            std::snprintf (buf, sizeof (buf), "Balance %+.1f dB -> %.0f LUFS%s", s.inputTrimDb,
                           s.calibrationTargetLufs, s.calibrated ? " (verified)" : "");
        else
            std::snprintf (buf, sizeof (buf), "Balance %+.1f dB", s.inputTrimDb);
        out.emplace_back (buf);
    }
    out.push_back ("High-pass " + fmtHz (s.hpfHz) + " (24 dB/oct)");
    if (s.lpfHz < 20000.0f)
        out.push_back ("Low-pass " + fmtHz (s.lpfHz));

    for (const auto& b : s.eq)
    {
        if (b.type == EqBandType::Off)
            continue;
        const char* kind = b.type == EqBandType::LowShelf ? "Low shelf" : b.type == EqBandType::HighShelf ? "High shelf"
                         : b.q >= 3.0f                    ? "Resonance cut"
                         : b.gainDb < 0                   ? "Cut"
                                                          : "Boost";
        std::snprintf (buf, sizeof (buf), "%s %s %+.1f dB", kind, fmtHz (b.freq).c_str(), b.gainDb);
        out.emplace_back (buf);
    }

    std::snprintf (buf, sizeof (buf), "Comp %.1f:1 @ %.1f dB, %.0f/%.0f ms%s", s.compRatio, s.compThreshDb,
                   s.compAttackMs, s.compReleaseMs, s.compMix < 0.99f ? " (parallel)" : "");
    out.emplace_back (buf);
    if (s.deessOn)
        out.push_back ("De-esser " + fmtHz (s.deessHz));
    std::snprintf (buf, sizeof (buf), "Saturation %.0f%%, width %.0f%%", s.saturation * 100.0f, s.width * 100.0f);
    out.emplace_back (buf);
    if (s.monoBelowHz > 0.0f)
        out.push_back ("Mono below " + fmtHz (s.monoBelowHz));
    if (s.duckFromKick)
    {
        std::snprintf (buf, sizeof (buf), "Ducks %.1f dB under linked kick", s.duckDepthDb);
        out.emplace_back (buf);
    }
    if (s.unmaskDepthDb > 0.0f)
    {
        std::snprintf (buf, sizeof (buf), "Vocal pocket up to -%.1f dB", s.unmaskDepthDb);
        out.emplace_back (buf);
    }
    if (s.limiterOn)
    {
        std::snprintf (buf, sizeof (buf), "Limit %+.1f dB -> %.1f LUFS%s", s.limiterDriveDb, s.targetLufs,
                       s.calibrated ? " (verified)" : "");
        out.emplace_back (buf);
        std::snprintf (buf, sizeof (buf), "Clipper + limiter ceiling %.1f dBFS", s.limiterCeilingDb);
        out.emplace_back (buf);
    }
    return out;
}

} // namespace sam
