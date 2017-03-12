#include <cmath>
#include "Stencil.h"
#include "hlslib/SDAccel.h"

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

int main(int argc, char **argv) {
  if (argc > 2) {
    std::cerr << "Usage: ./ExecuteKernel <verify [yes/no]>" << std::endl;
    return 1;
  }
  bool verify = true;
  if (argc == 2) {
    const std::string arg(argv[1]);
    if (arg == "yes") {
      verify = true;
    } else if (arg == "no") {
      verify = false;
    }
  }
  std::vector<DataPack> memory0(kWriteSize, DataPack(static_cast<Data_t>(0)));
  std::vector<DataPack> memory1(kWriteSize, DataPack(static_cast<Data_t>(0)));
  try {
    hlslib::ocl::Context context("Xilinx");
    auto device0 = context.MakeBuffer<DataPack, hlslib::ocl::Access::readWrite>(
        hlslib::ocl::MemoryBank::bank0, memory0.cbegin(), memory0.cend());
    auto device1 = context.MakeBuffer<DataPack, hlslib::ocl::Access::readWrite>(
        hlslib::ocl::MemoryBank::bank0, memory1.cbegin(), memory1.cend());
    auto kernel = context.MakeKernelFromBinary(
        "sdaccel_hw.xclbin", "Kernel", device0, device1, device0, device1);
    std::cout << "Executing kernel..." << std::flush;
    const auto elapsed = kernel.ExecuteTask();
    std::cout << " Finished in " << elapsed << " seconds, ("
              << std::setprecision(2)
              << 1e-9 * kTotalCols * kRows * kTime * 4 / elapsed << " GOp/s)"
              << std::endl;
    device0.CopyToHost(memory0.begin());
    device1.CopyToHost(memory1.begin());
  } catch (std::runtime_error const &err) {
    std::cerr << "Execution failed with error: \"" << err.what() << "\"."
              << std::endl;
    return 1;
  }
  if (verify) {
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
            std::cerr << "Mismatch at (" << r << ", " << c
                      << "): " << Data_t(result[r * kCols + c][w])
                      << " (should be "
                      << static_cast<double>(
                             reference[r * kTotalCols + c * kDataWidth + w])
                      << ")\n";
            return 1;
          }
        }
      }
    }
  }
  return 0;
}
