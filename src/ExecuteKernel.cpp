#include "hlslib/SDAccel.h"
#include "Stencil.h"
#include "Reference.h"
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

    hlslib::ocl::Context context("Xilinx", kDeviceDsaString);

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

    auto kernel =
        context.MakeKernelFromBinary(kKernelString, "Jacobi", device, device);

    const auto readSize =
        static_cast<float>(kTimeFolded) * kTotalInputMemory * sizeof(Memory_t);
    const auto writeSize = static_cast<float>(kTimeFolded) * sizeof(Memory_t);
    const auto transferred = readSize + writeSize;

    std::cout << "Executing kernel..." << std::flush;
    auto begin = std::chrono::high_resolution_clock::now();
    kernel.ExecuteTask();
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed =
        1e-9 *
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
            .count();
    std::cout << " Done.\nMoved " << 1e-9 * transferred << " GB in " << elapsed
              << " seconds, bandwidth " << (1e-9 * transferred / elapsed)
              << " GB/s\nEvaluated " << kTimeTotal * kRows * kCols
              << " cells in " << elapsed << " seconds, performance "
              << 4e-9 * (kTimeTotal * kRows * kCols) / elapsed << " GOp/s"
              << std::endl;
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
    // int correct = 0;
    // int mismatches = 0;
    const auto reference = Reference(std::vector<Data_t>(kRows * kCols, 0));
    const int offset = (kTimeFolded % 2 == 0) ? 0 : kTotalElementsMemory;
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kBlockWidthMemory * kBlocks; ++c) {
        const int index = r * kBlockWidthMemory * kBlocks + c;
        for (int k = 0; k < kKernelPerMemory; ++k) {
          const Kernel_t elem = host[offset + index][k];
          for (int w = 0; w < kKernelWidth; ++w) {
            const auto expected =
                reference[kMemoryWidth * index + kKernelWidth * k + w];
            const auto actual = elem[w];
            const auto diff = std::fabs(expected - actual);
            if (diff > 1e-4) {
              std::cerr << "Mismatch at (" << r << ", "
                        << c * kMemoryWidth + k * kKernelWidth + w
                        << "): " << actual << " (should be " << expected << ")"
                        << std::endl;
              // ++mismatches;
              return 1;
            } else {
              // std::cout << "Correct at (" << r << ", "
              //           << c * kMemoryWidth + k * kKernelWidth + w
              //           << "): " << elem[w] << "\n";
              // ++correct;
            }
          }
        }
      }
    }
    // std::cout << "Correct: " << correct << "\nMismatches: " << mismatches
    //           << std::endl;
    return 0;
  }

  return 0;
}
