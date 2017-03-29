/// \author Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// \date March 2017

#include "Compute.h"
#include "Memory.h"

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
  UnrollCompute<kDepth>(kernelPipeIn, kernelPipeOut, threads);
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
  UnrollCompute<kDepth>(kernelPipeIn, kernelPipeOut);
  Narrow(kernelPipeOut, writeBuffer);
  Write(writeBuffer, out);
#endif
}

void JacobiTwoDimms(Memory_t const *in0, Memory_t *out0,
                    Memory_t const *in1, Memory_t *out1) {
  #pragma HLS INTERFACE m_axi port=in0  offset=slave bundle=gmem0
  #pragma HLS INTERFACE m_axi port=out0 offset=slave bundle=gmem0
  #pragma HLS INTERFACE m_axi port=in1  offset=slave bundle=gmem1
  #pragma HLS INTERFACE m_axi port=out1 offset=slave bundle=gmem1
  #pragma HLS INTERFACE s_axilite port=in0    bundle=control 
  #pragma HLS INTERFACE s_axilite port=out0   bundle=control 
  #pragma HLS INTERFACE s_axilite port=in1    bundle=control 
  #pragma HLS INTERFACE s_axilite port=out1   bundle=control 
  #pragma HLS INTERFACE s_axilite port=return bundle=control 
  #pragma HLS DATAFLOW
}

