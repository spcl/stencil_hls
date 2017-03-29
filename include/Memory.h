/// \author Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// \date   March 2017

#pragma once

#include "Stencil.h"
#include "hlslib/Stream.h"

void Read(Memory_t const *input, hlslib::Stream<Memory_t> &buffer);

void Widen(hlslib::Stream<Memory_t> &in, hlslib::Stream<Kernel_t> &out);

void Narrow(hlslib::Stream<Kernel_t> &in, hlslib::Stream<Memory_t> &out);

void Write(hlslib::Stream<Memory_t> &buffer, Memory_t *output);
