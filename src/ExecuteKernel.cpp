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

  if (kDimms == 1) {

    std::vector<Memory_t> host;

    try {

      std::cout << "Initializing OpenCL context..." << std::flush;
      hlslib::ocl::Context context;
      std::cout << " Done.\n";

      std::cout << "Allocating device memory..." << std::flush;
      auto device = context.MakeBuffer<Memory_t, hlslib::ocl::Access::readWrite>(
          hlslib::ocl::MemoryBank::bank0, 2 * kTotalElementsMemory);
      std::cout << " Done." << std::endl;

      if (verify) {
        std::cout << "Initializing memory..." << std::flush;
        host = std::vector<Memory_t>(2 * kTotalElementsMemory,
                                     Memory_t(Kernel_t(static_cast<Data_t>(0))));
        device.CopyFromHost(host.cbegin());
        std::cout << " Done." << std::endl;
      }

      std::cout << "Creating kernel..." << std::flush;
      auto program = context.MakeProgram(kKernelString);
      auto kernel = program.MakeKernel(kKernelString, "Jacobi", device, device);
      std::cout << " Done." << std::endl;

      const auto readSize =
          static_cast<float>(kTimeFolded) * kTotalInputMemory * sizeof(Memory_t);
      const auto writeSize = static_cast<float>(kTimeFolded) *
                             kTotalElementsMemory * sizeof(Memory_t);
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
        std::cout << "Copying back memory..." << std::flush;
        device.CopyToHost(host.begin());
        std::cout << " Done." << std::endl;
      }

    } catch (std::runtime_error const &err) {
      std::cerr << "Execution failed with error: \"" << err.what() << "\"."
                << std::endl;
      return 1;
    }

    // Verification
    if (verify) {
      int correct = 0;
      int mismatches = 0;
      std::cout << "Running reference implementation..." << std::flush;
      const auto reference = Reference(std::vector<Data_t>(kRows * kCols, 0));
      std::cout << " Done." << std::endl;
      std::cout << "Verifying result..." << std::flush;
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
              auto diff = expected - actual;
              diff = (diff < 0) ? Data_t(-diff) : Data_t(diff);
              if (diff > 1e-4) {
                if (mismatches == 0) {
                  std::cerr << "Mismatch at (" << r << ", "
                            << c * kMemoryWidth + k * kKernelWidth + w
                            << "): " << actual << " (should be " << expected << ")"
                            << std::endl;
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

  } else if (kDimms == 2) {

    std::vector<Memory_t> hostSplit0;
    std::vector<Memory_t> hostSplit1;

    try {

      std::cout << "Initializing OpenCL context..." << std::flush;
      hlslib::ocl::Context context;
      std::cout << " Done.\n";

      std::cout << "Allocating device memory..." << std::flush;
      auto device0 =
          context.MakeBuffer<Memory_t, hlslib::ocl::Access::readWrite>(
              hlslib::ocl::MemoryBank::bank0, kTotalElementsMemory);
      auto device1 =
          context.MakeBuffer<Memory_t, hlslib::ocl::Access::readWrite>(
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
      auto program = context.MakeProgram(kKernelString);
      auto kernel = program.MakeKernel("JacobiTwoDimms", device0, device0,
                                       device1, device1);
      std::cout << " Done." << std::endl;

      const auto readSize =
          static_cast<float>(kTimeFolded) * kTotalInputMemory * sizeof(Memory_t);
      const auto writeSize = static_cast<float>(kTimeFolded) *
                             kTotalElementsMemory * sizeof(Memory_t);
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
      for (int rIn = 0, rOut = 0; rOut < kRows; ++rIn, rOut += 2) {
        static constexpr auto kMemoryCols = kCols / kMemoryWidth;
        const auto iStart = rIn * kMemoryCols;
        std::copy(hostSplit0.begin() + iStart,
                  hostSplit0.begin() + iStart + kMemoryCols,
                  host.begin() + rOut * kMemoryCols);
        std::copy(hostSplit1.begin() + iStart,
                  hostSplit1.begin() + iStart + kMemoryCols,
                  host.begin() + (rOut + 1) * kMemoryCols);
      }
      std::cout << " Done." << std::endl;
      int correct = 0;
      int mismatches = 0;
      std::cout << "Running reference implementation..." << std::flush;
      const auto reference = Reference(std::vector<Data_t>(kRows * kCols, 0));
      std::cout << " Done." << std::endl;
      std::cout << "Verifying result..." << std::flush;
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
              auto diff = expected - actual;
              diff = (diff < 0) ? Data_t(-diff) : Data_t(diff);
              if (diff > 1e-4) {
                if (mismatches == 0) {
                  std::cerr << "Mismatch at (" << r << ", "
                            << c * kMemoryWidth + k * kKernelWidth + w
                            << "): " << actual << " (should be " << expected << ")"
                            << std::endl;
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

  } else {
    throw std::runtime_error("Unrecognized number of DIMMs.");
  }

  return 0;
}
