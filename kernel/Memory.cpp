/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#include "Memory.h"
#include <cassert>

void ReadSplit(Memory_t const *input, hlslib::Stream<Memory_t> &buffer,
               const int timesteps_folded) {
  static_assert(kRows % kDimms == 0, "Uneven memory split");
  static constexpr long kRowsSplit = kRows / kDimms;
  static constexpr long kTotalElementsSplit = kTotalElementsMemory / kDimms;
ReadTime:
  for (int t = 0; t < timesteps_folded; ++t) {
  ReadBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    ReadRows:
      for (int r = 0; r < kRowsSplit; ++r) {
      ReadCols:
        for (int c = 0; c < kBlockWidthMemory + 2 * kHaloMemory; ++c) {
#pragma HLS LOOP_FLATTEN
#pragma HLS PIPELINE
          const auto offset = (t % 2 == 1) ? kTotalElementsSplit : 0;
          const auto shift =
              (b == 0) ? 0
                       : ((b == kBlocks - 1) ? (2 * kHaloMemory) : kHaloMemory);
          const auto index = offset + r * kBlockWidthMemory * kBlocks +
                             b * kBlockWidthMemory + c - shift;
          assert(index >= 0);
          assert(index < 2 * kTotalElementsSplit);
          const auto read = input[index];
          if ((b > 0 || c < kBlockWidthMemory + kHaloMemory) &&
              (b < kBlocks - 1 || c >= kHaloMemory)) {
            buffer.Push(read);
          }
        }
      }
    }
  }
}

// Two DIMM demux
void DemuxRead(hlslib::Stream<Memory_t> &buffer0,
               hlslib::Stream<Memory_t> &buffer1,
               hlslib::Stream<Memory_t> &pipe, const int timesteps_folded) {
  int b = 0;
  int r = 0;
  int c = 0;
DemuxTime:
  for (int t = 0; t < timesteps_folded; ++t) {
  DemuxSpace:
    for (int i = 0; i < kTotalInputMemory; ++i) {
#pragma HLS LOOP_FLATTEN
#pragma HLS PIPELINE
      if (r % 2 == 0) {
        pipe.Push(buffer0.Pop());
      } else {
        pipe.Push(buffer1.Pop());
      }
      const bool lastCol = ((b == 0 || b == kBlocks - 1) &&
                            c == kBlockWidthMemory + kHaloMemory - 1) ||
                           ((b > 0 && b < kBlocks - 1) &&
                            c == kBlockWidthMemory + 2 * kHaloMemory - 1);
      // We need nasty index calculations due to the irregular loop structure
      if (lastCol) {
        c = 0;
        if (r == kRows - 1) {
          r = 0;
          if (b == kBlocks - 1) {
            b = 0;
          } else {
            ++b;
          }
        } else {
          ++r;
        }
      } else {
        ++c;
      }
    }
  }
}

/// Convert from memory width to kernel width
void Widen(hlslib::Stream<Memory_t> &in, hlslib::Stream<Kernel_t> &out,
           const int timesteps_folded) {
  Memory_t memoryBlock;
  bool readNext = true;
  unsigned char memIndex = 0;
  int b = 0;
  int r = 0;
  int c = 0;
WidenTime:
  for (int t = 0; t < timesteps_folded; ++t) {
  WidenSpace:
    for (int i = 0; i < kTotalInputKernel; ++i) {
#pragma HLS LOOP_FLATTEN
#pragma HLS PIPELINE

      if (readNext) {
        memoryBlock = in.Pop();
      }

      const Kernel_t elem = memoryBlock[memIndex];
      out.Push(elem);

      const bool lastCol = ((b == 0 || b == kBlocks - 1) &&
                            c == kBlockWidthKernel + kHaloKernel - 1) ||
                           ((b > 0 && b < kBlocks - 1) &&
                            c == kBlockWidthKernel + 2 * kHaloKernel - 1);
      const bool nextAligned =
          (b == 0 && r < kRows - 1) || (b == kBlocks - 1 && r == kRows - 1);

      // We need nasty index calculations due to the irregular loop structure
      if (lastCol) {
        c = 0;
        readNext = true;
        memIndex = nextAligned ? 0 : kAlignmentGap;
        if (r == kRows - 1) {
          r = 0;
          if (b == kBlocks - 1) {
            b = 0;
          } else {
            ++b;
          }
        } else {
          ++r;
        }
      } else {
        ++c;
        readNext = memIndex == kKernelPerMemory - 1;
        memIndex = (memIndex == kKernelPerMemory - 1) ? 0 : (memIndex + 1);
      }
    }
  }
}

void WriteSplit(hlslib::Stream<Memory_t> &buffer, Memory_t *output,
                const int timesteps_folded) {
  static constexpr long kRowsSplit = kRows / kDimms;
  static constexpr long kTotalElementsSplit = kTotalElementsMemory / kDimms;
WriteTime:
  for (int t = 0; t < timesteps_folded; ++t) {
  WriteBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    WriteRows:
      for (int r = 0; r < kRowsSplit; ++r) {
      WriteCols:
        for (int c = 0; c < kBlockWidthMemory; ++c) {
#pragma HLS LOOP_FLATTEN
#pragma HLS PIPELINE
          const auto offset = (t % 2 == 0) ? kTotalElementsSplit : 0;
          const auto read = buffer.Pop();
          const auto index = (offset + r * kBlockWidthMemory * kBlocks +
                              b * kBlockWidthMemory + c) %
                             (2 * kTotalElementsSplit);
          assert(index >= 0);
          assert(index < 2 * kTotalElementsSplit);
          output[index] = read;
        }
      }
    }
  }
}

void MuxWrite(hlslib::Stream<Memory_t> &pipe, hlslib::Stream<Memory_t> &buffer0,
              hlslib::Stream<Memory_t> &buffer1, const int timesteps_folded) {
MuxTime:
  for (int t = 0; t < timesteps_folded; ++t) {
  MuxBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    MuxRows:
      for (int r = 0; r < kRows; ++r) {
      MuxCols:
        for (int c = 0; c < kBlockWidthMemory; ++c) {
#pragma HLS LOOP_FLATTEN
#pragma HLS PIPELINE
          const auto read = pipe.Pop();
          if (r % 2 == 0) {
            buffer0.Push(read);
          } else {
            buffer1.Push(read);
          }
        }
      }
    }
  }
}

/// Convert from kernel width to memory width
void Narrow(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Memory_t> &out,
            const int timesteps_folded) {
  Memory_t memoryBlock;
NarrowTime:
  for (int t = 0; t < timesteps_folded; ++t) {
  NarrowBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    NarrowRows:
      for (int r = 0; r < kRows; ++r) {
      NarrowCol:
        for (int c = 0; c < kBlockWidthKernel; ++c) {
#pragma HLS LOOP_FLATTEN
#pragma HLS PIPELINE
          const auto read = in.Pop();
          const auto memIndex = c % kKernelPerMemory;
          memoryBlock[memIndex] = read;
          if (memIndex == kKernelPerMemory - 1) {
            out.Push(memoryBlock);
          }
        }
      }
    }
  }
}
