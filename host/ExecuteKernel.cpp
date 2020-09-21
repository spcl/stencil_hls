/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include "Reference.h"
#include "Stencil.h"
#include "hlslib/xilinx/SDAccel.h"
#include "hlslib/xilinx/Utility.h"

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "Usage: ./ExecuteKernel <[hw/hw_emu]> <timesteps> [<verify "
                 "[on/off]>]"
              << std::endl;
    return 1;
  }

  bool is_emulation;
  std::string path;
  const std::string mode_arg(argv[1]);
  if (mode_arg == "hw") {
    is_emulation = false;
    path = "Stencil_hw.xclbin";
  } else if (mode_arg == "hw_emu") {
    hlslib::SetEnvironmentVariable("XCL_EMULATION_MODE", "hw_emu");
    path = "Stencil_hw_emu.xclbin";
    is_emulation = true;
  } else {
    std::cerr << "Invalid mode \"" << mode_arg << "\".\n";
    return 2;
  }

  const auto timesteps = std::stoi(argv[2]);
  if (timesteps % kDepth != 0) {
    std::cerr << "Number of timesteps (" << timesteps
              << ") must be divisible by depth (" << kDepth << ")\n";
    return 3;
  }
  const auto timesteps_folded = timesteps / kDepth;

  bool verify = false;
  if (argc == 4) {
    if (std::string(argv[3]) == "on") {
      verify = true;
    } else if (std::string(argv[3]) == "off") {
      verify = false;
    } else {
      std::cerr << "Verify option must be either \"on\" or \"off\"."
                << std::endl;
      return 4;
    }
  }

  std::vector<Memory_t> hostSplit0;
  std::vector<Memory_t> hostSplit1;

  try {
    std::cout << "Initializing OpenCL context..." << std::flush;
    hlslib::ocl::Context context;
    std::cout << " Done.\n";

    std::cout << "Creating program..." << std::flush;
    auto program = context.MakeProgram(path);
    std::cout << " Done.\n";

    std::cout << "Allocating device memory..." << std::flush;
    auto device0 = context.MakeBuffer<Memory_t, hlslib::ocl::Access::readWrite>(
        hlslib::ocl::MemoryBank::bank0, kTotalElementsMemory);
    auto device1 = context.MakeBuffer<Memory_t, hlslib::ocl::Access::readWrite>(
        hlslib::ocl::MemoryBank::bank1, kTotalElementsMemory);
    std::cout << " Done." << std::endl;

    if (verify) {
      std::cout << "Initializing memory..." << std::flush;
      hostSplit0 = std::vector<Memory_t>(
          kTotalElementsMemory, Memory_t(Kernel_t(static_cast<Data_t>(0))));
      hostSplit1 = std::vector<Memory_t>(
          kTotalElementsMemory, Memory_t(Kernel_t(static_cast<Data_t>(0))));
      device0.CopyFromHost(hostSplit0.cbegin());
      device1.CopyFromHost(hostSplit1.cbegin());
      std::cout << " Done." << std::endl;
    }

    std::cout << "Creating kernel..." << std::flush;
    auto kernel = program.MakeKernel(StencilKernel, "StencilKernel", device0,
                                     device0, device1, device1, timesteps);
    std::cout << " Done." << std::endl;

    const auto readSize = static_cast<float>(timesteps_folded) *
                          kTotalInputMemory * sizeof(Memory_t);
    const auto writeSize = static_cast<float>(timesteps_folded) *
                           kTotalElementsMemory * sizeof(Memory_t);
    const auto transferred = readSize + writeSize;

    std::cout << "Executing kernel..." << std::flush;
    auto begin = std::chrono::high_resolution_clock::now();
    kernel.ExecuteTask();
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed =
        1e-9 * std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin)
                   .count();
    std::cout << " Done.\nMoved " << 1e-9 * transferred << " GB in " << elapsed
              << " seconds, bandwidth " << (1e-9 * transferred / elapsed)
              << " GB/s\nEvaluated " << timesteps * kRows * kCols
              << " cells in " << elapsed << " seconds, performance "
              << 4e-9 * (timesteps * kRows * kCols) / elapsed << " GOp/s"
              << std::endl;
    if (verify) {
      std::cout << "Copying back memory..." << std::flush;
      device0.CopyToHost(hostSplit0.begin());
      device1.CopyToHost(hostSplit1.begin());
      std::cout << " Done." << std::endl;
    }

  } catch (std::runtime_error const &err) {
    std::cerr << "Execution failed with error: \"" << err.what() << "\"."
              << std::endl;
    return 1;
  }

  // Verification
  if (verify) {
    std::cout << "Reassembling memory..." << std::flush;
    std::vector<Memory_t> host(kTotalElementsMemory);
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kCols / kMemoryWidth; ++c) {
        constexpr auto kMemoryCols = kCols / kMemoryWidth;
        host[2 * r * kMemoryCols + c] = hostSplit0[r * kMemoryCols + c];
        host[(2 * r + 1) * kMemoryCols + c] = hostSplit1[r * kMemoryCols + c];
      }
    }
    std::cout << " Done." << std::endl;
    int correct = 0;
    int mismatches = 0;
    std::cout << "Running reference implementation..." << std::flush;
    const auto reference =
        Reference(std::vector<Data_t>(kRows * kCols, 0), timesteps);
    std::cout << " Done." << std::endl;
    std::cout << "Verifying result..." << std::flush;
    const int offset = (timesteps_folded % 2 == 0) ? 0 : kTotalElementsMemory;
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kBlockWidthMemory * kBlocks; ++c) {
        const int index = r * kBlockWidthMemory * kBlocks + c;
        for (int k = 0; k < kKernelPerMemory; ++k) {
          const Kernel_t elem = host[offset + index][k];
          for (int w = 0; w < kKernelWidth; ++w) {
            const auto expected =
                reference[kMemoryWidth * index + kKernelWidth * k + w];
            const auto actual = elem[w];
            auto diff = expected - actual;
            diff = (diff < 0) ? Data_t(-diff) : Data_t(diff);
            if (diff > 1e-4) {
              if (mismatches == 0) {
                std::cerr << "Mismatch at (" << r << ", "
                          << c * kMemoryWidth + k * kKernelWidth + w
                          << "): " << actual << " (should be " << expected
                          << ")" << std::endl;
              }
              ++mismatches;
            } else {
              // std::cout << "Correct at (" << r << ", "
              //           << c * kMemoryWidth + k * kKernelWidth + w
              //           << "): " << elem[w] << "\n";
              ++correct;
            }
          }
        }
      }
    }
    std::cout << " Done." << std::endl;
    std::cout << "Correct: " << correct << "\nMismatches: " << mismatches
              << std::endl;
    if (mismatches == 0) {
      std::cout << "Verification successful." << std::endl;
    } else {
      std::cerr << "Verification failed." << std::endl;
      return 1;
    }
  }

  return 0;
}
