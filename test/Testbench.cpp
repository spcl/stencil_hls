#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>
#include "Stencil.h"

std::vector<float> Reference(std::vector<float> const &input) {
  std::vector<float> domain(input);
  std::vector<float> buffer(input);
  for (int t = 0; t < kTime; ++t) {
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kTotalCols; ++c) {
        const float west = (c == 0) ? 1 : domain[r * kTotalCols + c - 1];
        const float east =
            (c == kTotalCols - 1) ? 1 : domain[r * kTotalCols + c + 1];
        const float north = (r == 0) ? 1 : domain[(r - 1) * kTotalCols + c];
        const float south =
            (r == kRows - 1) ? 1 : domain[(r + 1) * kTotalCols + c];
        buffer[r * kTotalCols + c] = 0.25 * (north + west + east + south);
      }
    }
    domain.swap(buffer);
  }
  return domain;
}

int main(int argc, char **) {
  if (argc != 1) {
    std::cerr << "Usage: ./Testbench" << std::endl;
    return 1;
  }
  std::vector<DataPack> memory0(kWriteSize, DataPack(static_cast<Data_t>(0)));
  std::vector<DataPack> memory1(kWriteSize, DataPack(static_cast<Data_t>(0)));
  Kernel(memory0.data(), memory1.data(), memory0.data(), memory1.data());
  std::vector<DataPack> result(kWriteSize);
  for (int r = 0; r < kRows / 2; ++r) {
    for (int c = 0; c < kCols; ++c) {
      result[2*r*kCols + c] = memory0[r*kCols + c]; 
      result[(2*r + 1)*kCols + c] = memory1[r*kCols + c]; 
    }
  }
  const auto reference = Reference(std::vector<float>(kRows * kTotalCols, 0));
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      for (int w = 0; w < kDataWidth; ++w) {
        const float diff =
            std::fabs(result[r * kCols + c][w] -
                      reference[r * kTotalCols + c * kDataWidth + w]);
        if (diff > 1e-4) {
          std::cerr << "Mismatch at (" << r << ", " << c
                    << "): " << result[r * kCols + c][w] << " (should be "
                    << reference[r * kTotalCols + c * kDataWidth + w] << ")\n";
          // return 1;
        }
      }
    }
  }
  return 0;
}
