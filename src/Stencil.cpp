/// \author Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// \date March 2017

#include "Stencil.h"
#include <hls_stream.h>
#include <cassert>
#ifndef BENCHMARK_SYNTHESIS
#include <thread>
#endif
#include <hlslib/Stream.h>

void Read(Memory_t const *input, hlslib::Stream<Memory_t> &buffer) {
ReadTime:
  for (int t = 0; t < kTime; ++t) {
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
  for (int t = 0; t < kTime; ++t) {
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

void Compute(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Kernel_t> &out) {
ComputeTime:
  for (int t = 0; t < kTime; ++t) {
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
            hlslib::WriteBlocking(out, result, 256);
          }
        }
      }
    }
  }
}

void Narrow(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Memory_t> &out) {
  Memory_t memoryBlock;
NarrowTime:
  for (int t = 0; t < kTime; ++t) {
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
  for (int t = 0; t < kTime; ++t) {
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
#ifndef BENCHMARK_SYNTHESIS
  hlslib::Stream<Memory_t> readBuffer("readBuffer");
  hlslib::Stream<Kernel_t> kernelPipeIn("kernelPipeIn");
  hlslib::Stream<Kernel_t> kernelPipeOut("kernelPipeOut");
  hlslib::Stream<Memory_t> writeBuffer("writeBuffer");
  std::thread read(Read, in, std::ref(readBuffer));
  std::thread widen(Widen, std::ref(readBuffer), std::ref(kernelPipeIn));
  std::thread compute(Compute, std::ref(kernelPipeIn), std::ref(kernelPipeOut));
  std::thread narrow(Narrow, std::ref(kernelPipeOut), std::ref(writeBuffer));
  std::thread write(Write, std::ref(writeBuffer), out);
  read.join();
  widen.join();
  compute.join();
  narrow.join();
  write.join();
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
  Compute(kernelPipeIn, kernelPipeOut);
  Narrow(kernelPipeOut, writeBuffer);
  Write(writeBuffer, out);
#endif
}

