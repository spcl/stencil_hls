/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @date      March 2017 
/// @copyright This software is copyrighted under the BSD 3-Clause License. 

#include "Memory.h"
#include <cassert>
#ifndef STENCIL_SYNTHESIS
#include <thread>
#endif

template <int dimms>
void ReadSplit(Memory_t const *input, hlslib::Stream<Memory_t> &buffer) {
  static_assert(kRows % dimms == 0, "Uneven memory split");
  static constexpr long kRowsSplit = kRows / dimms;
  static constexpr long kTotalElementsSplit = kTotalElementsMemory / dimms;
ReadTime:
  for (int t = 0; t < kTimeFolded; ++t) {
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
            hlslib::WriteBlocking(buffer, read, kMemoryBufferDepth);
          }
        }
      }
    }
  }
}

// Two DIMM demux
void DemuxRead(hlslib::Stream<Memory_t> &buffer0,
               hlslib::Stream<Memory_t> &buffer1,
               hlslib::Stream<Memory_t> &pipe) {
  int b = 0;
  int r = 0;
  int c = 0;
DemuxTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  DemuxSpace:
    for (int i = 0; i < kTotalInputMemory; ++i) {
      #pragma HLS LOOP_FLATTEN
      #pragma HLS PIPELINE
      if (r % 2 == 0) {
        hlslib::WriteBlocking(pipe, hlslib::ReadBlocking(buffer0), kPipeDepth);
      } else {
        hlslib::WriteBlocking(pipe, hlslib::ReadBlocking(buffer1), kPipeDepth);
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
void Widen(hlslib::Stream<Memory_t> &in, hlslib::Stream<Kernel_t> &out) {
  Memory_t memoryBlock;
  bool readNext = true;
  unsigned char memIndex = 0;
  int b = 0;
  int r = 0;
  int c = 0;
WidenTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  WidenSpace:
    for (int i = 0; i < kTotalInputKernel; ++i) {
      #pragma HLS LOOP_FLATTEN
      #pragma HLS PIPELINE

      if (readNext) {
        memoryBlock = hlslib::ReadBlocking(in);
      }

      const Kernel_t elem = memoryBlock[memIndex];
      hlslib::WriteBlocking(out, elem, kPipeDepth);

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

template <int dimms>
void WriteSplit(hlslib::Stream<Memory_t> &buffer, Memory_t *output) {
  static_assert(kRows % dimms == 0, "Uneven memory split");
  static constexpr long kRowsSplit = kRows / dimms;
  static constexpr long kTotalElementsSplit = kTotalElementsMemory / dimms;
WriteTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  WriteBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    WriteRows:
      for (int r = 0; r < kRowsSplit; ++r) {
      WriteCols:
        for (int c = 0; c < kBlockWidthMemory; ++c) {
          #pragma HLS LOOP_FLATTEN
          #pragma HLS PIPELINE
          const auto offset = (t % 2 == 0) ? kTotalElementsSplit : 0;
          const auto read = hlslib::ReadBlocking(buffer);
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
              hlslib::Stream<Memory_t> &buffer1) {
MuxTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  MuxBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    MuxRows:
      for (int r = 0; r < kRows; ++r) {
      MuxCols:
        for (int c = 0; c < kBlockWidthMemory; ++c) {
          #pragma HLS LOOP_FLATTEN
          #pragma HLS PIPELINE
          const auto read = hlslib::ReadBlocking(pipe);
          if (r % 2 == 0) {
            hlslib::WriteBlocking(buffer0, read, kMemoryBufferDepth);
          } else {
            hlslib::WriteBlocking(buffer1, read, kMemoryBufferDepth);
          }
        }
      }
    }
  }
}

/// Convert from kernel width to memory width
void Narrow(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Memory_t> &out) {
  Memory_t memoryBlock;
NarrowTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  NarrowBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    NarrowRows:
      for (int r = 0; r < kRows; ++r) {
      NarrowCol:
        for (int c = 0; c < kBlockWidthKernel; ++c) {
          #pragma HLS LOOP_FLATTEN
          #pragma HLS PIPELINE
          const auto read = hlslib::ReadBlocking(in);
          const auto memIndex = c % kKernelPerMemory;
          memoryBlock[memIndex] = read;
          if (memIndex == kKernelPerMemory - 1) {
            hlslib::WriteBlocking(out, memoryBlock, kMemoryBufferDepth);
          }
        }
      }
    }
  }
}

#ifndef STENCIL_SYNTHESIS

// Single DIMM read
void Read(Memory_t const *memory, hlslib::Stream<Kernel_t> &toKernel,
          std::vector<std::thread> &threads) {
  #pragma HLS INLINE
  static hlslib::Stream<Memory_t> readBuffer("readBuffer");
  threads.emplace_back(ReadSplit<1>, memory, std::ref(readBuffer));
  threads.emplace_back(Widen, std::ref(readBuffer), std::ref(toKernel));
}

// Dual DIMM read
void Read(Memory_t const *memory0, Memory_t const *memory1,
          hlslib::Stream<Kernel_t> &toKernel,
          std::vector<std::thread> &threads) {
  static hlslib::Stream<Memory_t> readBuffer0("readBuffer0");
  static hlslib::Stream<Memory_t> readBuffer1("readBuffer1");
  static hlslib::Stream<Memory_t> demuxPipe;
  threads.emplace_back(ReadSplit<2>, memory0, std::ref(readBuffer0));
  threads.emplace_back(ReadSplit<2>, memory1, std::ref(readBuffer1));
  threads.emplace_back(DemuxRead, std::ref(readBuffer0), std::ref(readBuffer1),
                       std::ref(demuxPipe));
  threads.emplace_back(Widen, std::ref(demuxPipe), std::ref(toKernel));
}

// Single DIMM write
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory,
           std::vector<std::thread> &threads) {
  #pragma HLS INLINE
  static hlslib::Stream<Memory_t> writeBuffer("writeBuffer");
  threads.emplace_back(Narrow, std::ref(fromKernel), std::ref(writeBuffer));
  threads.emplace_back(WriteSplit<1>, std::ref(writeBuffer), memory);
}

// Dual DIMM write
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory0,
           Memory_t *memory1, std::vector<std::thread> &threads) {
  static hlslib::Stream<Memory_t> writeBuffer0("writeBuffer0");
  static hlslib::Stream<Memory_t> writeBuffer1("writeBuffer1");
  static hlslib::Stream<Memory_t> muxPipe("muxPipe");
  threads.emplace_back(Narrow, std::ref(fromKernel), std::ref(muxPipe));
  threads.emplace_back(MuxWrite, std::ref(muxPipe), std::ref(writeBuffer0),
                       std::ref(writeBuffer1));
  threads.emplace_back(WriteSplit<2>, std::ref(writeBuffer0), memory0);
  threads.emplace_back(WriteSplit<2>, std::ref(writeBuffer1), memory1);
}

#else

// Single DIMM read
void Read(Memory_t const *memory, hlslib::Stream<Kernel_t> &toKernel) {
  #pragma HLS INLINE
  hlslib::Stream<Memory_t> readBuffer("readBuffer", kMemoryBufferDepth);
  ReadSplit<1>(memory, readBuffer);
  Widen(readBuffer, toKernel);
}

// Dual DIMM read
void Read(Memory_t const *memory0, Memory_t const *memory1,
          hlslib::Stream<Kernel_t> &toKernel) {
  #pragma HLS INLINE
  hlslib::Stream<Memory_t> readBuffer0("readBuffer0", kMemoryBufferDepth);
  hlslib::Stream<Memory_t> readBuffer1("readBuffer1", kMemoryBufferDepth);
  hlslib::Stream<Memory_t> demuxPipe("demuxPipe", kPipeDepth);
  ReadSplit<2>(memory0, readBuffer0);
  ReadSplit<2>(memory1, readBuffer1);
  DemuxRead(readBuffer0, readBuffer1, demuxPipe);
  Widen(demuxPipe, toKernel);
}

// Single DIMM write
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory) {
  #pragma HLS INLINE
  hlslib::Stream<Memory_t> writeBuffer("writeBuffer", kMemoryBufferDepth);
  Narrow(fromKernel, writeBuffer);
  WriteSplit<1>(writeBuffer, memory);
}

// Dual DIMM write
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory0,
           Memory_t *memory1) {
  #pragma HLS INLINE
  hlslib::Stream<Memory_t> writeBuffer0("writeBuffer0", kMemoryBufferDepth);
  hlslib::Stream<Memory_t> writeBuffer1("writeBuffer1", kMemoryBufferDepth);
  hlslib::Stream<Memory_t> muxPipe("muxPipe", kPipeDepth);
  Narrow(fromKernel, muxPipe);
  MuxWrite(muxPipe, writeBuffer0, writeBuffer1);
  WriteSplit<2>(writeBuffer0, memory0);
  WriteSplit<2>(writeBuffer1, memory1);
}

#endif
