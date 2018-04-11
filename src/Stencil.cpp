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
  hlslib::Stream<Kernel_t> toKernel("toKernel");
  hlslib::Stream<Kernel_t> fromKernel("fromKernel");
  Read(in, toKernel, threads);
  UnrollCompute<kDepth>(toKernel, fromKernel, threads);
  Write(fromKernel, out, threads);
  for (auto &t : threads) {
    t.join();
  }
#else
  hlslib::Stream<Kernel_t> toKernel("toKernel", kPipeDepth);
  hlslib::Stream<Kernel_t> fromKernel("fromKernel", kPipeDepth);
  Read(in, toKernel);
  UnrollCompute<kDepth>(toKernel, fromKernel);
  Write(fromKernel, out);
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
#ifndef STENCIL_SYNTHESIS
  std::vector<std::thread> threads;
  hlslib::Stream<Kernel_t> toKernel("toKernel");
  hlslib::Stream<Kernel_t> fromKernel("fromKernel");
  Read(in0, in1, toKernel, threads);
  UnrollCompute<kDepth>(toKernel, fromKernel, threads);
  Write(fromKernel, out0, out1, threads);
  for (auto &t : threads) {
    t.join();
  }
#else
  hlslib::Stream<Kernel_t> toKernel("toKernel", kPipeDepth);
  hlslib::Stream<Kernel_t> fromKernel("fromKernel", kPipeDepth);
  Read(in0, in1, toKernel);
  UnrollCompute<kDepth>(toKernel, fromKernel);
  Write(fromKernel, out0, out1);
#endif
}

