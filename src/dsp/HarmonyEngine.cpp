#include "dsp/HarmonyEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace novapitch::dsp
{

namespace
{
constexpr float kMaxDetuneCents = 12.0f;
constexpr double kMaxDelaySeconds = 0.030;
constexpr std::array<float, HarmonyEngine::kVoices> kDelayShape { 0.55f, 0.85f, 0.70f, 1.0f };

int nextPow2 (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

bool inMask (uint16_t mask, int note) noexcept
{
    return ((mask >> (((note % 12) + 12) % 12)) & 1u) != 0;
}
} // namespace

void HarmonyEngine::prepare (double sr, int maxLatencySamples, int maxBlockSize, double frames)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    frameRate = frames > 0.0 ? frames : 375.0;
    maxBlock = std::max (1, maxBlockSize);

    const int delaySize = nextPow2 (static_cast<int> (kMaxDelaySeconds * sampleRate) + maxBlock + 2);
    uint32_t seed = 0x9e3779b9u;
    for (auto& v : voices)
    {
        v.shifter.prepare (sampleRate, maxLatencySamples);
        v.shifter.setMix (1.0f);
        v.buffer.assign (static_cast<size_t> (maxBlock), 0.0f);
        v.delayLine.assign (static_cast<size_t> (delaySize), 0.0f);
        v.rng = (seed += 0x6d2b79f5u);
    }
    busL.assign (static_cast<size_t> (maxBlock), 0.0f);
    busR.assign (static_cast<size_t> (maxBlock), 0.0f);
    filtersDirty = true;
    setBus (bus);
    for (int i = 0; i < kVoices; ++i)
        setVoice (i, voices[static_cast<size_t> (i)].settings);
    reset();
}

void HarmonyEngine::reset() noexcept
{
    for (auto& v : voices)
    {
        v.shifter.reset();
        std::fill (v.delayLine.begin(), v.delayLine.end(), 0.0f);
        v.delayWrite = 0;
        v.env = v.envTarget = 0.0f;
        v.detuneWalk = 0.0f;
    }
    for (auto* f : { &hpf, &lpf, &peak })
        for (auto& b : *f)
            b.reset();
}

void HarmonyEngine::setLatency (int samples) noexcept
{
    for (auto& v : voices)
        v.shifter.setLatency (samples);
}

void HarmonyEngine::setVoice (int index, const HarmonyVoiceSettings& s) noexcept
{
    if (index < 0 || index >= kVoices)
        return;
    auto& v = voices[static_cast<size_t> (index)];

    if (v.settings.mode == HarmonyMode::Off && s.mode != HarmonyMode::Off)
        v.shifter.reset(); // its input ring is stale: start clean (the envelope fades in)

    v.settings = s;
    v.levelLin = std::pow (10.0f, std::clamp (s.levelDb, -80.0f, 12.0f) / 20.0f);
    const float angle = (std::clamp (s.pan, -1.0f, 1.0f) + 1.0f) * static_cast<float> (std::numbers::pi) * 0.25f;
    v.panL = std::cos (angle);
    v.panR = std::sin (angle);
    v.delaySamples = static_cast<int> (std::lround (std::clamp (s.humanize, 0.0f, 1.0f) * kMaxDelaySeconds * sampleRate
                                                    * kDelayShape[static_cast<size_t> (index)]));
    v.shifter.setFormantFactor (std::exp2 (std::clamp (s.formantSt, -12.0f, 12.0f) / 12.0f));
}

void HarmonyEngine::setBus (const HarmonyBusSettings& s) noexcept
{
    bus = s;
    attackCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (1.0f, s.attackMs) * 0.001 * sampleRate)));
    releaseCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (1.0f, s.releaseMs) * 0.001 * sampleRate)));
    auto differs = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    if (differs (s.hpfHz, appliedBus.hpfHz) || differs (s.lpfHz, appliedBus.lpfHz)
        || differs (s.eqFreqHz, appliedBus.eqFreqHz) || differs (s.eqGainDb, appliedBus.eqGainDb))
        filtersDirty = true;
}

void HarmonyEngine::updateFilters() noexcept
{
    const double nyq = 0.45 * sampleRate;
    for (auto& b : hpf)
        b.setHighPass (sampleRate, std::clamp (static_cast<double> (bus.hpfHz), 10.0, nyq), 0.70710678);
    for (auto& b : lpf)
        b.setLowPass (sampleRate, std::clamp (static_cast<double> (bus.lpfHz), 100.0, nyq), 0.70710678);
    for (auto& b : peak)
        b.setPeak (sampleRate, std::clamp (static_cast<double> (bus.eqFreqHz), 50.0, nyq), 0.9, bus.eqGainDb);
    appliedBus = bus;
    filtersDirty = false;
}

bool HarmonyEngine::anyVoiceEnabled() const noexcept
{
    return std::any_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.settings.mode != HarmonyMode::Off; });
}

int HarmonyEngine::stepDegrees (int note, int degrees, uint16_t mask) noexcept
{
    mask &= 0x0fff;
    if (mask == 0)
        return note + 2 * degrees;

    int n = note;
    if (! inMask (mask, n)) // start from the nearest scale note (below first)
        for (int k = 1; k < 12; ++k)
        {
            if (inMask (mask, n - k)) { n -= k; break; }
            if (inMask (mask, n + k)) { n += k; break; }
        }

    const int sign = degrees >= 0 ? 1 : -1;
    for (int s = 0; s < std::abs (degrees); ++s)
    {
        do
            n += sign;
        while (! inMask (mask, n));
    }
    return n;
}

bool HarmonyEngine::voiceTargetMidi (int index, const HarmonyFrame& f, float& out) const noexcept
{
    const auto& s = voices[static_cast<size_t> (index)].settings;
    if (s.mode == HarmonyMode::Off || ! f.voiced)
        return false;

    const int base = f.leadNote >= 0 ? f.leadNote : static_cast<int> (std::lround (f.leadOutputMidi));
    const float deviation = f.leadOutputMidi - static_cast<float> (base); // vibrato / expression of the lead

    switch (s.mode)
    {
        case HarmonyMode::Diatonic:
            out = static_cast<float> (stepDegrees (base, s.degrees, f.scaleMask)) + deviation;
            return true;
        case HarmonyMode::Semitones:
            out = f.leadOutputMidi + static_cast<float> (s.semitones);
            return true;
        case HarmonyMode::Midi:
            if (index >= f.numHeld)
                return false;
            out = static_cast<float> (f.held[static_cast<size_t> (index)]) + deviation;
            return true;
        case HarmonyMode::Off:
            break;
    }
    return false;
}

float HarmonyEngine::random (Voice& v) noexcept
{
    v.rng ^= v.rng << 13;
    v.rng ^= v.rng >> 17;
    v.rng ^= v.rng << 5;
    return static_cast<float> (v.rng) / 2147483648.0f - 1.0f;
}

void HarmonyEngine::updateControl (const HarmonyFrame& f) noexcept
{
    bool anySolo = false;
    for (const auto& v : voices)
        anySolo = anySolo || (v.settings.solo && v.settings.mode != HarmonyMode::Off);

    const bool gateOpen = f.inputRmsDb > bus.gateDb;
    const auto walkCoeff = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * 0.5 / frameRate));

    for (int i = 0; i < kVoices; ++i)
    {
        auto& v = voices[static_cast<size_t> (i)];
        float midi = 0.0f;
        const bool active = voiceTargetMidi (i, f, midi) && gateOpen && ! v.settings.mute && (! anySolo || v.settings.solo);

        if (active)
        {
            v.detuneWalk += walkCoeff * (random (v) - v.detuneWalk);
            const float detune = v.detuneWalk * std::clamp (v.settings.humanize, 0.0f, 1.0f) * kMaxDetuneCents;
            const float ratio = std::exp2 ((midi - f.detectedMidi) / 12.0f + detune / 1200.0f);
            v.shifter.setControl (true, f.periodSamples, ratio);
        }
        else
        {
            v.shifter.setControl (f.voiced, f.periodSamples, 1.0f);
        }
        v.envTarget = active ? 1.0f : 0.0f;
    }
}

void HarmonyEngine::process (const float* monoIn, float* outL, float* outR, int numSamples) noexcept
{
    if (monoIn == nullptr || outL == nullptr || numSamples <= 0)
        return;

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);
        bool anything = false;
        std::fill (busL.begin(), busL.begin() + n, 0.0f);
        std::fill (busR.begin(), busR.begin() + n, 0.0f);

        for (auto& v : voices)
        {
            if (v.settings.mode == HarmonyMode::Off && v.env < 1.0e-5f)
                continue;
            anything = true;

            std::copy (monoIn + start, monoIn + start + n, v.buffer.begin());
            float* ch[] = { v.buffer.data() };
            v.shifter.process (ch, 1, monoIn + start, n);

            const int mask = static_cast<int> (v.delayLine.size()) - 1;
            for (int i = 0; i < n; ++i)
            {
                v.env += (v.envTarget > v.env ? attackCoeff : releaseCoeff) * (v.envTarget - v.env);
                v.delayLine[static_cast<size_t> (v.delayWrite)] = v.buffer[static_cast<size_t> (i)] * v.env * v.levelLin;
                const float s = v.delayLine[static_cast<size_t> ((v.delayWrite - v.delaySamples) & mask)];
                v.delayWrite = (v.delayWrite + 1) & mask;
                busL[static_cast<size_t> (i)] += s * v.panL;
                busR[static_cast<size_t> (i)] += s * v.panR;
            }
        }
        if (! anything)
            continue;

        if (filtersDirty)
            updateFilters();

        const float width = std::clamp (bus.width, 0.0f, 2.0f);
        for (int i = 0; i < n; ++i)
        {
            float l = peak[0].process (lpf[0].process (hpf[0].process (busL[static_cast<size_t> (i)])));
            float r = peak[1].process (lpf[1].process (hpf[1].process (busR[static_cast<size_t> (i)])));
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
            l = mid + side;
            r = mid - side;
            if (outR == nullptr || outR == outL)
                outL[start + i] += 0.5f * (l + r);
            else
            {
                outL[start + i] += l;
                outR[start + i] += r;
            }
        }
    }
}

} // namespace novapitch::dsp
