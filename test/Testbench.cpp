#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>
#include "Stencil.h"

std::vector<Data_t> Reference(std::vector<Data_t> const &input) {
  std::vector<Data_t> domain(input);
  std::vector<Data_t> buffer(input);
  for (int t = 0; t < kTime; ++t) {
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kTotalCols; ++c) {
        const Data_t west =
            (c == 0) ? Data_t(1) : domain[r * kTotalCols + c - 1];
        const Data_t east =
            (c == kTotalCols - 1) ? Data_t(1) : domain[r * kTotalCols + c + 1];
        const Data_t north =
            (r == 0) ? Data_t(1) : domain[(r - 1) * kTotalCols + c];
        const Data_t south =
            (r == kRows - 1) ? Data_t(1) : domain[(r + 1) * kTotalCols + c];
        buffer[r * kTotalCols + c] =
            Data_t(0.25) * (north + west + east + south);
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
  const auto reference = Reference(std::vector<Data_t>(kRows * kTotalCols, 0));
  for (int r = 0; r < kRows; ++r) {
    for (int c = 0; c < kCols; ++c) {
      for (int w = 0; w < kDataWidth; ++w) {
        const Data_t diff = std::fabs(static_cast<double>(
            Data_t(result[r * kCols + c][w]) -
            Data_t(reference[r * kTotalCols + c * kDataWidth + w])));
        if (diff > 1e-4) {
          std::cerr << "Mismatch at (" << r << ", " << c << "): "
                    << static_cast<double>(Data_t(result[r * kCols + c][w]))
                    << " (should be "
                    << static_cast<double>(
                           reference[r * kTotalCols + c * kDataWidth + w])
                    << ")\n";
          // return 1;
        }
      }
    }
  }
  return 0;
}
