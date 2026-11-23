#include "plugin/graph/AudioCapture.h"

#include <algorithm>
#include <array>

namespace novapitch
{

AudioCapture::AudioCapture() : juce::Thread ("NovaPitch capture") {}

AudioCapture::~AudioCapture()
{
    stopThread (4000);
    const std::lock_guard<std::mutex> lock (drainMutex);
    drain();
    finishPass();
}

void AudioCapture::prepare (double sampleRate, int channels)
{
    {
        const std::lock_guard<std::mutex> drainLock (drainMutex);
        drain();
        finishPass(); // a new sample rate / layout starts a new pass
        const std::lock_guard<std::mutex> ringLock (ringMutex);

        const int ch = std::clamp (channels, 1, kMaxChannels);
        const int capacity = static_cast<int> (kRingSeconds * std::max (8000.0, sampleRate)) + 1;
        rings.assign (static_cast<size_t> (ch), std::vector<float> (static_cast<size_t> (capacity), 0.0f));
        headers.assign (4096, {});
        sampleFifo.setTotalSize (capacity);
        sampleFifo.reset();
        headerFifo.setTotalSize (static_cast<int> (headers.size()));
        headerFifo.reset();
        numChannels.store (ch);
        rate.store (sampleRate);
    }
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void AudioCapture::setDirectory (const juce::File& dir)
{
    const std::lock_guard<std::mutex> lock (mutex);
    directory = dir;
}

juce::File AudioCapture::getDirectory() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return directory;
}

void AudioCapture::push (const float* const* channels, int channelCount, int numSamples, int64_t hostSample) noexcept
{
    if (numSamples <= 0 || channelCount <= 0 || channels == nullptr || rings.empty())
        return;
    if (sampleFifo.getFreeSpace() < numSamples || headerFifo.getFreeSpace() < 1)
    {
        dropped.fetch_add (1, std::memory_order_relaxed);
        return;
    }

    const int stored = static_cast<int> (rings.size());
    {
        const auto scope = sampleFifo.write (numSamples);
        for (int c = 0; c < stored; ++c)
        {
            const float* src = channels[std::min (c, channelCount - 1)];
            auto* ring = rings[static_cast<size_t> (c)].data();
            std::copy (src, src + scope.blockSize1, ring + scope.startIndex1);
            std::copy (src + scope.blockSize1, src + scope.blockSize1 + scope.blockSize2, ring + scope.startIndex2);
        }
    }
    {
        const auto scope = headerFifo.write (1);
        const int index = scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2;
        headers[static_cast<size_t> (index)] = { hostSample, numSamples };
    }
}

void AudioCapture::run()
{
    while (! threadShouldExit())
    {
        {
            const std::lock_guard<std::mutex> lock (drainMutex);
            drain();
            if (endRequested.exchange (false))
                finishPass();
        }
        wait (20);
    }
}

void AudioCapture::flush()
{
    const std::lock_guard<std::mutex> lock (drainMutex);
    drain();
    if (endRequested.exchange (false))
        finishPass();
}

std::vector<AudioPass> AudioCapture::takeFinishedPasses()
{
    const std::lock_guard<std::mutex> lock (mutex);
    std::vector<AudioPass> out;
    out.swap (finished);
    return out;
}

void AudioCapture::drain()
{
    const std::lock_guard<std::mutex> ringLock (ringMutex);
    const int ch = static_cast<int> (rings.size());
    for (;;)
    {
        Header h;
        {
            const auto scope = headerFifo.read (1);
            if (scope.blockSize1 + scope.blockSize2 == 0)
                break;
            h = headers[static_cast<size_t> (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        }

        scratch.resize (static_cast<size_t> (ch));
        for (auto& s : scratch)
            s.resize (static_cast<size_t> (h.numSamples));
        {
            const auto scope = sampleFifo.read (h.numSamples);
            for (int c = 0; c < ch; ++c)
            {
                const auto* ring = rings[static_cast<size_t> (c)].data();
                auto* dst = scratch[static_cast<size_t> (c)].data();
                std::copy (ring + scope.startIndex1, ring + scope.startIndex1 + scope.blockSize1, dst);
                std::copy (ring + scope.startIndex2, ring + scope.startIndex2 + scope.blockSize2, dst + scope.blockSize1);
            }
        }

        if (writer != nullptr && (h.hostSample != expectedNext || current.channels != ch))
            finishPass(); // the host jumped (loop / relocation): new pass

        if (writer == nullptr)
        {
            const auto dir = getDirectory();
            if (dir == juce::File() || ! dir.createDirectory())
            {
                dropped.fetch_add (1, std::memory_order_relaxed);
                continue;
            }
            current = {};
            current.fileName = "pass-" + juce::String (juce::Time::currentTimeMillis()) + "-" + juce::String (++passCounter) + ".wav";
            current.start = h.hostSample;
            current.sampleRate = rate.load();
            current.channels = ch;

            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (dir.getChildFile (current.fileName));
            if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
            {
                dropped.fetch_add (1, std::memory_order_relaxed);
                continue;
            }
            juce::WavAudioFormat wav;
            writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                      .withSampleRate (current.sampleRate)
                                                      .withNumChannels (ch)
                                                      .withBitsPerSample (24));
            if (writer == nullptr)
            {
                dropped.fetch_add (1, std::memory_order_relaxed);
                continue;
            }
        }

        std::array<const float*, kMaxChannels> ptrs {};
        for (int c = 0; c < ch; ++c)
            ptrs[static_cast<size_t> (c)] = scratch[static_cast<size_t> (c)].data();
        writer->writeFromFloatArrays (ptrs.data(), ch, h.numSamples);
        current.length += h.numSamples;
        expectedNext = h.hostSample + h.numSamples;
    }
}

void AudioCapture::finishPass()
{
    if (writer == nullptr)
        return;
    writer.reset(); // writes the WAV header and closes the file
    if (current.length > 0)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        finished.push_back (current);
    }
    current = {};
    expectedNext = -1;
}

} // namespace novapitch
