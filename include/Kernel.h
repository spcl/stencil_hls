#pragma once

#include "hlslib/Burst.h"

constexpr int kDataWidth = 4;
constexpr int kPipeDepth = 4;
constexpr int kRows = 2048;
constexpr int kTotalCols = 2048;
constexpr int kCols = kTotalCols / kDataWidth;
constexpr int kDepth = 32;
constexpr int kHalo = (kDataWidth + kDepth - 1) / kDataWidth;
constexpr int kBlocks = 4;
constexpr int kMemSize = kRows * kCols;
constexpr int kTime = 1024;
constexpr int kColsPerBlock = kCols / kBlocks;
constexpr int kReadSize = 2 * (kColsPerBlock + kHalo) * kRows +
                          (kBlocks - 2) * (kColsPerBlock + 2 * kHalo) * kRows;
constexpr int kTimeFolded = kTime / kDepth;

using Data_t = float;
constexpr Data_t kBoundary = 1;
using Burst = hlslib::Burst<Data_t, kDataWidth>;

static_assert(kBlocks > 1, "Must have multiple blocks");
static_assert(kTimeFolded % 2 == 0, "Number of timesteps must be even");

extern "C" {

void Kernel(Burst const *read0, Burst const *read1, Burst *write0,
            Burst *write1);
}
