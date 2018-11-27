/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @date      March 2017 
/// @copyright This software is copyrighted under the BSD 3-Clause License. 

#include "Reference.h"

std::vector<Data_t> Reference(std::vector<Data_t> const &input) {
  std::vector<Data_t> domain(input);
  std::vector<Data_t> buffer(input);
  for (int t = 0; t < kTimeTotal; ++t) {
    for (int r = 0; r < kRows; ++r) {
      for (int c = 0; c < kCols; ++c) {
        const Data_t west =
            (c == 0) ? Data_t(1) : domain[r * kCols + c - 1];
        const Data_t east =
            (c == kCols - 1) ? Data_t(1) : domain[r * kCols + c + 1];
        const Data_t north =
            (r == 0) ? Data_t(1) : domain[(r - 1) * kCols + c];
        const Data_t south =
            (r == kRows - 1) ? Data_t(1) : domain[(r + 1) * kCols + c];
        buffer[r * kCols + c] =
            Data_t(0.25) * (north + west + east + south);
      }
    }
    domain.swap(buffer);
  }
  return domain;
}
