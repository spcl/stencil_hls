/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#include <algorithm>  // std::copy
#include <cmath>      // std::fabs
#include <iostream>
#include <vector>
#include "Reference.h"
#include "Stencil.h"

bool Verify(std::vector<Data_t> const &reference,
            std::vector<Memory_t> const &test, const int timesteps_folded) {
  const int offset = (timesteps_folded % 2 == 0) ? 0 : kTotalElementsMemory;
  // int correct = 0;
  // int mismatches = 0;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kBlockWidthMemory * kBlocks; ++c) {
      const int index = r * kBlockWidthMemory * kBlocks + c;
      for (int k = 0; k < kKernelPerMemory; ++k) {
        const Kernel_t elem = test[offset + index][k];
        for (int w = 0; w < kKernelWidth; ++w) {
          const auto expected =
              reference[kMemoryWidth * index + kKernelWidth * k + w];
          const auto actual = elem[w];
          auto diff = expected - actual;
          diff = (diff < 0) ? Data_t(-diff) : Data_t(diff);
          if (diff > 1e-4) {
            std::cerr << "Mismatch at (" << r << ", "
                      << c * kMemoryWidth + k * kKernelWidth + w
                      << "): " << actual << " (should be " << expected << ")"
                      << std::endl;
            // ++mismatches;
            return false;
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
  return true;
}

int main(int argc, char **argv) {
  int timesteps = 2 * kDepth;
  if (argc > 1) {
    timesteps = std::stoi(argv[1]);
    if (timesteps % kDepth != 0) {
      std::cerr << "Number of timesteps (" << timesteps
                << ") must be divisible by depth (" << kDepth << ")\n";
      return 2;
    }
  }
  const auto timesteps_folded = timesteps / kDepth;

  std::cout << "Running reference implementation..." << std::flush;
  const auto reference =
      Reference(std::vector<Data_t>(kRows * kCols, 0), timesteps);
  std::cout << " Done." << std::endl;

  std::cout << "Initializing memory..." << std::flush;
  std::vector<Memory_t> memorySplit(2 * kTotalElementsMemory,
                                    Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::vector<Memory_t> memorySplit0(kTotalElementsMemory,
                                     Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::vector<Memory_t> memorySplit1(kTotalElementsMemory,
                                     Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::cout << " Done." << std::endl;

  std::cout << "Running simulation..." << std::flush;
  StencilKernel(memorySplit0.data(), memorySplit0.data(), memorySplit1.data(),
                memorySplit1.data(), timesteps_folded);
  std::cout << " Done." << std::endl;

  std::cout << "Reassembling memory..." << std::flush;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols / kMemoryWidth; ++c) {
      constexpr auto kMemoryCols = kCols / kMemoryWidth;
      memorySplit[2 * r * kMemoryCols + c] = memorySplit0[r * kMemoryCols + c];
      memorySplit[(2 * r + 1) * kMemoryCols + c] =
          memorySplit1[r * kMemoryCols + c];
    }
  }
  std::cout << " Done." << std::endl;

  std::cout << "Verifying result..." << std::flush;
  if (!Verify(reference, memorySplit, timesteps_folded)) {
    return 1;
  }
  std::cout << " Done." << std::endl;

  return 0;
}
