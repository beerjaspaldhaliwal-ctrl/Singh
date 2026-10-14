#include "Analyzer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sam
{
const std::array<float, kNumBands> kBandCentres = {
    20.f,   25.f,   31.5f,  40.f,   50.f,   63.f,   80.f,    100.f,   125.f,   160.f,   200.f,
    250.f,  315.f,  400.f,  500.f,  630.f,  800.f,  1000.f,  1250.f,  1600.f,  2000.f,  2500.f,
    3150.f, 4000.f, 5000.f, 6300.f, 8000.f, 10000.f, 12500.f, 16000.f, 20000.f
};

const char* roleName (Role r)
{
    switch (r)
    {
        case Role::Auto:         return "Auto Detect";
        case Role::LeadVocal:    return "Lead Vocal";
        case Role::BackingVocal: return "Backing Vocals";
        case Role::Kick:         return "Kick";
        case Role::Snare:        return "Snare / Clap";
        case Role::HatsPerc:     return "Hi-Hats / Perc";
        case Role::DrumBus:      return "Drum Bus";
        case Role::Bass:         return "Bass";
        case Role::Bass808:      return "808";
        case Role::Guitar:       return "Guitar";
        case Role::Keys:         return "Keys / Piano";
        case Role::SynthLead:    return "Synth Lead";
        case Role::Pad:          return "Pad / Strings";
        case Role::FX:           return "FX / Ambience";
        case Role::Master:       return "Master / Mix Bus";
        default:                 return "?";
    }
}

const char* genreName (Genre g)
{
    switch (g)
    {
        case Genre::HipHopTrap:     return "Hip-Hop / Trap";
        case Genre::Pop:            return "Pop";
        case Genre::EDM:            return "EDM";
        case Genre::Rock:           return "Rock";
        case Genre::RnB:            return "R&B";
        case Genre::LoFi:           return "Lo-Fi";
        case Genre::PunjabiBhangra: return "Punjabi / Bhangra";
        case Genre::Acoustic:       return "Acoustic";
        default:                    return "?";
    }
}

const char* loudnessTargetName (LoudnessTarget t)
{
    switch (t)
    {
        case LoudnessTarget::GenreDefault: return "Genre Default";
        case LoudnessTarget::Streaming14:  return "Streaming (-14 LUFS)";
        case LoudnessTarget::Loud11:       return "Balanced (-11 LUFS)";
        case LoudnessTarget::Loud9:        return "Loud (-9 LUFS)";
        case LoudnessTarget::Club7:        return "Club (-7 LUFS)";
        default:                           return "?";
    }
}

//==============================================================================
void AnalysisResult::serialise (float* out) const
{
    int i = 0;
    out[i++] = 1.0f; // version
    out[i++] = valid ? 1.0f : 0.0f;
    out[i++] = seconds;
    out[i++] = integratedLufs;
    out[i++] = loudRmsDb;
    out[i++] = peakDb;
    out[i++] = crestDb;
    out[i++] = correlation;
    out[i++] = sideToMidDb;
    out[i++] = lowSideToMidDb;
    out[i++] = centroidHz;
    out[i++] = lowRatio;
    out[i++] = midRatio;
    out[i++] = highRatio;
    out[i++] = sibilanceDb;
    out[i++] = (float) numResonances;
    out[i++] = (float) detectedRole;
    for (float b : bandDb)
        out[i++] = b;
    for (int r = 0; r < 3; ++r)
    {
        out[i++] = resonanceHz[(size_t) r];
        out[i++] = resonanceExcessDb[(size_t) r];
    }
}

bool AnalysisResult::deserialise (const float* in, int size)
{
    if (size != kSerialisedSize || in[0] != 1.0f)
        return false;

    int i = 1;
    valid = in[i++] > 0.5f;
    seconds = in[i++];
    integratedLufs = in[i++];
    loudRmsDb = in[i++];
    peakDb = in[i++];
    crestDb = in[i++];
    correlation = in[i++];
    sideToMidDb = in[i++];
    lowSideToMidDb = in[i++];
    centroidHz = in[i++];
    lowRatio = in[i++];
    midRatio = in[i++];
    highRatio = in[i++];
    sibilanceDb = in[i++];
    numResonances = std::clamp ((int) in[i++], 0, 3);
    detectedRole = (Role) std::clamp ((int) in[i++], 1, (int) Role::NumRoles - 1);
    for (auto& b : bandDb)
        b = in[i++];
    for (int r = 0; r < 3; ++r)
    {
        resonanceHz[(size_t) r] = in[i++];
        resonanceExcessDb[(size_t) r] = in[i++];
    }
    return true;
}

//==============================================================================
void LoudnessMeter::prepare (double sampleRate)
{
    sr_ = sampleRate;
    BiquadCoeffs s1, s2;
    makeKWeighting (sr_, s1, s2);
    kL1_.setCoeffs (s1);
    kR1_.setCoeffs (s1);
    kL2_.setCoeffs (s2);
    kR2_.setCoeffs (s2);
    blockLen_ = std::max (1, (int) std::lround (sr_ * 0.1));
    reset();
}

void LoudnessMeter::reset()
{
    kL1_.reset();
    kL2_.reset();
    kR1_.reset();
    kR2_.reset();
    blockPos_ = 0;
    blockSum_ = 0.0;
    blocks_.fill (0.0);
    blockIndex_ = blocksFilled_ = 0;
    shortTerm_ = momentary_ = -70.0f;
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    const bool stereo = right != nullptr && right != left;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = kL2_.process (kL1_.process (left[i]));
        double ms = l * l;
        if (stereo)
        {
            const double r = kR2_.process (kR1_.process (right[i]));
            ms += r * r;
        }
        blockSum_ += ms;

        if (++blockPos_ >= blockLen_)
        {
            blocks_[(size_t) blockIndex_] = blockSum_ / blockLen_;
            blockIndex_ = (blockIndex_ + 1) % (int) blocks_.size();
            blocksFilled_ = std::min (blocksFilled_ + 1, (int) blocks_.size());
            blockPos_ = 0;
            blockSum_ = 0.0;

            auto lufsOf = [] (double meanSquare)
            { return meanSquare > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -70.0f; };

            double sumAll = 0.0, sumLast4 = 0.0;
            for (int b = 0; b < blocksFilled_; ++b)
            {
                const int idx = (blockIndex_ - 1 - b + (int) blocks_.size()) % (int) blocks_.size();
                sumAll += blocks_[(size_t) idx];
                if (b < 4)
                    sumLast4 += blocks_[(size_t) idx];
            }
            shortTerm_ = std::max (-70.0f, lufsOf (sumAll / blocksFilled_));
            momentary_ = std::max (-70.0f, lufsOf (sumLast4 / std::min (4, blocksFilled_)));
        }
    }
}

//==============================================================================
Analyzer::Analyzer()
{
    window_.resize (kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window_[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (kFftSize - 1)));

    ring_.assign (kFftSize, 0.0f);
    fftBuf_.resize (kFftSize);
    powerAccum_.assign (kFftSize / 2 + 1, 0.0);
    loudnessBlocks_.reserve (kMaxLoudnessBlocks);
}

void Analyzer::prepare (double sampleRate)
{
    sr_ = sampleRate;
    rmsLen_ = std::max (1, (int) std::lround (sr_ * 0.05));
    blockLen_ = std::max (1, (int) std::lround (sr_ * 0.1));
    crestLen_ = std::max (1, (int) std::lround (sr_));

    BiquadCoeffs s1, s2;
    makeKWeighting (sr_, s1, s2);
    kL1_.setCoeffs (s1);
    kR1_.setCoeffs (s1);
    kL2_.setCoeffs (s2);
    kR2_.setCoeffs (s2);

    const auto lp = BiquadCoeffs::make (FilterType::LowPass, sr_, 150.0, 0.707, 0.0);
    lowM_.setCoeffs (lp);
    lowS_.setCoeffs (lp);

    capL_.assign ((size_t) (kCaptureSeconds * sr_), 0.0f);
    capR_.assign ((size_t) (kCaptureSeconds * sr_), 0.0f);
    winL_.assign ((size_t) rmsLen_, 0.0f);
    winR_.assign ((size_t) rmsLen_, 0.0f);

    reset();
}

void Analyzer::reset()
{
    std::fill (ring_.begin(), ring_.end(), 0.0f);
    std::fill (powerAccum_.begin(), powerAccum_.end(), 0.0);
    ringPos_ = hopCounter_ = 0;
    fftFrames_ = 0;

    rmsPos_ = 0;
    rmsSum_ = 0.0;
    hist_.fill (0);
    activeWindows_ = 0;

    kL1_.reset();
    kL2_.reset();
    kR1_.reset();
    kR2_.reset();
    blockPos_ = 0;
    blockK_ = 0.0;
    lastSubBlocks_.fill (0.0);
    subBlockCount_ = 0;
    loudnessBlocks_.clear();
    crestPos_ = 0;
    crestMs_ = crestPeak_ = 0.0;
    crestSum_ = 0.0;
    crestCount_ = 0;

    sLL_ = sRR_ = sLR_ = sMM_ = sSS_ = sLowMM_ = sLowSS_ = 0.0;
    lowM_.reset();
    lowS_.reset();
    peak_ = 0.0;
    capPos_ = 0;
}

void Analyzer::process (const float* left, const float* right, int numSamples)
{
    if (right == nullptr)
        right = left;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i];
        const double r = right[i];
        const double m = 0.5 * (l + r);
        const double s = 0.5 * (l - r);

        peak_ = std::max (peak_, std::max (std::abs (l), std::abs (r)));

        // stereo statistics (silence contributes nothing, so no gate is needed)
        sLL_ += l * l;
        sRR_ += r * r;
        sLR_ += l * r;
        sMM_ += m * m;
        sSS_ += s * s;
        const double lm = lowM_.process ((float) m);
        const double ls = lowS_.process ((float) s);
        sLowMM_ += lm * lm;
        sLowSS_ += ls * ls;

        // spectrum
        ring_[(size_t) ringPos_] = (float) m;
        ringPos_ = (ringPos_ + 1) % kFftSize;
        if (++hopCounter_ >= kFftSize / 2)
        {
            hopCounter_ = 0;
            runFft();
        }

        // 50 ms RMS windows
        const double ms = 0.5 * (l * l + r * r);
        if (rmsPos_ < (int) winL_.size())
        {
            winL_[(size_t) rmsPos_] = (float) l;
            winR_[(size_t) rmsPos_] = (float) r;
        }
        rmsSum_ += ms;
        if (++rmsPos_ >= rmsLen_)
            endRmsWindow();

        // 100 ms blocks: crest + K-weighted loudness
        const double kl = kL2_.process (kL1_.process ((float) l));
        const double kr = kR2_.process (kR1_.process ((float) r));
        blockK_ += kl * kl + (right == left ? 0.0 : kr * kr);
        if (++blockPos_ >= blockLen_)
            endLoudnessBlock();

        crestMs_ += ms;
        crestPeak_ = std::max (crestPeak_, std::max (std::abs (l), std::abs (r)));
        if (++crestPos_ >= crestLen_)
        {
            const double meanSq = crestMs_ / crestLen_;
            if (meanSq > 1.0e-6) // > -60 dBFS
            {
                crestSum_ += 20.0 * std::log10 (std::max (crestPeak_, 1.0e-9)) - 10.0 * std::log10 (meanSq);
                ++crestCount_;
            }
            crestPos_ = 0;
            crestMs_ = crestPeak_ = 0.0;
        }
    }
}

void Analyzer::endRmsWindow()
{
    const double meanSq = rmsSum_ / rmsLen_;
    const double db = meanSq > 1.0e-12 ? 10.0 * std::log10 (meanSq) : -120.0;
    const int bin = std::clamp ((int) ((db + 120.0) * 2.0), 0, kHistBins - 1);
    hist_[(size_t) bin]++;
    if (db > -60.0)
    {
        ++activeWindows_;
        const int n = std::min (rmsPos_, (int) capL_.size() - capPos_);
        if (n > 0)
        {
            std::copy (winL_.begin(), winL_.begin() + n, capL_.begin() + capPos_);
            std::copy (winR_.begin(), winR_.begin() + n, capR_.begin() + capPos_);
            capPos_ += n;
        }
    }
    rmsPos_ = 0;
    rmsSum_ = 0.0;
}

void Analyzer::endLoudnessBlock()
{
    // shift the 100 ms sub-blocks and form a 400 ms momentary block
    for (int i = 3; i > 0; --i)
        lastSubBlocks_[(size_t) i] = lastSubBlocks_[(size_t) i - 1];
    lastSubBlocks_[0] = blockK_ / blockLen_;
    if (++subBlockCount_ >= 4 && (int) loudnessBlocks_.size() < kMaxLoudnessBlocks)
    {
        const double z = (lastSubBlocks_[0] + lastSubBlocks_[1] + lastSubBlocks_[2] + lastSubBlocks_[3]) * 0.25;
        loudnessBlocks_.push_back (z > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (z)) : -120.0f);
    }

    blockPos_ = 0;
    blockK_ = 0.0;
}

void Analyzer::runFft()
{
    double energy = 0.0;
    for (int i = 0; i < kFftSize; ++i)
    {
        const float x = ring_[(size_t) ((ringPos_ + i) % kFftSize)];
        energy += (double) x * x;
        fftBuf_[(size_t) i] = { x * (double) window_[(size_t) i], 0.0 };
    }

    if (energy / kFftSize < 1.0e-6) // ignore frames quieter than -60 dBFS
        return;

    fft_.perform (fftBuf_.data());
    for (size_t k = 0; k < powerAccum_.size(); ++k)
        powerAccum_[k] += std::norm (fftBuf_[k]);
    ++fftFrames_;
}

//==============================================================================
AnalysisResult Analyzer::finalize() const
{
    AnalysisResult res;
    res.seconds = (float) activeSeconds();
    if (fftFrames_ == 0 || activeWindows_ < 10)
        return res; // not enough signal

    res.valid = true;
    res.peakDb = gainToDb ((float) peak_);

    // ---- spectrum: normalise so sum of bins == mean-square of the signal
    double winSq = 0.0;
    for (float w : window_)
        winSq += (double) w * w;
    const size_t numBins = powerAccum_.size();
    std::vector<double> p (numBins);
    for (size_t k = 0; k < numBins; ++k)
        p[k] = powerAccum_[k] * 2.0 / ((double) kFftSize * winSq * (double) fftFrames_);

    const double binHz = sr_ / kFftSize;

    for (int b = 0; b < kNumBands; ++b)
    {
        const double lo = kBandCentres[(size_t) b] * std::pow (2.0, -1.0 / 6.0);
        const double hi = kBandCentres[(size_t) b] * std::pow (2.0, 1.0 / 6.0);
        double sum = 0.0;
        for (size_t k = 1; k < numBins; ++k)
        {
            const double bl = ((double) k - 0.5) * binHz, bh = ((double) k + 0.5) * binHz;
            const double overlap = std::min (bh, hi) - std::max (bl, lo);
            if (overlap > 0.0)
                sum += p[k] * overlap / binHz;
        }
        res.bandDb[(size_t) b] = sum > 1.0e-14 ? (float) (10.0 * std::log10 (sum)) : -140.0f;
    }

    double total = 0.0, low = 0.0, mid = 0.0, high = 0.0, sib = 0.0, centroidNum = 0.0;
    for (size_t k = 1; k < numBins; ++k)
    {
        const double f = (double) k * binHz;
        total += p[k];
        centroidNum += p[k] * f;
        if (f < 150.0) low += p[k];
        if (f >= 300.0 && f < 4000.0) mid += p[k];
        if (f >= 6000.0) high += p[k];
        if (f >= 5000.0 && f < 10000.0) sib += p[k];
    }
    total = std::max (total, 1.0e-14);
    res.lowRatio = (float) (low / total);
    res.midRatio = (float) (mid / total);
    res.highRatio = (float) (high / total);
    res.centroidHz = (float) (centroidNum / total);
    res.sibilanceDb = (float) (10.0 * std::log10 (std::max (sib, 1.0e-14) / total));

    // ---- resonances: fine (1/12 oct) vs broad (1 oct) smoothed spectrum
    {
        auto smoothAt = [&] (double f, double octaves)
        {
            const double lo = f * std::pow (2.0, -octaves * 0.5), hi = f * std::pow (2.0, octaves * 0.5);
            const size_t k0 = (size_t) std::max (1.0, std::floor (lo / binHz));
            const size_t k1 = (size_t) std::min ((double) numBins - 1, std::ceil (hi / binHz));
            double s = 0.0;
            for (size_t k = k0; k <= k1; ++k)
                s += p[k];
            return 10.0 * std::log10 (std::max (s / (double) (k1 - k0 + 1), 1.0e-16));
        };

        struct Cand { double f, excess; };
        std::vector<Cand> cands;
        const int steps = 96; // 1/16-oct grid over 200 Hz .. 6.4 kHz (5 octaves)
        std::vector<double> excess ((size_t) steps), freqs ((size_t) steps);
        for (int i = 0; i < steps; ++i)
        {
            const double f = 200.0 * std::pow (2.0, i / 16.0);
            freqs[(size_t) i] = f;
            excess[(size_t) i] = smoothAt (f, 1.0 / 12.0) - smoothAt (f, 1.0);
        }
        for (int i = 1; i < steps - 1; ++i)
            if (excess[(size_t) i] > 6.0 && excess[(size_t) i] >= excess[(size_t) i - 1]
                && excess[(size_t) i] >= excess[(size_t) i + 1])
                cands.push_back ({ freqs[(size_t) i], excess[(size_t) i] });

        std::sort (cands.begin(), cands.end(), [] (auto& a, auto& b) { return a.excess > b.excess; });
        for (const auto& c : cands)
        {
            if (res.numResonances >= 3)
                break;
            bool tooClose = false;
            for (int r = 0; r < res.numResonances; ++r)
                tooClose |= std::abs (std::log2 (c.f / res.resonanceHz[(size_t) r])) < 1.0 / 3.0;
            if (tooClose)
                continue;
            res.resonanceHz[(size_t) res.numResonances] = (float) c.f;
            res.resonanceExcessDb[(size_t) res.numResonances] = (float) c.excess;
            ++res.numResonances;
        }
    }

    // ---- level statistics
    {
        long active = 0;
        for (int b = 120; b < kHistBins; ++b) // > -60 dBFS
            active += hist_[(size_t) b];
        long cum = 0;
        res.loudRmsDb = -60.0f;
        for (int b = 120; b < kHistBins; ++b)
        {
            cum += hist_[(size_t) b];
            if (active > 0 && (double) cum >= 0.9 * (double) active)
            {
                res.loudRmsDb = (float) (b * 0.5 - 120.0 + 0.25);
                break;
            }
        }
    }

    if (crestCount_ > 0)
        res.crestDb = (float) (crestSum_ / (double) crestCount_);
    else if (crestMs_ > 0.0 && crestPos_ > 0) // less than a second analysed
        res.crestDb = (float) (20.0 * std::log10 (std::max (crestPeak_, 1.0e-9))
                               - 10.0 * std::log10 (std::max (crestMs_ / crestPos_, 1.0e-12)));

    // gated integrated loudness (BS.1770-4)
    {
        auto meanOf = [] (const std::vector<float>& v, float gate)
        {
            double sum = 0.0;
            long n = 0;
            for (float l : v)
                if (l > gate)
                {
                    sum += std::pow (10.0, (l + 0.691) / 10.0);
                    ++n;
                }
            return n > 0 ? (float) (-0.691 + 10.0 * std::log10 (sum / (double) n)) : -70.0f;
        };
        const float ungated = meanOf (loudnessBlocks_, -70.0f);
        res.integratedLufs = std::max (-70.0f, meanOf (loudnessBlocks_, std::max (-70.0f, ungated - 10.0f)));
    }

    // stereo
    {
        const double denom = std::sqrt (sLL_ * sRR_);
        res.correlation = denom > 1.0e-12 ? (float) (sLR_ / denom) : 1.0f;
        auto ratioDb = [] (double s, double m)
        { return (float) std::clamp (10.0 * std::log10 ((s + 1.0e-14) / (m + 1.0e-14)), -60.0, 30.0); };
        res.sideToMidDb = ratioDb (sSS_, sMM_);
        res.lowSideToMidDb = ratioDb (sLowSS_, sLowMM_);
    }

    res.detectedRole = classify (res);
    return res;
}

float measureIntegratedLufs (const float* left, const float* right, int numSamples, double sampleRate)
{
    BiquadCoeffs s1, s2;
    makeKWeighting (sampleRate, s1, s2);
    Biquad l1, l2, r1, r2;
    l1.setCoeffs (s1);
    l2.setCoeffs (s2);
    r1.setCoeffs (s1);
    r2.setCoeffs (s2);

    const int sub = std::max (1, (int) std::lround (sampleRate * 0.1));
    std::vector<double> subBlocks;
    subBlocks.reserve ((size_t) (numSamples / sub + 1));
    double acc = 0.0;
    int pos = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const double a = l2.process (l1.process (left[i]));
        acc += a * a;
        if (right != nullptr)
        {
            const double b = r2.process (r1.process (right[i]));
            acc += b * b;
        }
        if (++pos >= sub)
        {
            subBlocks.push_back (acc / sub);
            acc = 0.0;
            pos = 0;
        }
    }

    std::vector<double> blocks;
    for (size_t i = 3; i < subBlocks.size(); ++i)
        blocks.push_back ((subBlocks[i] + subBlocks[i - 1] + subBlocks[i - 2] + subBlocks[i - 3]) * 0.25);

    auto gatedMean = [&] (double gateLufs)
    {
        double sum = 0.0;
        long n = 0;
        for (double z : blocks)
            if (z > 1.0e-12 && -0.691 + 10.0 * std::log10 (z) > gateLufs)
            {
                sum += z;
                ++n;
            }
        return n > 0 ? -0.691 + 10.0 * std::log10 (sum / (double) n) : -70.0;
    };
    const double ungated = gatedMean (-70.0);
    return (float) std::max (-70.0, gatedMean (std::max (-70.0, ungated - 10.0)));
}

Role Analyzer::classify (const AnalysisResult& r)
{
    const bool wide = r.correlation < 0.75f;
    const bool monoish = r.correlation > 0.93f;

    // Low-frequency source with (almost) nothing on top: kick, 808 or bass
    if (r.lowRatio > 0.55f && r.highRatio < 0.01f && r.midRatio < 0.2f)
    {
        if (r.crestDb > 13.0f)
            return Role::Kick;
        return r.crestDb < 9.5f ? Role::Bass808 : Role::Bass;
    }

    // Full-spectrum programme material (energy in lows, mids and highs): a mix or a drum bus
    const bool broadband = r.lowRatio > 0.1f && r.midRatio > 0.08f && r.highRatio > 0.015f;
    if (broadband && ! monoish)
        return r.crestDb > 17.0f ? Role::DrumBus : Role::Master;

    // Bright, transient content
    if (r.highRatio > 0.4f)
        return Role::HatsPerc;
    if (r.crestDb > 16.0f && r.centroidHz > 900.0f)
        return Role::Snare;

    // Voice-like: focused mid band, centred image, moderate dynamics
    if (monoish && r.midRatio > 0.55f && r.centroidHz > 500.0f && r.centroidHz < 4000.0f && r.crestDb < 16.0f)
        return Role::LeadVocal;

    if (wide)
        return r.crestDb < 10.0f ? Role::Pad : Role::Keys;

    if (r.centroidHz > 1500.0f)
        return Role::SynthLead;
    if (r.centroidHz > 700.0f && r.midRatio > 0.6f)
        return Role::Guitar;
    return Role::Keys;
}

} // namespace sam
