#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace novapitch
{

/** One contiguous capture pass written to disk. Positions are host-timeline
    samples at `sampleRate`. */
struct AudioPass
{
    juce::String fileName; ///< relative to the capture directory
    int64_t start = 0;
    int64_t length = 0;
    double sampleRate = 48000.0;
    int channels = 1;
    double startSeconds() const noexcept { return static_cast<double> (start) / sampleRate; }
    double endSeconds() const noexcept { return static_cast<double> (start + length) / sampleRate; }
};

/** Records the plugin input while the graphical mode captures, without ever
    blocking the audio thread:

      audio thread --(lock-free sample rings + block headers)--> writer thread
      --> 24-bit WAV, one file per contiguous pass (a jump of the host position
      starts a new pass) --> finished passes collected by the message thread.

    If the rings are full (disk too slow) a block is dropped, never waited for. */
class AudioCapture : private juce::Thread
{
public:
    static constexpr double kRingSeconds = 4.0;
    static constexpr int kMaxChannels = 2;

    AudioCapture();
    ~AudioCapture() override;

    /** Allocates the rings (message thread, audio stopped: prepareToPlay). */
    void prepare (double sampleRate, int numChannels);
    /** Folder of the pass files (message thread). Created on first write. */
    void setDirectory (const juce::File& dir);
    juce::File getDirectory() const;

    /** Audio thread: input of one block at host position `hostSample`. */
    void push (const float* const* channels, int numChannels, int numSamples, int64_t hostSample) noexcept;

    /** Message thread: ends the current pass once everything pushed so far is written. */
    void endPass() noexcept { endRequested.store (true); notify(); }

    /** Blocks until the rings are drained and an ended pass is finalised (tests / shutdown). */
    void flush();

    /** Finished passes since the last call (message thread). */
    std::vector<AudioPass> takeFinishedPasses();

    int64_t getDroppedBlocks() const noexcept { return dropped.load(); }

private:
    struct Header
    {
        int64_t hostSample = 0;
        int numSamples = 0;
    };

    void run() override;
    void drain();
    void finishPass();

    // Rings (written by the audio thread, read by the writer thread)
    std::mutex ringMutex; // writer thread vs prepare() only; the audio thread never takes it
    std::vector<std::vector<float>> rings;
    std::vector<Header> headers;
    juce::AbstractFifo sampleFifo { 2 }, headerFifo { 2 };
    std::atomic<int> numChannels { 1 };
    std::atomic<double> rate { 48000.0 };
    std::atomic<int64_t> dropped { 0 };
    std::atomic<bool> endRequested { false };

    // Writer thread state
    std::unique_ptr<juce::AudioFormatWriter> writer;
    AudioPass current;
    int64_t expectedNext = -1;
    int passCounter = 0;
    std::vector<std::vector<float>> scratch;

    mutable std::mutex mutex; // directory + finished passes
    juce::File directory;
    std::vector<AudioPass> finished;
    std::mutex drainMutex;
};

} // namespace novapitch
