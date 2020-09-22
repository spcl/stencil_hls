/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#include "Memory.h"
#include "hlslib/xilinx/Simulation.h"
#include "hlslib/xilinx/Utility.h"  // hlslib::CeilDivide

void Compute(hlslib::Stream<Kernel_t> &pipeIn,
             hlslib::Stream<Kernel_t> &pipeOut, const int stage,
             const int timesteps_folded) {
  const int input_width =
      kBlockWidthKernel + 2 * hlslib::CeilDivide(kDepth - stage, kKernelWidth);

  // Size of the halo in either side
  const int boundary_width = (input_width - kBlockWidthKernel) / 2;

  // Begin and end indices of the inner block size (without any halos)
  const int inner_begin = boundary_width;
  const int inner_end = input_width - boundary_width;

  // Only shrink the output if we hit the boundary of the data width
  const bool shrink_output =
      (kKernelWidth + kDepth - stage - 1) % kKernelWidth == 0;

  // The stencil radius is always smaller than the halo size, so we always
  // shrink by one
  const int output_begin = shrink_output ? 1 : 0;
  const int output_end = shrink_output ? input_width - 1 : input_width;

  constexpr auto kLineWidth =
      kBlockWidthKernel + 2 * hlslib::CeilDivide(kDepth, kKernelWidth);

  Kernel_t northBuffer[kLineWidth];

  Kernel_t centerBuffer[kLineWidth];

  int t = 0;
  int b = 0;
  int r = 0;
  int c = 0;

  Data_t shiftWest(kBoundary);
  Kernel_t shiftCenter(kBoundary);

ComputeFlat:
  for (long i = 0;
       i < timesteps_folded * kBlocks * kRows * input_width + input_width - 1;
       ++i) {
#pragma HLS PIPELINE II = 1

#ifdef STENCIL_KERNEL_DEBUG
    std::stringstream debugStream;
    debugStream << "Stage " << stage << ": (" << t << ", " << b << ", " << r
                << ", " << c - boundary_width << "): ";
    bool debugCond = true;
#endif

    const bool isSaturating = i < input_width - 1;  // Shift right by one
    const bool isDraining =
        i >= timesteps_folded * kBlocks * kRows * input_width;
    const bool inBounds =
        ((b > 0 || c >= inner_begin) && (b < kBlocks - 1 || c < inner_end));
    const bool inBlock = c >= inner_begin && c < inner_end;

    if (isSaturating) {
      Kernel_t read;
      if (i >= inner_begin - 1) {  // Shift right by one
        read = pipeIn.Pop();
      } else {
        read = Kernel_t(kBoundary);
      }
      centerBuffer[i + inner_begin] = read;
#ifdef STENCIL_KERNEL_DEBUG
      debugStream << "saturating " << read << "\n";
#endif
    } else {  // Use else instead of continue or the whole pipeline breaks...

      Kernel_t read;
      if (!isDraining) {
        // If not on the last row, check if the column in this block is out
        // of bounds. If on the last row, check if the column is out of
        // bounds in the NEXT block. Written below in DNF form:
        if ((b == 0 && c < inner_begin && r < kRows - 1) ||
            (b == kBlocks - 1 && c < inner_begin && r == kRows - 1) ||
            (b == kBlocks - 1 && c >= inner_end && r < kRows - 1) ||
            (b == kBlocks - 2 && c >= inner_end && r == kRows - 1)) {
          read = Kernel_t(kBoundary);
        } else {
          read = pipeIn.Pop();
        }
      }

      // Collect vertical values
      const auto north = (r > 0) ? northBuffer[c] : Kernel_t(kBoundary);
      const auto south = (r < kRows - 1) ? read : Kernel_t(kBoundary);

      // Use center value shifted forward to populate bulk of west and east
      // vectors
      Kernel_t west, east;
      shiftCenter.ShiftTo<0, 1, kKernelWidth - 1>(west);
      shiftCenter.ShiftTo<1, 0, kKernelWidth - 1>(east);

      // Read the next center value to collect the last element of east
      const auto nextCenter = centerBuffer[c < input_width - 1 ? (c + 1) : 0];
      east[kKernelWidth - 1] = nextCenter[0];

      // We shifted the last element of west forward from last iteration
      west[0] = shiftWest;

      // Now update the line buffers
      centerBuffer[c] = read;
#pragma HLS DEPENDENCE variable = centerBuffer false
      northBuffer[c] = shiftCenter;
#pragma HLS DEPENDENCE variable = northBuffer false

#ifdef STENCIL_KERNEL_DEBUG
      debugStream << "N" << north << ", W" << west << ", C" << shiftCenter
                  << ", E" << east << ", S" << south;
#endif

      // Values have been consumed, so shift all center registers left
      shiftWest = shiftCenter[kKernelWidth - 1];
      shiftCenter = nextCenter;

      // Now we can perform the actual compute
      Kernel_t result;
    ComputeSIMD:
      for (int w = 0; w < kKernelWidth; ++w) {
#pragma HLS UNROLL
        const Data_t northVal = north[w];
        const Data_t westVal = west[w];
        const Data_t eastVal = east[w];
        const Data_t southVal = south[w];
        const Data_t factor =
            0.25;  // Cannot be constexpr due to half precision
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
      if (c >= output_begin && c < output_end && inBounds) {
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

      // Index calculations
      if (c == input_width - 1) {
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

#ifdef STENCIL_KERNEL_DEBUG
    if (debugCond) {
      std::cout << debugStream.str();
    }
#endif
  }
}

void StencilKernel(Memory_t const *in0, Memory_t *out0, Memory_t const *in1,
                   Memory_t *out1, const int timesteps_folded) {
#pragma HLS INTERFACE m_axi port = in0 offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = out0 offset = slave bundle = gmem0
#pragma HLS INTERFACE m_axi port = in1 offset = slave bundle = gmem1
#pragma HLS INTERFACE m_axi port = out1 offset = slave bundle = gmem1
#pragma HLS INTERFACE s_axilite port = in0 bundle = control
#pragma HLS INTERFACE s_axilite port = out0 bundle = control
#pragma HLS INTERFACE s_axilite port = in1 bundle = control
#pragma HLS INTERFACE s_axilite port = out1 bundle = control
#pragma HLS INTERFACE s_axilite port = timesteps_folded bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
#pragma HLS DATAFLOW

  hlslib::Stream<Kernel_t, kPipeDepth> pipes[kDepth + 1];

  hlslib::Stream<Memory_t, kMemoryBufferDepth> readBuffer0("readBuffer0");
  hlslib::Stream<Memory_t, kMemoryBufferDepth> readBuffer1("readBuffer1");
  hlslib::Stream<Memory_t, kPipeDepth> demuxPipe("demuxPipe");
  HLSLIB_DATAFLOW_INIT();
  HLSLIB_DATAFLOW_FUNCTION(ReadSplit, in0, readBuffer0, timesteps_folded);
  HLSLIB_DATAFLOW_FUNCTION(ReadSplit, in1, readBuffer1, timesteps_folded);
  HLSLIB_DATAFLOW_FUNCTION(DemuxRead, readBuffer0, readBuffer1, demuxPipe,
                           timesteps_folded);
  HLSLIB_DATAFLOW_FUNCTION(Widen, demuxPipe, pipes[0], timesteps_folded);

  for (int d = 0; d < kDepth; ++d) {
#pragma HLS UNROLL
    HLSLIB_DATAFLOW_FUNCTION(Compute, pipes[d], pipes[d + 1], d,
                             timesteps_folded);
  }

  hlslib::Stream<Memory_t, kMemoryBufferDepth> writeBuffer0("writeBuffer0");
  hlslib::Stream<Memory_t, kMemoryBufferDepth> writeBuffer1("writeBuffer1");
  hlslib::Stream<Memory_t, kPipeDepth> muxPipe("muxPipe");
  HLSLIB_DATAFLOW_FUNCTION(Narrow, pipes[kDepth], muxPipe, timesteps_folded);
  HLSLIB_DATAFLOW_FUNCTION(MuxWrite, muxPipe, writeBuffer0, writeBuffer1,
                           timesteps_folded);
  HLSLIB_DATAFLOW_FUNCTION(WriteSplit, writeBuffer0, out0, timesteps_folded);
  HLSLIB_DATAFLOW_FUNCTION(WriteSplit, writeBuffer1, out1, timesteps_folded);
  HLSLIB_DATAFLOW_FINALIZE();
}

