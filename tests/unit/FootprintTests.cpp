// The processor object must stay small: some hosts and tests create it on the
// stack, and Windows threads have a 1 MB stack. Large buffers live on the heap.

#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"

using namespace novapitch;

TEST_CASE ("Processor footprint stays well below a Windows thread stack", "[robustness]")
{
    INFO ("sizeof (NovaPitchProcessor) = " << sizeof (NovaPitchProcessor) << " bytes; PitchFrameFifo "
                                           << sizeof (PitchFrameFifo) << ", CaptureFifo " << sizeof (CaptureFifo)
                                           << ", HarmonyEngine " << sizeof (dsp::HarmonyEngine) << ", PsolaShifter "
                                           << sizeof (dsp::PsolaShifter) << ", PitchDetector " << sizeof (dsp::PitchDetector));
    CHECK (sizeof (NovaPitchProcessor) < 192 * 1024);
}
