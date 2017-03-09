#include "hlslib/Stream.h"
#include "hlslib/Utility.h"
#include "Stencil.h"
#include <cassert>
#ifndef STENCIL_SYNTHESIS
#include <thread>
#endif

void Read(DataPack const *memory, hlslib::Stream<DataPack> &pipeIn) {
ReadTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  ReadBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    ReadRows:
      for (int r = 0; r < kRows / 2; ++r) {
      ReadCols:
        for (int c = 0; c < kColsPerBlock + 2 * kHalo; ++c) {
          #pragma HLS PIPELINE
          #pragma HLS LOOP_FLATTEN
          const auto offset =
              (b == 0) ? 0 : ((b == kBlocks - 1) ? (2 * kHalo) : kHalo);
          const auto index = (t % 2) * (kWriteSize / 2) + r * kCols +
                             b * kColsPerBlock + c - offset;
          assert(index >= 0);
          assert(index < kWriteSize);
          const auto read = memory[index];
          if ((b > 0 || c < kColsPerBlock + kHalo) &&
              (b < kBlocks - 1 || c >= kHalo)) {
            hlslib::WriteBlocking(pipeIn, read, kPipeDepth);
          }
        } // End cols
      } // End rows
    } // End blocks
  } // End time
}

void Demux(hlslib::Stream<DataPack> &pipeIn0, hlslib::Stream<DataPack> &pipeIn1,
           hlslib::Stream<DataPack> &pipeIn) {

  int b = 0;
  int r = 0;
  int c = 0;

DemuxTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  DemuxSpatial:
    for (int i = 0; i < kReadSize; ++i) {
      #pragma HLS PIPELINE
      #pragma HLS LOOP_FLATTEN
      DataPack read;
      if (r % 2 == 0) {
        read = hlslib::ReadBlocking(pipeIn0);
      } else {
        read = hlslib::ReadBlocking(pipeIn1);
      }
      hlslib::WriteBlocking(pipeIn, read, kPipeDepth);
      if (((b == 0 || b == kBlocks - 1) && c == kColsPerBlock + kHalo - 1) ||
          c == kColsPerBlock + 2 * kHalo - 1) {
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

template <int stage>
void Compute(hlslib::Stream<DataPack> &pipeIn, hlslib::Stream<DataPack> &pipeOut) {

  static constexpr int kInputWidth =
      kColsPerBlock + 2 * hlslib::CeilDivide(kDepth - stage, kDataWidth);

  // Size of the halo in either side
  static constexpr int kBoundaryWidth = (kInputWidth - kColsPerBlock) / 2;

  // Begin and end indices of the inner block size (without any halos)
  static constexpr int kInnerBegin = kBoundaryWidth;
  static constexpr int kInnerEnd = kInputWidth - kBoundaryWidth;

  // Only shrink the output if we hit the boundary of the data width
  static constexpr bool kShrinkOutput =
      (kDataWidth + kDepth - stage - 1) % kDataWidth == 0;

  // The stencil radius is always smaller than the halo size, so we always
  // shrink by one
  static constexpr int kOutputBegin = kShrinkOutput ? 1 : 0;
  static constexpr int kOutputEnd =
      kShrinkOutput ? kInputWidth - 1 : kInputWidth;

  // The typedef seems to break the high level synthesis tool when applying
  // pragmas
#ifndef STENCIL_SYNTHESIS 
  hlslib::Stream<DataPack> northBuffer("northBuffer");
  hlslib::Stream<DataPack> centerBuffer("centerBuffer");
#else
  hls::stream<DataPack> northBuffer("northBuffer");
  #pragma HLS STREAM variable=northBuffer depth=kInputWidth
  #pragma HLS RESOURCE variable=northBuffer core=FIFO_BRAM
  hls::stream<DataPack> centerBuffer("centerBuffer");
  #pragma HLS STREAM variable=centerBuffer depth=kInputWidth
  #pragma HLS RESOURCE variable=centerBuffer core=FIFO_BRAM
#endif

  int t = 0;
  int b = 0;
  int r = 0;
  int c = 0;

  Data_t shiftWest(kBoundary);
  DataPack shiftCenter(kBoundary);

ComputeFlat:
  for (int i = 0;
       i < kTimeFolded * kBlocks * kRows * kInputWidth + kInputWidth - 1; ++i) {
    #pragma HLS PIPELINE

#ifdef STENCIL_KERNEL_DEBUG
    std::stringstream debugStream;
    debugStream << "Stage " << stage << ": (" << t << ", " << b << ", "
                << r << ", " << c - kBoundaryWidth << "): ";
    bool debugCond = stage == 0;
#endif

    const bool isSaturating = i < kInputWidth - 1; // Shift right by one 
    const bool isDraining = i >= kTimeFolded * kBlocks * kRows * kInputWidth;
    const bool inBounds = ((b > 0 || c >= kInnerBegin) &&
                           (b < kBlocks - 1 || c < kInnerEnd));
    const bool inBlock = c >= kInnerBegin && c < kInnerEnd;

    if (isSaturating) {
      DataPack read;
      if (i >= kInnerBegin - 1) { // Shift right by one
        read = hlslib::ReadBlocking(pipeIn);
      } else {
        read = DataPack(kBoundary);
      }
      hlslib::WriteOptimistic(centerBuffer, read, kInputWidth);
#ifdef STENCIL_KERNEL_DEBUG
      debugStream << "saturating " << read << "\n";
      if (debugCond) {
        std::cout << debugStream.str();
      }
#endif
      continue;
    }

    DataPack read;
    if (!isDraining) {
      // If not on the last row, check if the column in this block is out
      // of bounds. If on the last row, check if the column is out of
      // bounds in the NEXT block. Written below in DNF form:
      if ((b == 0 && c < kInnerBegin && r < kRows - 1) ||
          (b == kBlocks - 1 && c < kInnerBegin && r == kRows - 1) ||
          (b == kBlocks - 1 && c >= kInnerEnd && r < kRows - 1) ||
          (b == kBlocks - 2 && c >= kInnerEnd && r == kRows - 1)) {

        read = DataPack(kBoundary);

      } else {

        read = hlslib::ReadBlocking(pipeIn);

      }
    }

    // Collect vertical values
    const auto north =
        (r > 0) ? hlslib::ReadOptimistic(northBuffer) : DataPack(kBoundary);
    const auto south = (r < kRows - 1) ? read : DataPack(kBoundary);

    // Use center value shifted forward to populate bulk of west and east
    // vectors
    DataPack west, east;
    shiftCenter.ShiftTo<0, 1, kDataWidth - 1>(west);
    shiftCenter.ShiftTo<1, 0, kDataWidth - 1>(east);

    // Read the next center value to collect the last element of east
    const auto nextCenter = hlslib::ReadOptimistic(centerBuffer);
    east[kDataWidth - 1] = nextCenter[0];

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
    shiftWest = shiftCenter[kDataWidth - 1];
    shiftCenter = nextCenter;

#ifdef STENCIL_KERNEL_DEBUG
    debugStream << "N" << north << ", W" << west << ", E" << east << ", S"
                << south;
#endif

    // Now we can perform the actual compute
    DataPack result;
  ComputeSIMD:
    for (int w = 0; w < kDataWidth; ++w) {
      #pragma HLS UNROLL
      result[w] =
          static_cast<Data_t>(0.25) * (north[w] + west[w] + east[w] + south[w]);
    }

    // Only output values if the next unit needs them
    if (c >= kOutputBegin && c < kOutputEnd && inBounds) {
      DataPack write;
      if (inBounds) {
        write = result;
      } else {
        write = DataPack(kBoundary);
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

#ifdef STENCIL_SYNTHESIS

template <int stage>
void UnrollCompute(hlslib::Stream<DataPack> &previous,
                   hlslib::Stream<DataPack> &last) {
  #pragma HLS INLINE
  hls::stream<DataPack> next("pipe");
  #pragma HLS STREAM variable=next depth=kPipeDepth
  Compute<kDepth - stage>(previous, next);
  UnrollCompute<stage - 1>(next, last);
}

template <>
void UnrollCompute<1>(hlslib::Stream<DataPack> &previous,
                      hlslib::Stream<DataPack> &last) {
  #pragma HLS INLINE
  Compute<kDepth - 1>(previous, last);
}

#else

template <int stage>
void UnrollCompute(hlslib::Stream<DataPack> &previous, hlslib::Stream<DataPack> &last,
                   std::vector<std::thread> &threads) {
  static hlslib::Stream<DataPack> next("pipe");
  threads.emplace_back(Compute<kDepth - stage>, std::ref(previous),
                       std::ref(next));
  UnrollCompute<stage - 1>(next, last, threads);
} 
template <>
void UnrollCompute<1>(hlslib::Stream<DataPack> &previous,
                      hlslib::Stream<DataPack> &last,
                      std::vector<std::thread> &threads) {
  threads.emplace_back(Compute<kDepth - 1>, std::ref(previous), std::ref(last));
}

#endif

void Mux(hlslib::Stream<DataPack> &pipeOut, hlslib::Stream<DataPack> &pipeOut0,
         hlslib::Stream<DataPack> &pipeOut1) {
DemuxTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  DemuxBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    DemuxRows:
      for (int r = 0; r < kRows; ++r) {
      DemuxCols:
        for (int c = 0; c < kColsPerBlock; ++c) {
          #pragma HLS PIPELINE
          #pragma HLS LOOP_FLATTEN
          const auto read = hlslib::ReadBlocking(pipeOut);
          if (r % 2 == 0) {
            hlslib::WriteBlocking(pipeOut0, read, kPipeDepth);
          } else {
            hlslib::WriteBlocking(pipeOut1, read, kPipeDepth);
          }
        }
      }
    }
  }

}

void Write(hlslib::Stream<DataPack> &pipeOut, DataPack *memory) {
WriteTime:
  for (int t = 0; t < kTimeFolded; ++t) {
  WriteBlocks:
    for (int b = 0; b < kBlocks; ++b) {
    WriteRows:
      for (int r = 0; r < kRows / 2; ++r) {
      WriteCols:
        for (int c = 0; c < kColsPerBlock; ++c) {
          #pragma HLS PIPELINE
          #pragma HLS LOOP_FLATTEN
          const int index = (((t + 1) % 2) * (kWriteSize / 2) + r * kCols +
                             b * kColsPerBlock + c) %
                            kWriteSize;
          assert(index >= 0);
          assert(index < kWriteSize);
          memory[index] = hlslib::ReadBlocking(pipeOut);
        }
      }
    }
  }
}

void Kernel(DataPack const *memIn0, DataPack const *memIn1, DataPack *memOut0,
            DataPack *memOut1) {

  #pragma HLS INTERFACE m_axi port=memIn0  offset=slave bundle=gmem0
  #pragma HLS INTERFACE m_axi port=memIn1  offset=slave bundle=gmem1
  #pragma HLS INTERFACE m_axi port=memOut0 offset=slave bundle=gmem0
  #pragma HLS INTERFACE m_axi port=memOut1 offset=slave bundle=gmem1
  #pragma HLS INTERFACE s_axilite port=memIn0  bundle=control
  #pragma HLS INTERFACE s_axilite port=memIn1  bundle=control
  #pragma HLS INTERFACE s_axilite port=memOut0 bundle=control
  #pragma HLS INTERFACE s_axilite port=memOut1 bundle=control
  #pragma HLS INTERFACE s_axilite port=return  bundle=control

  #pragma HLS DATAFLOW

  // The typedef seems to break the high level synthesis tool when applying
  // pragmas
#ifndef STENCIL_SYNTHESIS
  hlslib::Stream<DataPack> pipeIn("pipeIn");
  hlslib::Stream<DataPack> pipeIn0("pipeIn0");
  hlslib::Stream<DataPack> pipeIn1("pipeIn1");
  hlslib::Stream<DataPack> pipeCompute("pipeCompute");
  hlslib::Stream<DataPack> pipeOut("pipeOut");
  hlslib::Stream<DataPack> pipeOut0("pipeOut0");
  hlslib::Stream<DataPack> pipeOut1("pipeOut1");
#else
  hls::stream<DataPack> pipeIn("pipeIn");
  #pragma HLS STREAM variable=pipeIn depth=kPipeDepth
  hls::stream<DataPack> pipeIn0("pipeIn0");
  #pragma HLS STREAM variable=pipeIn0 depth=kPipeDepth
  hls::stream<DataPack> pipeIn1("pipeIn1");
  #pragma HLS STREAM variable=pipeIn1 depth=kPipeDepth
  hls::stream<DataPack> pipeCompute("pipeCompute");
  #pragma HLS STREAM variable=pipeCompute depth=kPipeDepth
  hls::stream<DataPack> pipeOut("pipeOut");
  #pragma HLS STREAM variable=pipeOut depth=kPipeDepth
  hls::stream<DataPack> pipeOut0("pipeOut0");
  #pragma HLS STREAM variable=pipeOut0 depth=kPipeDepth
  hls::stream<DataPack> pipeOut1("pipeOut1");
  #pragma HLS STREAM variable=pipeOut1 depth=kPipeDepth
#endif

#ifdef STENCIL_SYNTHESIS
  Read(memIn0, pipeIn0);
  Read(memIn1, pipeIn1);
  Demux(pipeIn0, pipeIn1, pipeIn);
  UnrollCompute<kDepth>(pipeIn, pipeOut);
  Mux(pipeOut, pipeOut0, pipeOut1);
  Write(pipeOut0, memOut0);
  Write(pipeOut1, memOut1);
#else
  std::vector<std::thread> threads;
  threads.emplace_back(Read, memIn0, std::ref(pipeIn0));
  threads.emplace_back(Read, memIn1, std::ref(pipeIn1));
  threads.emplace_back(Demux, std::ref(pipeIn0), std::ref(pipeIn1),
                       std::ref(pipeIn));
  UnrollCompute<kDepth>(pipeIn, pipeOut, threads);
  threads.emplace_back(Mux, std::ref(pipeOut), std::ref(pipeOut0),
                       std::ref(pipeOut1));
  threads.emplace_back(Write, std::ref(pipeOut0), memOut0);
  threads.emplace_back(Write, std::ref(pipeOut1), memOut1);
  for (auto &t : threads) {
    t.join();
  }
#endif
}
