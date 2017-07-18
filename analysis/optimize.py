#!/usr/bin/env python3
from opt_db import Optimized
import save_to_db
import sys
import numpy as np
import itertools
import multiprocessing as mp
from functools import partial

def efficiency(tileSize, depth):
  return tileSize / (tileSize + 2 * depth)

def effective_performance(frequency, width, depth, tileSize):
  return frequency * depth * width * efficiency(tileSize, depth)

def bram_requirement_depth(depth, tileSize, bramDepth):
  stages = (tileSize + 2 * np.arange(1, depth + 1) - 1) / bramDepth
  stages = np.ceil(stages).astype(np.int)
  ret = np.sum(stages)
  return ret

def bram_requirement(width, depth, tileSize, bramDepth):
  return 2 * width * bram_requirement_depth(depth, tileSize, bramDepth)

def optimize_single(opt, tileSizeSteps, bramReq):
    frequencies = [opt.frequency]
    computeSteps = np.arange(1, opt.compute + 1)
    best = (0, 0, 0, 0, 0)
    for f in frequencies:
      widthMax = int(opt.bandwidth / (4e-3 * f))
      for w in range(1, widthMax+1):
        for c in computeSteps[w-1:]:
          d = int(c / w)
          perf = effective_performance(f, w, d, tileSizeSteps)
          invalid = 2 * w * bramReq[d - 1, :] > opt.bram
          if np.count_nonzero(invalid) < invalid.size:
            perf[invalid] = 0
          else:
            continue
          i = np.argmax(perf)
          if perf[i] > best[4]:
            best = (f, d, w, tileSizeSteps[i], perf[i])
    opt.add_optimized(float(best[0]), int(best[1]), int(best[2]), int(best[3]))
    return opt

def optimize(frequencyRange, bandwidthRange, computeRange, bramRange,
             bramDepth):

  opts = [Optimized(frequency=float(conf[0]), bandwidth=float(conf[1]),
                    compute=int(conf[2]), bram=int(conf[3]),
                    bramDepth=int(bramDepth))
          for conf in itertools.product(frequencyRange, bandwidthRange,
                                        computeRange, bramRange)]

  # Create table of BRAM requirements
  frequencyMax = max(frequencyRange)
  computeMax = max(computeRange)
  bandwidthMax = max(bandwidthRange)
  widthMax = int(bandwidthMax / (4e-3 * frequencyMax))
  depthSteps = np.arange(1, computeMax + 1)
  tileSizeSteps = np.arange(32, 2048+1, 32, dtype=int)
  bramReq = np.empty((computeMax, len(tileSizeSteps)))
  for i in range(computeMax):
    for j, t in enumerate(tileSizeSteps):
      bramReq[i, j] = bram_requirement_depth(i + 1, t, 512)

  # Loop over data points
  pool = mp.Pool(processes=4)
  optimize_single_func = partial(optimize_single, tileSizeSteps=tileSizeSteps,
                                 bramReq=bramReq)
  opts = pool.map(optimize_single_func, opts)

  return opts

if __name__ == "__main__":
  if len(sys.argv) < 6:
    print("Usage: <frequency range> <bandwidth range> <compute range> <bram range> <bram depth>")
    sys.exit(1)
  frequency = np.arange(*[float(x) for x in sys.argv[1].split(",")]) if "," in sys.argv[1] else [float(sys.argv[1])]
  bandwidth = np.arange(*[float(x) for x in sys.argv[2].split(",")]) if "," in sys.argv[2] else [float(sys.argv[2])]
  compute = np.arange(*[int(x) for x in sys.argv[3].split(",")]) if "," in sys.argv[3] else [int(sys.argv[3])]
  bram = np.arange(*[int(x) for x in sys.argv[4].split(",")]) if "," in sys.argv[4] else [int(sys.argv[4])]
  bramDepth = int(sys.argv[5])
  opts = optimize(frequency, bandwidth, compute, bram, bramDepth)
  save_to_db.save_optimized(opts)
  if len(opts) > 10:
    print("Successfully optimized {} configurations.".format(len(opts)))
  else:
    print("Successfully optimized {} configuration...".format(len(opts)))
    for opt in opts:
      print(opt, "=", opt.performance(), "GStencil/s")
