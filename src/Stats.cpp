#include "Stencil.h"

constexpr unsigned long BufferSpace() {
  return (kBlocks == 1) ? (kDepth * 2 * kBlockWidthKernel * kBlocks * kKernelWidth)
                        : (2 * kDepth * kBlockWidthKernel * kKernelWidth +
                           2 * kDepth * kDepth + 2 * kDepth);
}

constexpr unsigned long CyclesRequired() {
  return (kBlockWidthKernel + 2 * kHaloKernel) * kRows * kBlocks * kTimeFolded;
}

constexpr float Efficiency() {
  return (kBlocks == 1)
             ? 1
             : kBlockWidthKernel / static_cast<float>(kBlockWidthKernel + 2 * kHaloKernel);
}

constexpr int OpsPerCycle() { return kDepth * kKernelWidth * 4; }

int main(int argc, char **argv) {
  float clock = kTargetClock;
  if (argc > 1) {
    clock = std::stof(argv[1]);
  }
  std::cout << "Rows:           " << kRows << "\n";
  std::cout << "Cols:           " << kCols << "\n";
  std::cout << "Total elements: " << kRows * kCols << "\n";
  std::cout << "Data width:     " << kKernelWidth << " elements / "
            << sizeof(Kernel_t) << " bytes\n";
  std::cout << "Total bursts:   " << kTotalElementsKernel << " / "
            << kTotalInputKernel << " with halos\n";
  std::cout << "Burst requests: " << kBlocks * kRows * kTimeFolded << "\n";
  std::cout << "Depth:          " << kDepth << "\n";
  std::cout << "Blocks:         " << kBlocks << "\n";
  std::cout << "Block size:     " << kBlockWidthKernel << " bursts / "
            << kBlockWidthKernel * kKernelWidth << " elements\n";
  std::cout << "Halo size:      " << kHaloKernel << " bursts\n";
  std::cout << "Efficiency:     " << 100 * Efficiency() << "%\n";
  std::cout << "Buffer space:   " << BufferSpace() << " elements / "
            << BufferSpace() * sizeof(Data_t) << " bytes\n";
  std::cout << "Timesteps:      " << kTimeTotal << " / " << kTimeFolded
            << " folded\n";
  std::cout << "Total cycles:   " << CyclesRequired() << " (plus latency)\n";
  std::cout << "Expected time:  " << CyclesRequired() / (1e6 * clock)
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
  std::cout << "Bandwidth required to saturate: " << 2 * sizeof(Kernel_t) * 1e-3 * clock
            << " GB/s\n";
}
