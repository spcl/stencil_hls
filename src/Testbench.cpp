#include "Stencil.h"
#include "Reference.h"
#include <cmath>
#include <iostream>
#include <vector>

int main() {
  const auto reference = Reference(std::vector<Data_t>(kRows * kCols, 0));
  std::vector<Memory_t> memory(2 * kTotalElementsMemory,
                               Kernel_t(Data_t(static_cast<Data_t>(0))));
  Jacobi(memory.data(), memory.data());
  // int correct = 0;
  // int mismatches = 0;
  const int offset = (kTimeFolded % 2 == 0) ? 0 : kTotalElementsMemory;
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kBlockWidthMemory * kBlocks; ++c) {
      const int index = r * kBlockWidthMemory * kBlocks + c;
      for (int k = 0; k < kKernelPerMemory; ++k) {
        const Kernel_t elem = memory[offset + index][k];
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
