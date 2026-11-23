// NovaPitchRender — offline render of a WAV/AIFF/FLAC file through NovaPitch,
// for A/B listening tests without a DAW.
//
// Usage:
//   NovaPitchRender <input> <output.wav> [paramId=value ...] [--block N]
// Values are real parameter values (as shown in the UI), e.g.
//   NovaPitchRender voz.wav voz_hardtune.wav retuneSpeed=0 key=9 scale=1 qualityMode=1
// Choice parameters take the item index (key: 0=C ... 11=B; scale: 0=Mayor,
// 1=Menor natural, ... 4=Cromática; qualityMode: 0=Live 1=Tracking 2=HQ 3=Vintage).
// The plugin latency is removed from the output so it lines up with the input.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/PluginProcessor.h"

#include <iostream>

using namespace novapitch;

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 3)
    {
        std::cout << "Usage: NovaPitchRender <input> <output.wav> [paramId=value ...] [--block N]\n\nParameters:\n";
        NovaPitchProcessor p;
        for (auto* param : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                std::cout << "  " << ranged->getParameterID() << "  (" << ranged->getName (64) << ", default "
                          << ranged->getCurrentValueAsText() << ")\n";
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto inFile = cwd.getChildFile (argv[1]);
    const auto outFile = cwd.getChildFile (argv[2]);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr)
    {
        std::cerr << "Cannot read " << inFile.getFullPathName() << std::endl;
        return 1;
    }

    const double sr = reader->sampleRate;
    const int numIn = static_cast<int> (juce::jmin (2u, reader->numChannels));
    const auto length = static_cast<int> (reader->lengthInSamples);

    NovaPitchProcessor proc;
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (numIn == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.add (numIn == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    proc.setBusesLayout (layout);

    int block = 256;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--block" && i + 1 < argc)
        {
            block = juce::jmax (1, juce::String (argv[++i]).getIntValue());
            continue;
        }
        const auto id = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto value = arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        auto* param = proc.getValueTreeState().getParameter (id);
        if (param == nullptr)
        {
            std::cerr << "Unknown parameter: " << id << std::endl;
            return 1;
        }
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        std::cout << id << " = " << param->getCurrentValueAsText() << std::endl;
    }

    proc.prepareToPlay (sr, block);
    const int latency = proc.getLatencySamples();

    // Input + `latency` samples of silence, so the tail comes out too.
    juce::AudioBuffer<float> audio (numIn, length + latency);
    audio.clear();
    reader->read (&audio, 0, length, 0, true, numIn > 1);

    juce::MidiBuffer midi;
    for (int pos = 0; pos < audio.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, audio.getNumSamples() - pos);
        juce::AudioBuffer<float> view (audio.getArrayOfWritePointers(), numIn, pos, n);
        proc.processBlock (view, midi);
    }

    juce::AudioBuffer<float> aligned (numIn, length);
    for (int ch = 0; ch < numIn; ++ch)
        aligned.copyFrom (ch, 0, audio, ch, latency, length);

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outFile);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (sr)
                                                   .withNumChannels (numIn)
                                                   .withBitsPerSample (24));
    if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (aligned, 0, length))
    {
        std::cerr << "Cannot write " << outFile.getFullPathName() << std::endl;
        return 1;
    }
    std::cout << "Rendered " << outFile.getFullPathName() << " (" << length / sr << " s, latency " << latency
              << " samples compensated)" << std::endl;
    return 0;
}
