#include "plugin/graph/TimeRenderer.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>

namespace novapitch
{

namespace
{
/** Channel view with a sample offset, in the shape signalsmith-stretch expects. */
struct Offset
{
    float* const* data;
    int64_t offset;
    float* operator[] (int c) const noexcept { return data[c] + offset; }
};
} // namespace

//==============================================================================
bool RenderedTime::substitute (float* const* channels, int numChannels, int numSamples, int64_t blockStart) const noexcept
{
    bool any = false;
    const int fade = std::max (1, static_cast<int> (kFadeSeconds * sampleRate));
    for (const auto& span : spans)
    {
        const int64_t a = std::max (blockStart, span.start);
        const int64_t b = std::min (blockStart + numSamples, span.end());
        if (b <= a)
            continue;
        const int spanChannels = span.audio.getNumChannels();
        for (int64_t t = a; t < b; ++t)
        {
            const auto k = static_cast<int> (t - span.start);
            const auto fromEnd = static_cast<int> (span.end() - 1 - t);
            float w = 1.0f;
            if (k < fade)
                w = (static_cast<float> (k) + 0.5f) / static_cast<float> (fade);
            if (fromEnd < fade)
                w = std::min (w, (static_cast<float> (fromEnd) + 0.5f) / static_cast<float> (fade));
            const auto i = static_cast<size_t> (t - blockStart);
            for (int c = 0; c < numChannels; ++c)
            {
                const float r = span.audio.getReadPointer (std::min (c, spanChannels - 1))[k];
                float& x = channels[c][i];
                x += w * (r - x);
            }
        }
        any = true;
    }
    return any;
}

//==============================================================================
TimeRenderer::TimeRenderer() : juce::Thread ("NovaPitch time render")
{
    startThread (juce::Thread::Priority::low);
}

TimeRenderer::~TimeRenderer()
{
    stopThread (10000);
}

void TimeRenderer::request (Job job)
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        queued = std::make_unique<Job> (std::move (job));
    }
    notify();
}

std::unique_ptr<RenderedTime> TimeRenderer::takeResult (int& id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (! hasResult)
        return nullptr;
    hasResult = false;
    id = resultId;
    return std::move (result);
}

void TimeRenderer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<Job> job;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            job = std::move (queued);
        }
        if (job == nullptr)
        {
            wait (-1);
            continue;
        }
        busy.store (true);
        auto rendered = render (*job);
        {
            const std::lock_guard<std::mutex> lock (mutex);
            result = std::move (rendered);
            resultId = job->id;
            hasResult = true;
        }
        busy.store (false);
    }
}

//==============================================================================
juce::AudioBuffer<float> TimeRenderer::readSource (const std::vector<AudioPass>& passes, const juce::File& dir, double sampleRate,
                                                   int channels, int64_t start, int numSamples, bool* missing)
{
    juce::AudioBuffer<float> out (channels, numSamples);
    out.clear();
    bool covered = false;
    juce::WavAudioFormat wav;

    for (const auto& pass : passes) // later passes overwrite earlier ones
    {
        const auto passStart = static_cast<int64_t> (std::llround (pass.startSeconds() * sampleRate));
        const auto passEnd = static_cast<int64_t> (std::llround (pass.endSeconds() * sampleRate));
        const int64_t a = std::max (start, passStart), b = std::min (start + numSamples, passEnd);
        if (b <= a)
            continue;

        auto stream = std::make_unique<juce::FileInputStream> (dir.getChildFile (pass.fileName));
        if (! stream->openedOk())
        {
            if (missing != nullptr)
                *missing = true;
            continue;
        }
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
        if (reader == nullptr)
        {
            if (missing != nullptr)
                *missing = true;
            continue;
        }

        const int count = static_cast<int> (b - a);
        const int readerChannels = static_cast<int> (reader->numChannels);
        const double ratio = pass.sampleRate / sampleRate;
        juce::AudioBuffer<float> tmp (readerChannels, count);
        if (std::abs (ratio - 1.0) < 1.0e-9)
        {
            reader->read (&tmp, 0, count, a - passStart, true, true);
        }
        else
        {
            // Different sample rate since the capture: read at the pass rate and resample.
            const auto fileStart = static_cast<int64_t> (std::floor ((static_cast<double> (a) / sampleRate - pass.startSeconds()) * pass.sampleRate));
            const int fileCount = static_cast<int> (std::ceil (count * ratio)) + 8;
            juce::AudioBuffer<float> raw (readerChannels, fileCount);
            reader->read (&raw, 0, fileCount, std::max<int64_t> (0, fileStart), true, true);
            for (int c = 0; c < readerChannels; ++c)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process (ratio, raw.getReadPointer (c), tmp.getWritePointer (c), count);
            }
        }

        for (int c = 0; c < channels; ++c)
        {
            auto* dst = out.getWritePointer (c, static_cast<int> (a - start));
            if (readerChannels == 2 && channels == 1)
            {
                for (int i = 0; i < count; ++i)
                    dst[i] = 0.5f * (tmp.getSample (0, i) + tmp.getSample (1, i));
            }
            else
                std::copy (tmp.getReadPointer (std::min (c, readerChannels - 1)), tmp.getReadPointer (std::min (c, readerChannels - 1)) + count, dst);
        }
        covered = true;
    }
    if (! covered && missing != nullptr)
        *missing = true;
    return out;
}

std::unique_ptr<RenderedTime> TimeRenderer::render (const Job& job, bool* audioMissing)
{
    if (job.map.anchors.size() < 2 || job.map.isIdentity() || ! job.map.isValid())
        return nullptr;

    const double sr = job.sampleRate;
    const int ch = std::clamp (job.channels, 1, 2);

    // Warped ranges with their margins, merged when the margins touch.
    std::vector<std::pair<double, double>> ranges;
    for (auto [o0, o1] : job.map.warpedRanges())
    {
        o0 -= kPadSeconds;
        o1 += kPadSeconds;
        if (! ranges.empty() && o0 <= ranges.back().second + 0.05)
            ranges.back().second = std::max (ranges.back().second, o1);
        else
            ranges.emplace_back (std::max (0.0, o0), o1);
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch (0x4e6f7661); // fixed seed: reproducible renders
    stretch.presetDefault (ch, static_cast<float> (sr));
    const int inLat = stretch.inputLatency();
    const int delay = inLat + stretch.outputLatency();
    const int seekLength = stretch.seekLength();

    auto result = std::make_unique<RenderedTime>();
    result->sampleRate = sr;

    for (const auto& [r0, r1] : ranges)
    {
        const auto s0 = static_cast<int64_t> (std::floor (r0 * sr));
        const auto s1 = static_cast<int64_t> (std::ceil (r1 * sr));
        const int64_t tail = delay + seekLength;

        // Breakpoints (input sample, output sample). The map is shifted by the
        // stretcher's input latency on both axes, so an event at source time s
        // comes out exactly at toOutput(s) once the total delay is removed.
        std::vector<std::pair<int64_t, int64_t>> points { { s0, s0 } };
        for (const auto& [out, src] : job.map.anchors)
        {
            if (out <= r0 || out >= r1)
                continue;
            const auto p = std::make_pair (static_cast<int64_t> (std::llround (src * sr)) + inLat,
                                           static_cast<int64_t> (std::llround (out * sr)) + inLat);
            if (p.first > points.back().first && p.second > points.back().second)
                points.push_back (p);
        }
        if (s1 + tail > points.back().first && s1 + tail > points.back().second)
            points.emplace_back (s1 + tail, s1 + tail);
        else
            continue; // malformed (should not happen with a valid map)

        const int64_t srcStart = s0 - seekLength;
        const auto srcLength = static_cast<int> (points.back().first - srcStart);
        bool missing = false;
        auto source = readSource (job.passes, job.directory, sr, ch, srcStart, srcLength, &missing);
        if (missing)
        {
            if (audioMissing != nullptr)
                *audioMissing = true;
            result->audioMissing = true;
            continue;
        }

        juce::AudioBuffer<float> rendered (ch, static_cast<int> (points.back().second - s0));
        rendered.clear();
        stretch.reset();
        stretch.seek (Offset { source.getArrayOfWritePointers(), 0 }, seekLength, 1.0);
        for (size_t i = 1; i < points.size(); ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            stretch.process (Offset { source.getArrayOfWritePointers(), a.first - srcStart }, static_cast<int> (b.first - a.first),
                             Offset { rendered.getArrayOfWritePointers(), a.second - s0 }, static_cast<int> (b.second - a.second));
        }

        // After a stretch the phase vocoder keeps a phase offset against the
        // original, so the switch back to the live input is placed where the
        // source is quietest (gaps between syllables), at least ~80 ms after
        // the last edited anchor (the analysis block must have left the edit).
        double lastWarped = r0;
        for (const auto& [out, src] : job.map.anchors)
            if (out > r0 && out < r1 && std::abs (out - src) > 1.0e-9)
                lastWarped = std::max (lastWarped, out);
        for (const auto& [o0, o1] : job.map.warpedRanges())
            if (o1 > r0 && o1 < r1)
                lastWarped = std::max (lastWarped, o1);
        const int fade = std::max (1, static_cast<int> (RenderedTime::kFadeSeconds * sr));
        const auto searchFrom = std::max (s0 + fade, static_cast<int64_t> ((lastWarped + 0.08) * sr));
        const int64_t searchTo = s1 - fade;
        int64_t cut = s1;
        double quietest = 1.0e30;
        for (int64_t w = searchFrom; w + fade <= searchTo + fade && w + fade <= s1; w += fade / 2)
        {
            double energy = 0.0;
            for (int c = 0; c < ch; ++c)
            {
                const float* x = source.getReadPointer (c, static_cast<int> (w - srcStart));
                for (int i = 0; i < fade; ++i)
                    energy += x[i] * x[i];
            }
            if (energy < quietest * 0.999)
            {
                quietest = energy;
                cut = w + fade; // the fade-out ends at the quiet window's end
            }
        }

        RenderedTime::Span span;
        span.start = s0;
        span.audio.setSize (ch, static_cast<int> (cut - s0));
        for (int c = 0; c < ch; ++c)
            span.audio.copyFrom (c, 0, rendered, c, delay, static_cast<int> (cut - s0));
        result->spans.push_back (std::move (span));
    }
    return result;
}

} // namespace novapitch
