#include "Memory.h"

void Read(Memory_t const *input, hlslib::Stream<Memory_t> &buffer) {
ReadTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  ReadBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    ReadRows:
      for (int r = 0; r < kRows; ++r) {
      ReadCols:
        for (int c = 0; c < kBlockWidthMemory + 2 * kHaloMemory; ++c) {
          #pragma HLS LOOP_FLATTEN
          #pragma HLS PIPELINE
          const auto offset = (t % 2 == 1) ? kTotalElementsMemory : 0;
          const auto shift =
              (b == 0) ? 0
                       : ((b == kBlocks - 1) ? (2 * kHaloMemory) : kHaloMemory);
          const auto index = offset + r * kBlockWidthMemory * kBlocks +
                             b * kBlockWidthMemory + c - shift;
          assert(index >= 0);
          assert(index < 2 * kTotalElementsMemory);
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

void Write(hlslib::Stream<Memory_t> &buffer, Memory_t *output) {
WriteTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  WriteBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    WriteRows:
      for (int r = 0; r < kRows; ++r) {
      WriteCols:
        for (int c = 0; c < kBlockWidthMemory; ++c) {
          #pragma HLS LOOP_FLATTEN
          #pragma HLS PIPELINE
          const auto offset = (t % 2 == 0) ? kTotalElementsMemory : 0;
          const auto read = hlslib::ReadBlocking(buffer);
          const auto index = (offset + r * kBlockWidthMemory * kBlocks +
                              b * kBlockWidthMemory + c) %
                             (2 * kTotalElementsMemory);
          assert(index >= 0);
          assert(index < 2 * kTotalElementsMemory);
          output[index] = read;
        }
      }
    }
  }
}
