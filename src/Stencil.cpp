/// \author Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// \date March 2017

#include "Stencil.h"
#include <hls_stream.h>
#include <cassert>
#ifndef STENCIL_SYNTHESIS
#include <thread>
#endif
#include <hlslib/Stream.h>
#include <hlslib/Utility.h>

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

template <int stage>
void Compute(hlslib::Stream<Kernel_t> &pipeIn,
             hlslib::Stream<Kernel_t> &pipeOut) {

  static constexpr int kInputWidth =
      kBlockWidthKernel + 2 * hlslib::CeilDivide(kDepth - stage, kKernelWidth);

  // Size of the halo in either side
  static constexpr int kBoundaryWidth = (kInputWidth - kBlockWidthKernel) / 2;

  // Begin and end indices of the inner block size (without any halos)
  static constexpr int kInnerBegin = kBoundaryWidth;
  static constexpr int kInnerEnd = kInputWidth - kBoundaryWidth;

  // Only shrink the output if we hit the boundary of the data width
  static constexpr bool kShrinkOutput =
      (kKernelWidth + kDepth - stage - 1) % kKernelWidth == 0;

  // The stencil radius is always smaller than the halo size, so we always
  // shrink by one
  static constexpr int kOutputBegin = kShrinkOutput ? 1 : 0;
  static constexpr int kOutputEnd =
      kShrinkOutput ? kInputWidth - 1 : kInputWidth;

  // The typedef seems to break the high level synthesis tool when applying
  // pragmas
#ifndef STENCIL_SYNTHESIS 
  hlslib::Stream<Kernel_t> northBuffer("northBuffer");
  hlslib::Stream<Kernel_t> centerBuffer("centerBuffer");
#else
  hls::stream<Kernel_t> northBuffer("northBuffer");
  #pragma HLS STREAM variable=northBuffer depth=kInputWidth
  #pragma HLS RESOURCE variable=northBuffer core=FIFO_BRAM
  hls::stream<Kernel_t> centerBuffer("centerBuffer");
  #pragma HLS STREAM variable=centerBuffer depth=kInputWidth
  #pragma HLS RESOURCE variable=centerBuffer core=FIFO_BRAM
#endif

  int t = 0;
  int b = 0;
  int r = 0;
  int c = 0;

  Data_t shiftWest(kBoundary);
  Kernel_t shiftCenter(kBoundary);

ComputeFlat:
  for (long i = 0;
       i < kTimeFolded * kBlocks * kRows * kInputWidth + kInputWidth - 1; ++i) {
    #pragma HLS PIPELINE

#ifdef STENCIL_KERNEL_DEBUG
    std::stringstream debugStream;
    debugStream << "Stage " << stage << ": (" << t << ", " << b << ", "
                << r << ", " << c - kBoundaryWidth << "): ";
    bool debugCond = stage == 0 && r == 0 && c - kBoundaryWidth == 0 && b == 0;
#endif

    const bool isSaturating = i < kInputWidth - 1; // Shift right by one 
    const bool isDraining = i >= kTimeFolded * kBlocks * kRows * kInputWidth;
    const bool inBounds = ((b > 0 || c >= kInnerBegin) &&
                           (b < kBlocks - 1 || c < kInnerEnd));
    const bool inBlock = c >= kInnerBegin && c < kInnerEnd;

    if (isSaturating) {
      Kernel_t read;
      if (i >= kInnerBegin - 1) { // Shift right by one
        read = hlslib::ReadBlocking(pipeIn);
      } else {
        read = Kernel_t(kBoundary);
      }
      hlslib::WriteOptimistic(centerBuffer, read, kInputWidth);
#ifdef STENCIL_KERNEL_DEBUG
      debugStream << "saturating " << read << "\n";
      if (debugCond) {
        std::cout << debugStream.str();
      }
#endif
    } else { // Use else instead of continue or the whole pipeline breaks...

      Kernel_t read;
      if (!isDraining) {
        // If not on the last row, check if the column in this block is out
        // of bounds. If on the last row, check if the column is out of
        // bounds in the NEXT block. Written below in DNF form:
        if ((b == 0 && c < kInnerBegin && r < kRows - 1) ||
            (b == kBlocks - 1 && c < kInnerBegin && r == kRows - 1) ||
            (b == kBlocks - 1 && c >= kInnerEnd && r < kRows - 1) ||
            (b == kBlocks - 2 && c >= kInnerEnd && r == kRows - 1)) {

          read = Kernel_t(kBoundary);

        } else {

          read = hlslib::ReadBlocking(pipeIn);

        }
      }

      // Collect vertical values
      const auto north =
          (r > 0) ? hlslib::ReadOptimistic(northBuffer) : Kernel_t(kBoundary);
      const auto south = (r < kRows - 1) ? read : Kernel_t(kBoundary);

      // Use center value shifted forward to populate bulk of west and east
      // vectors
      Kernel_t west, east;
      shiftCenter.ShiftTo<0, 1, kKernelWidth - 1>(west);
      shiftCenter.ShiftTo<1, 0, kKernelWidth - 1>(east);

      // Read the next center value to collect the last element of east
      const auto nextCenter = hlslib::ReadOptimistic(centerBuffer);
      east[kKernelWidth - 1] = nextCenter[0];

      // We shifted the last element of west forward from last iteration
      west[0] = shiftWest;

      // Now update the line buffers
      if (!isDraining) {
        hlslib::WriteOptimistic(centerBuffer, read, kInputWidth);
        if (r < kRows - 1) {
          hlslib::WriteOptimistic(northBuffer, shiftCenter, kInputWidth);
        }
      }

      // Values have been consumed, so shift all center registers left 
      shiftWest = shiftCenter[kKernelWidth - 1];
      shiftCenter = nextCenter;

#ifdef STENCIL_KERNEL_DEBUG
      debugStream << "N" << north << ", W" << west << ", E" << east << ", S"
                  << south;
#endif

      // Now we can perform the actual compute
      Kernel_t result;
    ComputeSIMD:
      for (int w = 0; w < kKernelWidth; ++w) {
        #pragma HLS UNROLL
        const Data_t northVal = north[w];
        const Data_t westVal = west[w];
        const Data_t eastVal = east[w];
        const Data_t southVal = south[w];
        const Data_t factor = 0.25;
        result[w] = factor * (northVal + westVal + eastVal + southVal);
      }

      // Only output values if the next unit needs them
      if (c >= kOutputBegin && c < kOutputEnd && inBounds) {
        Kernel_t write;
        if (inBounds) {
          write = result;
        } else {
          write = Kernel_t(kBoundary);
        }
        hlslib::WriteBlocking(pipeOut, write, kPipeDepth);
#ifdef STENCIL_KERNEL_DEBUG
        if (debugCond) {
          debugStream << " -> " << write << "\n"; 
        }
#endif
      } else {
#ifdef STENCIL_KERNEL_DEBUG
        if (debugCond) {
          debugStream << " no output.\n";
        }
#endif
      }

#ifdef STENCIL_KERNEL_DEBUG
      if (debugCond) {
        std::cout << debugStream.str();
      }
#endif

      // Index calculations
      if (c == kInputWidth - 1) {
        c = 0;
        if (r == kRows - 1) {
          r = 0;
          if (b == kBlocks - 1) {
            b = 0;
            ++t;
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

#ifdef STENCIL_SYNTHESIS

template <int stage>
void UnrollCompute(hlslib::Stream<Kernel_t> &previous,
                   hlslib::Stream<Kernel_t> &last) {
  #pragma HLS INLINE
  hls::stream<Kernel_t> next("pipe");
  #pragma HLS STREAM variable=next depth=kPipeDepth
  Compute<kDepth - stage>(previous, next);
  UnrollCompute<stage - 1>(next, last);
}

template <>
void UnrollCompute<1>(hlslib::Stream<Kernel_t> &previous,
                      hlslib::Stream<Kernel_t> &last) {
#pragma HLS INLINE
  Compute<kDepth - 1>(previous, last);
}

#else

template <int stage>
void UnrollCompute(hlslib::Stream<Kernel_t> &previous,
                   hlslib::Stream<Kernel_t> &last,
                   std::vector<std::thread> &threads) {
  static hlslib::Stream<Kernel_t> next("pipe");
  threads.emplace_back(Compute<kDepth - stage>, std::ref(previous),
                       std::ref(next));
  UnrollCompute<stage - 1>(next, last, threads);
}

template <>
void UnrollCompute<1>(hlslib::Stream<Kernel_t> &previous,
                      hlslib::Stream<Kernel_t> &last,
                      std::vector<std::thread> &threads) {
  threads.emplace_back(Compute<kDepth - 1>, std::ref(previous), std::ref(last));
}

#endif

void ComputeEmpty(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Kernel_t> &out) {
ComputeTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  ComputeBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    ComputeRows:
      for (int r = 0; r < kRows; ++r) {
      ComputeCols:
        for (int c = 0; c < kBlockWidthKernel + 2 * kHaloKernel; ++c) {
          #pragma HLS LOOP_FLATTEN
          #pragma HLS PIPELINE
          Kernel_t read;
          if ((c >= kHaloKernel || b > 0) &&
              (c < kBlockWidthKernel + kHaloKernel || b < kBlocks - 1)) {
            read = hlslib::ReadBlocking(in);
          } else {
            read = Kernel_t(static_cast<Data_t>(0));
          }
          Kernel_t result;
        SIMD:
          for (int w = 0; w < kKernelWidth; ++w) {
            #pragma HLS UNROLL
            result[w] = read[w] + 1;
          }
          if (c >= kHaloKernel && c < kBlockWidthKernel + kHaloKernel) {
            hlslib::WriteBlocking(out, result, kMemoryBufferDepth);
          }
        }
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

void Jacobi(Memory_t const *in, Memory_t *out) {
  #pragma HLS INTERFACE m_axi port=in offset=slave bundle=gmem0
  #pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem1
  #pragma HLS INTERFACE s_axilite port=in     bundle=control 
  #pragma HLS INTERFACE s_axilite port=out    bundle=control 
  #pragma HLS INTERFACE s_axilite port=return bundle=control 
  #pragma HLS DATAFLOW
#ifndef STENCIL_SYNTHESIS
  std::vector<std::thread> threads;
  hlslib::Stream<Memory_t> readBuffer("readBuffer");
  hlslib::Stream<Kernel_t> kernelPipeIn("kernelPipeIn");
  hlslib::Stream<Kernel_t> kernelPipeOut("kernelPipeOut");
  hlslib::Stream<Memory_t> writeBuffer("writeBuffer");
  threads.emplace_back(Read, in, std::ref(readBuffer));
  threads.emplace_back(Widen, std::ref(readBuffer), std::ref(kernelPipeIn));
#ifdef STENCIL_NO_KERNEL
  threads.emplace_back(ComputeEmpty, std::ref(kernelPipeIn),
                       std::ref(kernelPipeOut));
#else
  UnrollCompute<kDepth>(kernelPipeIn, kernelPipeOut, threads);
#endif
  threads.emplace_back(Narrow, std::ref(kernelPipeOut), std::ref(writeBuffer));
  threads.emplace_back(Write, std::ref(writeBuffer), out);
  for (auto &t : threads) {
    t.join();
  }
#else
  hls::stream<Memory_t> readBuffer("readBuffer");
  #pragma HLS STREAM variable=readBuffer depth=kMemoryBufferDepth
  hls::stream<Kernel_t> kernelPipeIn("kernelPipeIn");
  #pragma HLS STREAM variable=kernelPipeIn depth=kPipeDepth
  hls::stream<Kernel_t> kernelPipeOut("kernelPipeOut");
  #pragma HLS STREAM variable=kernelPipeOut depth=kPipeDepth
  hls::stream<Memory_t> writeBuffer("writeBuffer");
  #pragma HLS STREAM variable=writeBuffer depth=kMemoryBufferDepth
  Read(in, readBuffer);
  Widen(readBuffer, kernelPipeIn);
#ifdef STENCIL_NO_KERNEL
  ComputeEmpty(kernelPipeIn, kernelPipeOut);
#else
  UnrollCompute<kDepth>(kernelPipeIn, kernelPipeOut);
#endif
  Narrow(kernelPipeOut, writeBuffer);
  Write(writeBuffer, out);
#endif
}

