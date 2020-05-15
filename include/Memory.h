/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License. 

#pragma once

#include "Stencil.h"
#include "hlslib/xilinx/Stream.h"

#ifdef STENCIL_SYNTHESIS

// Single DIMM
void Read(Memory_t const *memory, hlslib::Stream<Kernel_t> &toKernel);

// Dual DIMM
void Read(Memory_t const *memory0, Memory_t const *memory1,
          hlslib::Stream<Kernel_t> &toKernel);

// Single DIMM
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory);

// Dual DIMM
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory0,
           Memory_t *memory1);

#else

#include <thread>

// Single DIMM
void Read(Memory_t const *memory, hlslib::Stream<Kernel_t> &toKernel,
          std::vector<std::thread> &threads);

// Dual DIMM
void Read(Memory_t const *memory0, Memory_t const *memory1,
          hlslib::Stream<Kernel_t> &toKernel,
          std::vector<std::thread> &threads);

// Single DIMM
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory,
           std::vector<std::thread> &threads);

// Dual DIMM
void Write(hlslib::Stream<Kernel_t> &fromKernel, Memory_t *memory0,
           Memory_t *memory1, std::vector<std::thread> &threads);

#endif
