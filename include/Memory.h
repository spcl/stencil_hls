/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#pragma once

#include "Stencil.h"
#include "hlslib/xilinx/Stream.h"

static constexpr int kDimms = 2;

void ReadSplit(Memory_t const *input, hlslib::Stream<Memory_t> &buffer,
               const int timesteps_folded);

void DemuxRead(hlslib::Stream<Memory_t> &buffer0,
               hlslib::Stream<Memory_t> &buffer1,
               hlslib::Stream<Memory_t> &pipe, const int timesteps_folded);

void Widen(hlslib::Stream<Memory_t> &in, hlslib::Stream<Kernel_t> &out,
           const int timesteps_folded);

void Narrow(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Memory_t> &out,
            const int timesteps_folded);

void MuxWrite(hlslib::Stream<Memory_t> &pipe, hlslib::Stream<Memory_t> &buffer0,
              hlslib::Stream<Memory_t> &buffer1, const int timesteps_folded);

void WriteSplit(hlslib::Stream<Memory_t> &buffer, Memory_t *output,
                const int timesteps_folded);
