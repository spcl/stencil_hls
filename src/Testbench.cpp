#include "Stencil.h"
#include <iostream>
#include <vector>

int main() {
  std::vector<Memory_t> memory(2 * kTotalElementsMemory);
  for (int i = 0; i < kTotalElementsMemory; ++i) {
    memory[i] = Memory_t(Kernel_t(static_cast<Data_t>(i)));
  }
  Jacobi(memory.data(), memory.data());
  // int correct = 0;
  // int mismatches = 0;
  for (int offset = 0; offset < 2 * kTotalElementsMemory;
       offset += kTotalElementsMemory) {
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kBlockWidthMemory * kBlocks; ++c) {
        const int index = offset + r * kBlockWidthMemory * kBlocks + c;
        const Data_t expected =
            r * kBlockWidthMemory * kBlocks + c +
            ((offset == 0) ? kTimeFolded : (kTimeFolded - 1));
        for (int k = 0; k < kKernelPerMemory; ++k) {
          const Kernel_t elem = memory[index][k];
          for (int w = 0; w < kKernelWidth; ++w) {
            if (elem[w] != expected) {
              std::cerr << "Mismatch at (" << r << ", "
                        << c * kMemoryWidth + k * kKernelWidth + w
                        << "): " << elem[w] << " (should be " << expected
                        << ")" << std::endl;
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
  }
  // std::cout << "Correct: " << correct << "\nMismatches: " << mismatches
  //           << std::endl;
  return 0;
}
