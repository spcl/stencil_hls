/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#include "Stencil.h"

constexpr unsigned long BufferSpace() {
  return (kBlocks == 1)
             ? (kDepth * 2 * kBlockWidthKernel * kBlocks * kKernelWidth)
             : (2 * kDepth * kBlockWidthKernel * kKernelWidth +
                2 * kDepth * kDepth + 2 * kDepth);
}

unsigned long CyclesRequired(const int timesteps_folded) {
  return (static_cast<unsigned long>(kBlockWidthKernel) + 2 * kHaloKernel) *
         kRows * kBlocks * timesteps_folded;
}

constexpr float Efficiency() {
  return (kBlocks == 1)
             ? 1
             : kBlockWidthKernel /
                   static_cast<float>(kBlockWidthKernel + 2 * kHaloKernel);
}

constexpr int OpsPerCycle() { return kDepth * kKernelWidth * 4; }

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " <timesteps> [<frequency [MHz]>]]"
              << std::endl;
    return 1;
  }

  const auto timesteps = std::stoi(argv[1]);
  if (timesteps % kDepth != 0) {
    std::cerr << "Number of timesteps (" << timesteps
              << ") must be divisible by depth (" << kDepth << ")\n";
    return 2;
  }
  const auto timesteps_folded = timesteps / kDepth;

  float clock = kTargetClock;
  if (argc > 2) {
    clock = std::stof(argv[2]);
  }

  std::cout << "Rows:           " << kRows << "\n";
  std::cout << "Cols:           " << kCols << "\n";
  std::cout << "Total elements: " << kRows * kCols << "\n";
  std::cout << "Data width:     " << kKernelWidth << " elements / "
            << sizeof(Kernel_t) << " bytes\n";
  std::cout << "Total bursts:   " << kTotalElementsKernel << " / "
            << kTotalInputKernel << " with halos\n";
  std::cout << "Burst requests: " << kBlocks * kRows * timesteps << "\n";
  std::cout << "Depth:          " << kDepth << "\n";
  std::cout << "Blocks:         " << kBlocks << "\n";
  std::cout << "Block size:     " << kBlockWidthKernel << " bursts / "
            << kBlockWidthKernel * kKernelWidth << " elements\n";
  std::cout << "Halo size:      " << kHaloKernel << " bursts\n";
  std::cout << "Efficiency:     " << 100 * Efficiency() << "%\n";
  std::cout << "Buffer space:   " << BufferSpace() << " elements / "
            << BufferSpace() * sizeof(Data_t) << " bytes\n";
  std::cout << "Timesteps:      " << timesteps << " / " << timesteps_folded
            << " folded\n";
  std::cout << "Total cycles:   " << CyclesRequired(timesteps_folded)
            << " (plus latency)\n";
  std::cout << "Expected time:  "
            << CyclesRequired(timesteps_folded) / (1e6 * clock)
            << " seconds.\n";
  std::cout << "Clock rate:     " << clock << " MHz";
  if (clock != kTargetClock) {
    std::cout << " (target " << kTargetClock << " MHz)";
  }
  std::cout << "\n";
  std::cout << "Instantiated Op/Cycle: " << OpsPerCycle() << "\n";
  std::cout << "Instantiated Perf:     " << std::setprecision(4)
            << (OpsPerCycle() * clock) / 1000 << " GOp/s\n";
  std::cout << "Effective Op/Cycle:    "
            << static_cast<unsigned long>(Efficiency() * OpsPerCycle()) << "\n";
  std::cout << "Effective Perf:        " << std::setprecision(4)
            << (Efficiency() * OpsPerCycle() * clock) / 1000 << " GOp/s\n";
  std::cout << "Bandwidth required to saturate: "
            << 2 * sizeof(Kernel_t) * 1e-3 * clock << " GB/s\n";
}
