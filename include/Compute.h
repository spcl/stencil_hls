/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License. 

#pragma once

#include "Stencil.h"
#include "hlslib/xilinx/Stream.h"
#include "hlslib/xilinx/Utility.h"
#ifndef STENCIL_SYNTHESIS
#include <thread>
#endif


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
