#include "Stencil.h"

constexpr unsigned long BufferSpace() {
  return (kBlocks == 1) ? (kDepth * 2 * kColsPerBlock * kBlocks * kDataWidth)
                        : (2 * kDepth * kColsPerBlock * kDataWidth +
                           2 * kDepth * kDepth + 2 * kDepth);
}

constexpr unsigned long CyclesRequired() {
  return (kColsPerBlock + 2 * kHalo) * kRows * kBlocks * kTimeFolded;
}

constexpr float Efficiency() {
  return (kBlocks == 1)
             ? 1
             : kColsPerBlock / static_cast<float>(kColsPerBlock + 2 * kHalo);
}

constexpr int OpsPerCycle() { return kDepth * kDataWidth * 4; }

int main(int argc, char **argv) {
  float clock = kTargetClock;
  if (argc > 1) {
    clock = std::stof(argv[1]);
  }
  std::cout << "Rows:           " << kRows << "\n";
  std::cout << "Cols:           " << kTotalCols << "\n";
  std::cout << "Total elements: " << kRows * kTotalCols << "\n";
  std::cout << "Data width:     " << kDataWidth << " elements / "
            << sizeof(DataPack) << " bytes\n";
  std::cout << "Total bursts:   " << kWriteSize << " / " << kReadSize
            << " with halos\n";
  std::cout << "Memory req.:    " << kBlocks * kRows * kTimeFolded << "\n";
  std::cout << "Depth:          " << kDepth << "\n";
  std::cout << "Blocks:         " << kBlocks << "\n";
  std::cout << "Block size:     " << kColsPerBlock << " bursts / "
            << kColsPerBlock * kDataWidth << " elements\n";
  std::cout << "Halo size:      " << kHalo << " bursts\n";
  std::cout << "Efficiency:     " << 100 * Efficiency() << "%\n";
  std::cout << "Buffer space:   " << BufferSpace() << " elements / "
            << BufferSpace() * sizeof(Data_t) << " bytes\n";
  std::cout << "Timesteps:      " << kTime << " / " << kTimeFolded
            << " folded\n";
  std::cout << "Total cycles:   " << CyclesRequired() << " (plus latency)\n";
  std::cout << "Clock rate:     " << clock << " MHz";
  if (clock != kTargetClock) {
    std::cout << " (target " << kTargetClock << " MHz)";
  }
  std::cout << "\n";
  std::cout << "Peak Op/Cycle:  " << OpsPerCycle() << "\n";
  std::cout << "Peak Perf:      " << std::setprecision(4)
            << (OpsPerCycle() * clock) / 1000 << " GOp/s\n";
  std::cout << "Real Op/Cycle:  "
            << static_cast<unsigned long>(Efficiency() * OpsPerCycle()) << "\n";
  std::cout << "Real Perf:      " << std::setprecision(4)
            << (Efficiency() * OpsPerCycle() * clock) / 1000 << " GOp/s\n";
}
