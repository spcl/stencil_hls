#include "Stencil.h"
#include "Reference.h"
#include <algorithm> // std::copy
#include <cmath>     // std::fabs
#include <iostream>
#include <vector>

bool Verify(std::vector<Data_t> const &reference,
            std::vector<Memory_t> const &test) {
  const int offset = (kTimeFolded % 2 == 0) ? 0 : kTotalElementsMemory;
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
          const auto diff = std::fabs(expected - actual); 
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

int main() {

  std::cout << "Running reference implementation..." << std::flush;
  const auto reference = Reference(std::vector<Data_t>(kRows * kCols, 0));
  std::cout << " Done." << std::endl;

  std::cout << "Initializing memory..." << std::flush;
  std::vector<Memory_t> memory(2 * kTotalElementsMemory,
                               Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::vector<Memory_t> memorySplit(2 * kTotalElementsMemory,
                                    Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::vector<Memory_t> memorySplit0(kTotalElementsMemory,
                                     Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::vector<Memory_t> memorySplit1(kTotalElementsMemory,
                                     Kernel_t(Data_t(static_cast<Data_t>(0))));
  std::cout << " Done." << std::endl;

  std::cout << "Running single memory implementation..." << std::flush;
  Jacobi(memory.data(), memory.data());
  std::cout << " Done." << std::endl;

  std::cout << "Running dual memory implementation..." << std::flush;
  JacobiTwoDimms(memorySplit0.data(), memorySplit0.data(), memorySplit1.data(),
                 memorySplit1.data());
  std::cout << " Done." << std::endl;

  std::cout << "Reassembling memory..." << std::flush;
  for (int rIn = 0, rOut = 0; rOut < kRows; ++rIn, rOut += 2) {
    static constexpr auto kMemoryCols = kCols / kMemoryWidth;
    const auto iStart = rIn * kMemoryCols;
    std::copy(memorySplit0.begin() + iStart,
              memorySplit0.begin() + iStart + kMemoryCols,
              memorySplit.begin() + rOut * kMemoryCols);
    std::copy(memorySplit1.begin() + iStart,
              memorySplit1.begin() + iStart + kMemoryCols,
              memorySplit.begin() + (rOut + 1) * kMemoryCols);
  }
  std::cout << " Done." << std::endl;

  std::cout << "Verifying single memory..." << std::flush;
  if (!Verify(reference, memory)) {
    return 1;
  }
  std::cout << " Done." << std::endl;

  std::cout << "Verifying dual memory..." << std::flush;
  if (!Verify(reference, memorySplit)) {
    return 1; 
  }
  std::cout << " Done." << std::endl;
  
  return 0;
}
