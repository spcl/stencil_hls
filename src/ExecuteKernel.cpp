#include "hlslib/OpenCL.h"
#include "Stencil.h"
#include <string>
#include <iomanip>
#include <chrono>
#include <cmath>

int main(int argc, char **argv) {

  if (argc > 2) {
    std::cerr << "Usage: ./ExecuteKernel [<verify [on/off]>]" << std::endl;
    return 1;
  }

  bool verify = true;
  if (argc == 2) {
    if (std::string(argv[1]) == "on") {
      verify = true;
    } else if (std::string(argv[1]) == "off") {
      verify = false;
    } else {
      std::cerr << "Verify option must be either \"on\" or \"off\"."
                << std::endl;
      return 1;
    }
  }

  std::vector<Memory_t> host;

  try {

    hlslib::ocl::Context context("Xilinx");

    auto device = context.MakeBuffer<Memory_t, hlslib::ocl::Access::readWrite>(
        hlslib::ocl::MemoryBank::bank0, 2 * kTotalElementsMemory);

    if (verify) {
      host = std::vector<Memory_t>(2 * kTotalElementsMemory,
                                   Memory_t(Kernel_t(static_cast<Data_t>(0))));
      for (int i = 0; i < kTotalElementsMemory; ++i) {
        host[i] = Memory_t(Kernel_t(static_cast<Data_t>(i)));
      }
      device.CopyToDevice(host.cbegin());
    }

    auto kernel = context.MakeKernelFromBinary(
        "memory_benchmark.xclbin", "MemoryBenchmark", device, device);

    const auto readSize =
        static_cast<float>(kTime) * kRows *
        ((2 * (kBlockWidthMemory + kHaloMemory)) +
         (kBlocks - 2) * (kBlockWidthMemory + 2 * kHaloMemory)) *
        sizeof(Memory_t);
    const auto writeSize = static_cast<float>(kTime) * kRows * kBlocks *
                           kBlockWidthMemory * sizeof(Memory_t);
    const auto transferred = readSize + writeSize;

    std::cout << "Executing kernel..." << std::flush;
    auto begin = std::chrono::high_resolution_clock::now();
    kernel.ExecuteTask();
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed =
        1e-9 *
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
            .count();
    std::cout << " Done.\nTransferred " << 1e-9 * transferred
              << " GB in " << elapsed << " seconds, bandwidth "
              << (1e-9 * transferred / elapsed) << " GB/s" << std::endl;
    if (verify) {
      device.CopyToHost(host.begin());
    }

  } catch (std::runtime_error const &err) {
    std::cerr << "Execution failed with error: \"" << err.what() << "\"."
              << std::endl;
    return 1;
  }

  // Verification
  if (verify) {
    for (int offset = 0; offset < 2 * kTotalElementsMemory;
         offset += kTotalElementsMemory) {
      for (int b = 0; b < kBlocks; ++b) {
        for (int r = 0; r < kRows; ++r) {
          for (int m = 0; m < kBlockWidthMemory; ++m) {
            const int index = offset + r * kBlockWidthMemory * kBlocks +
                              b * kBlockWidthMemory + m;
            const Data_t expected = r * kBlockWidthMemory * kBlocks +
                                    b * kBlockWidthMemory + m +
                                    ((offset == 0) ? kTime : (kTime - 1));
            for (int k = 0; k < kKernelPerMemory; ++k) {
              const Kernel_t elem = host[index][k];
              for (int w = 0; w < kMemoryWidth; ++w) {
                if (elem[w] != expected) {
                  std::cerr << "Mismatch at (" << r << ", "
                            << m * kMemoryWidth + k * kKernelWidth + w
                            << "): " << elem[w] << " (should be "
                            << expected << ")" << std::endl;
                  return 1;
                }
              }
            }
          }
        }
      }
    }
  }

  return 0;
}
