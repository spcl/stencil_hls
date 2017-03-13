#!/usr/bin/env python
from utilization import peak_utilization
import numpy as np
import sys
import multiprocessing as mp
from functools import partial

class DataPoint(object):
  def __init__(self, compute, bram, bramDepth, bandwidth, frequency):
    self.compute = compute
    self.bram = bram
    self.bramDepth = bramDepth
    self.bandwidth = bandwidth
    self.frequency = frequency
    self.frequencyOpt = None
    self.depthOpt = None
    self.dataWidthOpt = None
    self.tileSizeOpt = None
    self.computeOpt = None
    self.bramOpt = None
    self.efficiency = None
    self.effectiveCompute = None
    self.perf = None
  def add_optimized(self, frequencyOpt, depthOpt, dataWidthOpt, tileSizeOpt):
    self.frequencyOpt = frequencyOpt
    self.depthOpt = depthOpt
    self.dataWidthOpt = dataWidthOpt
    self.tileSizeOpt = tileSizeOpt
    self.computeOpt = dataWidthOpt * depthOpt
    self.bramOpt = self.bram_requirement()
    self.efficiency = efficiency(tileSizeOpt, depthOpt)
    self.effectiveCompute = self.efficiency * self.computeOpt
    self.perf = self.effectiveCompute * 1e-3 * self.frequencyOpt
  def __repr__(self):
    return ",".join(map(str, [
        self.compute, self.bram, self.bramDepth, self.bandwidth, self.frequency,
        self.frequencyOpt, self.depthOpt, self.dataWidthOpt, self.tileSizeOpt,
        self.computeOpt, self.bramOpt, self.efficiency,
        self.effectiveCompute, self.perf]))
  @classmethod
  def from_string(cls, string):
    t = string.replace("\n", "").split(",")
    compute = int(t[0])
    bram = int(t[1])
    bramDepth = int(t[2])
    bandwidth = float(t[3])
    frequency = float(t[4])
    frequencyOpt = int(t[5])
    depthOpt = int(t[6])
    dataWidthOpt = int(t[7])
    tileSizeOpt = int(t[8])
    computeOpt = int(t[9])
    bramOpt = int(t[10])
    dp = cls(compute, bram, bramDepth, bandwidth, frequency)
    dp.add_optimized(frequencyOpt, depthOpt, dataWidthOpt, tileSizeOpt)
    return dp
  def bram_requirement_depth(self):
    stages = (self.tileSizeOpt + 2 * np.arange(1, self.depthOpt + 1) - 1) / self.bramDepth
    stages = np.ceil(stages).astype(np.int)
    ret = np.sum(stages)
    return ret
  def bram_requirement(self):
    return 2 * self.dataWidthOpt * self.bram_requirement_depth()

def bram_requirement_depth(depth, tileSize, bramDepth):
  stages = (tileSize + 2 * np.arange(1, depth + 1) - 1) / bramDepth
  stages = np.ceil(stages).astype(np.int)
  ret = np.sum(stages)
  return ret

def bram_requirement(width, depth, tileSize, bramDepth):
  return 2 * width * bram_requirement_depth(depth, tileSize, bramDepth)

def efficiency(tileSize, depth):
  return tileSize / (tileSize + 2 * depth)

def effective_performance(frequency, width, depth, tileSize):
  return frequency * depth * width * efficiency(tileSize, depth)

def optimize_dp(dp, tileSizeSteps, bramReq):
    # frequencies = np.arange(dp.frequency / 2, dp.frequency + 1, 25)
    frequencies = [dp.frequency]
    computeSteps = np.arange(1, dp.compute + 1)
    best = (0, 0, 0, 0, 0)
    for f in frequencies:
      widthMax = int(dp.bandwidth / (4e-3 * f))
      for w in range(1, widthMax+1):
        for c in computeSteps[w-1:]:
          d = int(c / w)
          perf = effective_performance(f, w, d, tileSizeSteps)
          invalid = 2 * w * bramReq[d - 1, :] > dp.bram
          if np.count_nonzero(invalid) < invalid.size:
            perf[invalid] = 0
          else:
            continue
          i = np.argmax(perf)
          if perf[i] > best[4]:
            best = (f, d, w, tileSizeSteps[i], perf[i])
    dp.add_optimized(best[0], best[1], best[2], best[3])
    return dp

def optimize(dataPoints):
  """Bandwidth expected in GB/s, frequency in MHz"""

  # Create table of BRAM requirements
  frequencyMax = max(dataPoints, key=lambda x: x.frequency).frequency
  computeMax = max(dataPoints, key=lambda x: x.compute).compute
  bandwidthMax = max(dataPoints, key=lambda x: x.bandwidth).bandwidth
  widthMax = int(bandwidthMax / (4e-3 * frequencyMax))
  depthSteps = np.arange(1, computeMax + 1)
  tileSizeSteps = np.arange(32, 2048+1, 32)
  bramReq = np.empty((computeMax, len(tileSizeSteps)))
  for i in range(computeMax):
    for j, t in enumerate(tileSizeSteps):
      bramReq[i, j] = bram_requirement_depth(i + 1, t, 512)

  # Loop over data points
  pool = mp.Pool(processes=4)
  optimize_dp_func = partial(optimize_dp, tileSizeSteps=tileSizeSteps,
                             bramReq=bramReq)
  dataPoints = pool.map(optimize_dp_func, dataPoints)

  return dataPoints

if __name__ == "__main__":
  if len(sys.argv) < 6:
    print("Usage: <compute units> <bram blocks> <bram depth> <bandwidth [GB/s]> <frequency [GHz]>")
    sys.exit(1)
  compute = int(sys.argv[1])
  bram = int(sys.argv[2])
  bramDepth = int(sys.argv[3])
  bandwidth = float(sys.argv[4])
  frequency = float(sys.argv[5])
  dataPoint = DataPoint(compute, bram, bramDepth, bandwidth, frequency)
  dataPoint = optimize([dataPoint])[0]
  dataWidthMax = int(dataPoint.bandwidth / (4e-3 * dataPoint.frequency))
  print("Frequency: {}/{}".format(dataPoint.frequencyOpt, dataPoint.frequency))
  print("Data width: {}/{}".format(dataPoint.dataWidthOpt, dataWidthMax))
  print("Compute used: {}/{}".format(dataPoint.computeOpt, dataPoint.compute))
  print("Pipeline depth: {}".format(dataPoint.depthOpt))
  print("Tile size: {}".format(dataPoint.tileSizeOpt))
  print("BRAM used: {}/{}".format(dataPoint.bramOpt, dataPoint.bram))
  print("Throughput: {} GStencil/s".format(dataPoint.perf))
  print("Performance: {} GOp/s".format(dataPoint.perf * 4))
