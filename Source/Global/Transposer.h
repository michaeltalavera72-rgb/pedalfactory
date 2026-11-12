#pragma once
// Semitone Transposer — board-level (global) pitch transposition, first in the chain.
//
//   input -> 20 Hz high-pass -> engine (LIVE or STUDIO) -> MIX with dry -> LEVEL -> ON/OFF crossfade -> output
//
// LIVE   (default): SpliceShifter, a correlation-aligned delay-line shifter. A read head moves through a
//        short delay line at the pitch ratio; when it reaches the edge of its range it jumps back
//        (forward for up-shifts) to the point where the waveform lines up best and crossfades.
//        Pitch is exact (the read speed IS 2^(n/12)), added delay averages ~6-8 ms, 0 st = no delay at all.
//        Best for small shifts (drop/raise tunings); large shifts on chords splice often and flutter a little.
// STUDIO: Signalsmith Stretch (MIT, https://github.com/Signalsmith-Audio/signalsmith-stretch), an STFT
//        polyphonic shifter. Cleaner on octaves and dense chords, ~85 ms latency (reported to the host).
//
// CPU: nothing runs while the transposer is OFF, and only the selected engine runs while it is ON (the other
// one is restarted when the mode changes; STUDIO needs its latency to fill before it is faded in, so switching
// to STUDIO - or switching ON in STUDIO - takes effect ~85 ms later, without a gap).
//
// Pure C++ (no JUCE) so it can be tested on its own. Mono, like the board.
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace transposer {

// ----------------------------------------------------------------------------------------------
// LIVE engine: correlation-aligned delay-line shifter with adaptive splicing.
//
// The read head drifts through a delay line at (1 - ratio) samples per sample. A jump ("splice") is
// crossfaded and lands where the recent waveform matches best, so the join is phase-continuous.
//  - Pitch down (delay grows): as the delay grows, every newly reachable jump back to the minimum delay
//    is tested; the first good match (correlation >= goodMatch, taken at its local peak) is used at once.
//    A single note matches after one period (low E: 12 ms); a chord usually needs its common period
//    (~24 ms). If nothing matches, the head splices to the best match when it reaches maxDelay.
//  - Pitch up (delay shrinks): at the minimum delay the head jumps to the SHORTEST good match.
//  - Pick attacks: an onset detector watches the input. When a new note is picked, the head jumps
//    straight to it (skipping the end of the previous note's decay instead of part of the new attack),
//    so every attack is heard ~3 ms after it is played, whole and only once. Up-shifts land the attack a
//    little further back so the next backward splice comes after the attack, not inside it.
// So the delay stays as short as the music allows: attacks ~3 ms, sustain ~7 ms (notes) / ~13 ms (chords).
class SpliceShifter
{
public:
    struct Tuning { double minDelayMs = 1.0, maxDelayMs = 26.0, crossfadeMs = 10.0, upMarginMs = 3.0,
                    matchWindowMs = 12.0, minJumpMs = 1.5, goodMatch = 0.95,
                    onsetSync = true, onsetRatio = 2.2, onsetFloor = 0.002, prerollMs = 1.5, onsetFadeMs = 3.0,
                    attackProtectMs = 30.0; } tune;
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        size = 1; while (size < (int) (sr * 0.12)) size <<= 1;      // ring holds max delay + match window + fade overrun
        mask = size - 1;
        buf.assign((size_t) size, 0.0f);
        dLo    = std::max(4.0, tune.minDelayMs * 0.001 * sr);          // interpolator needs >= 2 samples
        dHi    = tune.maxDelayMs * 0.001 * sr;
        xfade  = std::max(16, (int) (tune.crossfadeMs * 0.001 * sr));
        upMargin = std::max(4.0, tune.upMarginMs * 0.001 * sr);
        win    = std::max(32, (int) (tune.matchWindowMs * 0.001 * sr));
        jMin   = std::max(8,  (int) (tune.minJumpMs * 0.001 * sr));
        coarse = std::max(1, (int) std::lround(sr / 16000.0));          // 3 at 48 kHz
        preroll = tune.prerollMs * 0.001 * sr;
        onsetFade = std::max(16, (int) (tune.onsetFadeMs * 0.001 * sr));
        protect = tune.attackProtectMs * 0.001 * sr;
        refractory = (int) (0.040 * sr); maxAge = (int) (0.020 * sr);
        aFast = std::exp(-1.0 / (0.0005 * sr)); rFast = std::exp(-1.0 / (0.005 * sr)); aSlow = std::exp(-1.0 / (0.030 * sr));
        reset();
    }
    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        w = 0; splices = 0; onsets = 0; fastEnv = slowEnv = 0.0; sinceOnset = 1 << 30; age = -1; rewind();
    }
    // Put the read head back to minimum delay (only when the output isn't being heard).
    void rewind() { dA = dLo; fading = false; armed = false; lastJ = jMin - 1; tick = 0; }

    float process(float x, double ratio)
    {
        buf[(size_t) w] = x;
        const double rate = 1.0 - ratio;        // how much the delay grows per sample
        detectOnset(x);
        if (! fading && age >= 0 && std::abs(rate) > 1e-12) syncToOnset(rate);
        if (! fading)
        {
            if (rate > 0.0) spliceDown(rate);
            else if (rate < 0.0) spliceUp(rate);
        }
        float y;
        if (fading)
        {
            const float t  = (float) fadePos / (float) fadeLen;
            const float fi = 0.5f - 0.5f * std::cos(3.14159265f * t), fo = 1.0f - fi;
            // constant loudness whether the two heads are correlated (rho ~ 1) or not (rho ~ 0)
            const float norm = 1.0f / std::sqrt(std::max(0.25f, fi * fi + fo * fo + 2.0f * rho * fi * fo));
            y = (fo * read(dA) + fi * read(dB)) * norm;
            dB += rate;
            if (++fadePos >= fadeLen) { fading = false; dA = dB; armed = false; lastJ = jMin - 1; }
        }
        else y = read(dA);
        dA += rate;
        if (! fading) dA = std::clamp(dA, 2.0, (double) size - (double) win - 8.0);
        w = (w + 1) & mask;
        return y;
    }
    double currentDelay() const { return fading ? dB : dA; }
    // After reset() the head reads silence for this long before the input reaches it (minimum delay).
    int startupSamples() const { return (int) dLo + 8; }
    long splices = 0, onsets = 0;               // for tests

private:
    double sr = 48000.0, dLo = 48, dHi = 1248, dA = 48, dB = 48, upMargin = 144;
    std::vector<float> buf;
    int size = 8192, mask = 8191, w = 0, xfade = 480, win = 576, jMin = 72, fadePos = 0, fadeLen = 480;
    bool fading = false, armed = false;
    int armJ = 0, lastJ = 0, tick = 0, coarse = 3;
    double armC = 0.0;
    float rho = 0.0f;
    // onset detector
    double fastEnv = 0.0, slowEnv = 0.0, aFast = 0.9, rFast = 0.99, aSlow = 0.999, preroll = 72, protect = 1440;
    int sinceOnset = 1 << 30, age = -1, refractory = 1920, maxAge = 960, onsetFade = 144;

    void detectOnset(float x)
    {
        const double a = std::abs((double) x);
        fastEnv = a > fastEnv ? aFast * fastEnv + (1.0 - aFast) * a : rFast * fastEnv + (1.0 - rFast) * a;
        slowEnv = aSlow * slowEnv + (1.0 - aSlow) * fastEnv;
        if (age >= 0 && ++age > maxAge) age = -1;              // too late to sync this one
        if (sinceOnset < (1 << 30)) ++sinceOnset;
        if (tune.onsetSync && fastEnv > tune.onsetFloor && fastEnv > tune.onsetRatio * slowEnv && sinceOnset > refractory)
        { sinceOnset = 0; age = 0; ++onsets; }
    }
    // Jump forward so the head reads the new attack almost at once (skips old decay, never the attack).
    void syncToOnset(double rate)
    {
        const double reach = (double) age + preroll;           // delay at which the onset (minus pre-roll) sits now
        double target = std::max(dLo, reach);
        if (rate < 0.0) target = std::min(dHi, target + -rate * protect);   // up: room before the next backward splice
        age = -1;
        if (dA <= reach || target >= dA - 8.0) return;         // head already at/past the attack, or already close
        dB = target; rho = 0.0f; fadeLen = onsetFade; fading = true; fadePos = 0; ++splices;
    }

    float at(int i) const { return buf[(size_t) (i & mask)]; }
    float read(double d) const                  // 4-point cubic (Catmull-Rom) at delay d
    {
        const double pos = (double) w - d;
        const double fl = std::floor(pos);
        const int i = (int) fl;
        const float f = (float) (pos - fl);
        const float ym1 = at(i - 1), y0 = at(i), y1 = at(i + 1), y2 = at(i + 2);
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    }
    // Normalised correlation between the last `win` samples at the head and at delay (head + dir*J),
    // using every `stride`-th sample (guitar energy is mostly below 2 kHz, so a coarse pass is accurate enough).
    double match(int base, int dir, int J, double eA, int stride) const
    {
        const int cb = base - dir * J;
        double xy = 0.0, eB = 0.0;
        for (int k = 0; k < win; k += stride) { const float a = at(base - k), b = at(cb - k); xy += (double) a * b; eB += (double) b * b; }
        return (eA > 1e-12 && eB > 1e-12) ? xy / std::sqrt(eA * eB) : 0.0;
    }
    double energy(int base, int stride) const
    {
        double e = 0.0;
        for (int k = 0; k < win; k += stride) { const float a = at(base - k); e += (double) a * a; }
        return e;
    }
    // Best jump within J0 +/- coarse step, at full resolution.
    int refine(int base, int dir, int J0, int lo, int hi, double& c)
    {
        const double eA = energy(base, 1);
        int best = J0; c = -2.0;
        for (int J = std::max(lo, J0 - coarse); J <= std::min(hi, J0 + coarse); ++J)
        { const double m = match(base, dir, J, eA, 1); if (m > c) { c = m; best = J; } }
        return best;
    }
    void startSplice(int dir, int J, double c, double rate)
    {
        dB = dA + dir * J;
        rho = (float) std::clamp(c, 0.0, 1.0);
        // up-shifts: the old head must not catch up with "now" while it fades out
        fadeLen = rate < 0.0 ? std::max(16, std::min(xfade, (int) (upMargin / -rate))) : xfade;
        fading = true; fadePos = 0; ++splices;
    }
    void spliceDown(double rate)
    {
        const int maxJ = (int) (dA - dLo);
        if ((++tick & 15) == 0 && maxJ > lastJ)
        {
            // test the jumps that became possible since the last check (they land near minimum delay)
            const int base = w - (int) std::lround(dA);
            const double eA = energy(base, coarse);
            for (int J = std::max(lastJ + 1, jMin); J <= maxJ; ++J)
            {
                const double c = match(base, -1, J, eA, coarse);
                if (armed)
                {
                    if (c > armC) { armC = c; armJ = J; }
                    else if (c < armC - 0.01) { startSplice(-1, armJ, armC, rate); return; }   // just passed the peak
                }
                else if (c >= tune.goodMatch) { armed = true; armC = c; armJ = J; }
            }
            lastJ = maxJ;
        }
        if (dA >= dHi)
        {
            if (armed) { startSplice(-1, armJ, armC, rate); return; }
            const int base = w - (int) std::lround(dA);
            const double eA = energy(base, coarse);
            int best = maxJ; double bestC = -2.0;
            for (int J = maxJ; J >= jMin; J -= coarse) { const double c = match(base, -1, J, eA, coarse); if (c > bestC) { bestC = c; best = J; } }   // ties (silence): longest jump
            if (bestC > 0.0) best = refine(base, -1, best, jMin, maxJ, bestC);
            startSplice(-1, best, bestC, rate);
        }
    }
    void spliceUp(double rate)
    {
        const int fl = std::max(16, std::min(xfade, (int) (upMargin / -rate)));
        if (dA > dLo + -rate * fl) return;
        const int base = w - (int) std::lround(dA);
        const double eA = energy(base, coarse);
        const int maxJ = std::max(jMin, (int) (dHi - dA));
        int best = maxJ, firstGood = -1; double bestC = -2.0, goodC = -2.0;
        for (int J = jMin; J <= maxJ; J += coarse)
        {
            const double c = match(base, +1, J, eA, coarse);
            if (c > bestC) { bestC = c; best = J; }
            if (firstGood < 0 && c >= tune.goodMatch) { firstGood = J; goodC = c; }
            else if (firstGood >= 0)
            {
                if (c > goodC) { goodC = c; firstGood = J; }
                else break;                                       // shortest good match, at its peak
            }
        }
        int J = firstGood >= 0 ? firstGood : best;
        double c = firstGood >= 0 ? goodC : bestC;
        if (c > 0.0) J = refine(base, +1, J, jMin, maxJ, c);
        startSplice(+1, J, c, rate);
    }
};

// ----------------------------------------------------------------------------------------------
// Whole feature: high-pass, engines, glide, mix, level, on/off.
class Transposer
{
public:
    SpliceShifter live;                         // public so tests can adjust live.tune before prepare()
    double glideSec = 0.040;                    // any transpose change glides over 40 ms (no clicks, no long swoops)
    double studioBlockSec = 0.0853;             // STUDIO analysis block (4096 samples at 48 kHz); latency = 1 block
    enum class Mode { live, studio };

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        live.prepare(sr);
        // STUDIO: ~85 ms analysis block (4096 samples at 48 kHz), hop = block/4, computation spread over the hop.
        const int block = (int) std::lround(studioBlockSec * sr), interval = block / 4;
        studio.configure(1, block, interval, false);
        studioLatency = studio.inputLatency() + studio.outputLatency();
        {   // Prime STUDIO's internal peak list with noise so its vectors reach full capacity now,
            // not on the audio thread later (reset() keeps the capacity).
            std::vector<float> noise((size_t) block), out((size_t) block);
            unsigned r = 12345u;
            for (auto& v : noise) { r = r * 1664525u + 1013904223u; v = (float) ((double) (r >> 8) / 8388608.0 - 1.0) * 0.5f; }
            for (int k = 0; k < 4; ++k) { float* ins[1] = { noise.data() }; float* outs[1] = { out.data() }; studio.process(ins, block, outs, block); }
        }
        dryLine.assign((size_t) (studioLatency + 1), 0.0f);
        (void) maxBlock;                        // processing runs in sub-blocks of <= 32 samples
        tmpIn.assign(32, 0.0f);
        tmpOut.assign(32, 0.0f);
        // 20 Hz 2nd-order Butterworth high-pass (RBJ)
        const double w0 = 2.0 * 3.14159265358979 * 20.0 / sr, cw = std::cos(w0), al = std::sin(w0) / (2.0 * 0.70710678);
        const double a0 = 1.0 + al;
        hb0 = (1.0 + cw) / 2.0 / a0; hb1 = -(1.0 + cw) / a0; hb2 = hb0; ha1 = -2.0 * cw / a0; ha2 = (1.0 - al) / a0;
        glideStep = 1.0 / (glideSec * sr);
        fadeStep  = 1.0 / (0.015 * sr);         // 15 ms crossfades (on/off, mode, 0 st)
        paramStep = 1.0 / (0.020 * sr);         // 20 ms mix/level smoothing
        reset();
    }
    void reset()
    {
        live.reset(); studio.reset();
        std::fill(dryLine.begin(), dryLine.end(), 0.0f); dryPos = 0;
        hx1 = hx2 = hy1 = hy2 = 0.0;
        semis = targetSemis; on = targetOn ? 1.0 : 0.0; studioAmt = targetStudio ? 1.0 : 0.0;
        shifted = targetSt != 0 ? 1.0 : 0.0; mix = targetMix; level = targetLevel;
        liveRunning = studioRunning = true;     // both freshly reset; process() drops the one it doesn't need
        studioHold = liveHold = onHold = 0; asleep = false;
    }

    // Parameters (call once per block, from the audio thread).
    void setSemitones(int st)
    {
        st = std::clamp(st, -12, 12);
        if (st != targetSt)
        {
            targetSt = st; targetSemis = (double) st;
            glideStep = std::max(1.0, std::abs(targetSemis - semis)) / (glideSec * sr);
        }
    }
    void setMix(float m01)      { targetMix = std::clamp((double) m01, 0.0, 1.0); }
    void setLevelDb(float db)   { targetLevel = std::pow(10.0, std::clamp((double) db, -24.0, 12.0) / 20.0); }
    void setOn(bool b)          { targetOn = b; }
    void setMode(Mode m)        { targetStudio = (m == Mode::studio); }

    // Latency the host should compensate: STUDIO only (LIVE is a few ms and varies; for live playing).
    int reportedLatency() const { return (targetOn && targetStudio) ? studioLatency : 0; }
    int studioLatencySamples() const { return studioLatency; }
    double liveDelaySamples() const { return live.currentDelay(); }
    long liveSplices() const { return live.splices; }
    bool isIdle() const { return on <= 0.0 && ! targetOn; }

    void process(float* x, int n)
    {
        // OFF (and faded out): the signal passes untouched and neither engine runs.
        if (isIdle()) { asleep = true; return; }
        if (asleep) wake();
        // STUDIO engine runs block-wise (glide applied per sub-block of <= 32 samples)
        for (int start = 0; start < n; start += 32)
        {
            const int len = std::min(32, n - start);
            float* io = x + start;
            // Run only the engine(s) in use. One that starts again begins from a clean state.
            const bool wantStudio = targetStudio || studioAmt > 0.0;
            if (wantStudio && ! studioRunning)
            {
                studio.reset(); std::fill(dryLine.begin(), dryLine.end(), 0.0f); dryPos = 0;
                studioRunning = true; studioHold = studioLatency;        // fade it in once its latency has filled
            }
            else if (! wantStudio) studioRunning = false;
            const bool wantLive = ! targetStudio || studioAmt < 1.0;
            if (wantLive && ! liveRunning) { live.reset(); liveRunning = true; liveHold = live.startupSamples(); }
            else if (! wantLive) liveRunning = false;
            // high-passed input for the engines
            for (int i = 0; i < len; ++i)
            {
                const double in = io[i];
                const double y = hb0 * in + hb1 * hx1 + hb2 * hx2 - ha1 * hy1 - ha2 * hy2;
                hx2 = hx1; hx1 = in; hy2 = hy1; hy1 = y;
                tmpIn[(size_t) i] = (float) y;
            }
            // STUDIO: semitone value for this sub-block (Signalsmith only needs a slow update)
            if (studioRunning)
            {
                studio.setTransposeSemitones((float) semis);
                float* ins[1] = { tmpIn.data() }; float* outs[1] = { tmpOut.data() };
                studio.process(ins, len, outs, len);
            }

            for (int i = 0; i < len; ++i)
            {
                // glide the transpose value (in semitones) toward the integer target
                if (semis < targetSemis) semis = std::min(targetSemis, semis + glideStep);
                else if (semis > targetSemis) semis = std::max(targetSemis, semis - glideStep);
                // "shifted": crossfade between the engines and the plain (dry) signal around 0 st
                const bool wantShift = targetSt != 0 || std::abs(semis) > 1e-9;
                if (wantShift) { if (shifted <= 0.0) live.rewind(); shifted = std::min(1.0, shifted + fadeStep); }
                else shifted = std::max(0.0, shifted - fadeStep);
                if (onHold > 0) --onHold; else ramp(on, targetOn ? 1.0 : 0.0, fadeStep);
                if (studioHold > 0) --studioHold;
                if (liveHold > 0) --liveHold;
                if (targetStudio ? studioHold == 0 : liveHold == 0) ramp(studioAmt, targetStudio ? 1.0 : 0.0, fadeStep);
                ramp(mix, targetMix, paramStep);
                ramp(level, targetLevel, paramStep * std::max(0.05, std::abs(targetLevel - level)));

                const float dry = io[i];            // unfiltered input (true bypass path)
                const float hp  = tmpIn[(size_t) i];
                // STUDIO's dry reference is delayed by its latency so MIX stays time-aligned
                float dryDelayed = hp, studOut = 0.0f;
                if (studioRunning)
                {
                    dryDelayed = dryLine[(size_t) dryPos];
                    dryLine[(size_t) dryPos] = hp;
                    if (++dryPos >= (int) dryLine.size()) dryPos = 0;
                    studOut = tmpOut[(size_t) i];
                }
                if (semis != ratioSemis) { ratioSemis = semis; ratio = std::exp2(semis / 12.0); }   // only while gliding
                const float liveOut = liveRunning ? live.process(hp, ratio) : 0.0f;
                const float liveWet = (float) (shifted * liveOut + (1.0 - shifted) * hp);
                const float studWet = (float) (shifted * studOut + (1.0 - shifted) * dryDelayed);
                const float liveMix = (float) (mix * liveWet + (1.0 - mix) * hp);
                const float studMix = (float) (mix * studWet + (1.0 - mix) * dryDelayed);
                const float eng = (float) ((1.0 - studioAmt) * liveMix + studioAmt * studMix) * (float) level;
                io[i] = (float) (on * eng + (1.0 - on) * dry);
            }
        }
    }

private:
    // First buffer after being OFF: start the selected engine from a clean state and fade in from the dry signal.
    void wake()
    {
        live.reset(); studio.reset();
        std::fill(dryLine.begin(), dryLine.end(), 0.0f); dryPos = 0;
        hx1 = hx2 = hy1 = hy2 = 0.0;
        semis = targetSemis; studioAmt = targetStudio ? 1.0 : 0.0;
        shifted = targetSt != 0 ? 1.0 : 0.0; mix = targetMix; level = targetLevel;
        on = 0.0; onHold = targetStudio ? studioLatency : live.startupSamples();   // wait until the engine has signal, then fade in
        studioHold = liveHold = 0; liveRunning = ! targetStudio; studioRunning = targetStudio; asleep = false;
    }
    static void ramp(double& v, double target, double step)
    {
        if (v < target) v = std::min(target, v + step); else if (v > target) v = std::max(target, v - step);
    }
    double sr = 48000.0;
    signalsmith::stretch::SignalsmithStretch<float> studio;
    int studioLatency = 0, dryPos = 0;
    std::vector<float> dryLine, tmpIn, tmpOut;
    double hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0, hx1 = 0, hx2 = 0, hy1 = 0, hy2 = 0;
    double glideStep = 0.001, fadeStep = 0.001, paramStep = 0.001;
    double semis = 0, targetSemis = 0, on = 0, studioAmt = 0, shifted = 0, mix = 1, targetMix = 1, level = 1, targetLevel = 1;
    bool targetOn = false, targetStudio = false;
    int targetSt = 0;
    bool liveRunning = true, studioRunning = true, asleep = false;
    int studioHold = 0, liveHold = 0, onHold = 0;   // samples to wait before fading STUDIO / LIVE / the effect in
    double ratio = 1.0, ratioSemis = 1.0e9;         // pitch ratio, recomputed only when semis changes
};

} // namespace transposer
