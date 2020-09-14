/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License. 

#pragma once

#include "Stencil.h"
#include "hlslib/xilinx/Stream.h"
#include "hlslib/xilinx/Utility.h"
#ifndef STENCIL_SYNTHESIS
#include <thread>
#endif

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
  hlslib::Stream<Kernel_t, kInputWidth> northBuffer("northBuffer");
  hlslib::Stream<Kernel_t, kInputWidth> centerBuffer("centerBuffer");

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
        read = pipeIn.Pop();
      } else {
        read = Kernel_t(kBoundary);
      }
      centerBuffer.WriteOptimistic(read, kInputWidth);
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

          read = pipeIn.Pop();

        }
      }

      // Collect vertical values
      const auto north =
          (r > 0) ? northBuffer.ReadOptimistic() : Kernel_t(kBoundary);
      const auto south = (r < kRows - 1) ? read : Kernel_t(kBoundary);

      // Use center value shifted forward to populate bulk of west and east
      // vectors
      Kernel_t west, east;
      shiftCenter.ShiftTo<0, 1, kKernelWidth - 1>(west);
      shiftCenter.ShiftTo<1, 0, kKernelWidth - 1>(east);

      // Read the next center value to collect the last element of east
      const auto nextCenter = centerBuffer.ReadOptimistic();
      east[kKernelWidth - 1] = nextCenter[0];

      // We shifted the last element of west forward from last iteration
      west[0] = shiftWest;

      // Now update the line buffers
      if (!isDraining) {
        centerBuffer.WriteOptimistic(read, kInputWidth);
        if (r < kRows - 1) {
          northBuffer.WriteOptimistic(shiftCenter);
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
        const Data_t factor = 0.25; // Cannot be constexpr due to half precision
        const Data_t add0 = northVal + westVal;
        const Data_t add1 = add0 + eastVal;
        const Data_t add2 = add1 + southVal;
        STENCIL_RESOURCE_PRAGMA_ADD(add0);
        STENCIL_RESOURCE_PRAGMA_ADD(add1);
        STENCIL_RESOURCE_PRAGMA_ADD(add2);
        const Data_t mult = factor * add2;
        STENCIL_RESOURCE_PRAGMA_MULT(mult);
        result[w] = mult;
      }

      // Only output values if the next unit needs them
      if (c >= kOutputBegin && c < kOutputEnd && inBounds) {
        Kernel_t write;
        if (inBounds) {
          write = result;
        } else {
          write = Kernel_t(kBoundary);
        }
        pipeOut.Push(write);
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
  hlslib::Stream<Kernel_t, kPipeDepth> next("pipe");
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
