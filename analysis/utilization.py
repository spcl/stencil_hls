#!/usr/bin/env python3
import csv, itertools, json, os, re, sys
import numpy as np
from _parse_arguments import parse_args

def draw_roofline(ax, peak, bandwidth, label=None, ciLim=[-4, 9]):
  ci = np.logspace(ciLim[0], ciLim[1], (ciLim[1]-ciLim[0]+1)*1000, base=2)
  roofline = np.minimum(ci*bandwidth, peak*np.ones(ci.shape))/1e9
  ridgeIndex = np.nonzero(peak <= ci*bandwidth)[0][0]
  ridgeCi = ci[ridgeIndex]
  ridgePeak = roofline[ridgeIndex]
  rooflinePlot = ax.plot(ci, roofline, "-", linewidth=2, label=label)
  ridge, = ax.plot([ridgeCi, ridgeCi], [ax.get_ylim()[0], ridgePeak], "--",
                   linewidth=2)
  ax.plot([ax.get_xlim()[0], ridgeCi], [ridgePeak, ridgePeak], "--",
          linewidth=2, color=ridge.get_color())
  ax.set_yticks(sorted(list(ax.get_yticks()) + [ridgePeak]))
  ax.set_ylim([ax.get_yticks()[1], ax.get_yticks()[-2]])

def peak_utilization(board, costs, ops, maxLut=1.0, maxDsp=1.0, maxBram=1.0):
  utilization = {
      "dspCount": 0,
      "lutCount": 0,
      "bramCount": 0}
  nInstances = 0
  while True:
    newDsp = utilization["dspCount"]
    newLut = utilization["lutCount"]
    newBram = utilization["bramCount"]
    for (opType, opName, opCore, count) in ops:
      newDsp += count*costs[opType][opName][opCore]["dsp"]
      newLut += count*costs[opType][opName][opCore]["lut"]
      newBram += count*costs[opType][opName][opCore]["bram"]
    if (newDsp > board["compute"]["dspCount"]*maxDsp or
        newLut > board["compute"]["lutCount"]*maxLut or
        newBram > board["compute"]["bramCount"]*maxBram):
      break
    utilization["dspCount"] = newDsp
    utilization["lutCount"] = newLut
    utilization["bramCount"] = newBram
    nInstances += 1
  return nInstances, utilization

def generate_permutations(costs, ops):
  permutations = []
  options = {}
  for typeName, typeVal in ops.items():
    for opName, opVal in typeVal.items():
      if type(opVal) == dict:
        key = (typeName, opName)
        options[key] = []
        for coreName, count in opVal.items():
          options[key].append((typeName, opName, coreName, count))
      else:
        key = (typeName, opName)
        options[key] = []
        for coreName in costs[typeName][opName]:
          options[key].append((typeName, opName, coreName, opVal))
  return list(itertools.product(*options.values()))

def utilization(operations, args, verbose=True):
  if "board" in args:
    boardName = args["board"]
    del args["board"]
  else:
    boardName = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "boards", "ADM-PCIE-7V3.board")
  with open(boardName) as boardFile:
    board = json.load(boardFile)

  if "costs" in args:
    costsPath = args["costs"]
    del args["costs"]
  else:
    costsPath = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "costs", "Virtex7Datasheet.costs")

  if "maxluts" in args:
    maxLut = max(min(float(args["maxluts"]), 1), 0)
    del args["maxluts"]
  else:
    maxLut = 1.0

  if "maxdsps" in args:
    maxDsp = max(min(float(args["maxdsps"]), 1), 0)
    del args["maxdsps"]
  else:
    maxDsp = 1.0

  if "maxbram" in args:
    maxBram = max(min(float(args["maxbram"]), 1), 0)
    del args["maxbram"]
  else:
    maxBram = 1.0

  if "clock" in args:
    clock = 1e6*max(0, float(args["clock"]))
    del args["clock"]
  else:
    clock = 200e6

  if "sdaccel" in args and bool(args["sdaccel"]) == True:
    sdaccel = True
    if verbose:
      print("Assuming SDAccel: kernel will be clocked at 200 MHz and resource " +
            "usage constrained to 70% of the board.")
    clock = 200e6
    maxLut = min(maxLut, 0.7)
    maxDsp = min(maxDsp, 0.7)
    maxBram = min(maxBram, 0.7)
    del args["sdaccel"]
  else:
    sdaccel = False

  if "latex" in args:
    useLatex = True
    del args["latex"]
  else:
    useLatex = False

  rooflineFilename = None
  if "roofline" in args:
    roofline = True
    del args["roofline"]
    if "saveas" in args:
      rooflineFilename = args["saveas"]
      del args["saveas"]
  else:
    roofline = False

  for arg in args:
    print("Warning: unused argument \"{}\" with value \"{}\"".format(
        arg, args[arg]))

  with open(costsPath, "r") as inFile:
    costs = json.load(inFile)

  permutations = generate_permutations(costs, operations)

  nInstances = 0
  bestPerm = None
  for perm in permutations:
    testInstances, testUtilization = peak_utilization(
        board, costs, perm, maxLut, maxDsp, maxBram)
    if testInstances > nInstances:
      nInstances = testInstances
      utilization = testUtilization
      bestPerm = perm
  nOps = 0
  for (_, _, _, count) in bestPerm:
    nOps += nInstances*count
  peak = int(clock*nOps)

  if verbose:

    print("Operation counts:")
    for op, dtype, core, count in bestPerm:
      opStr = op + "/" + dtype + (("/" + core) if core else "")
      print("  {}: {} instances".format(opStr, count*nInstances))
    print("Total: {} ops/cycle".format(nOps))
    print("Peak: {:.1f} Gops/s at {} MHz".format(1e-9*peak, 1e-6*clock))
    print("Utilization:" +
          "\n  {} / {} DSP ({:.1f}%, {:.1f}% of available)".format(
              utilization["dspCount"], board["compute"]["dspCount"],
              1e2*utilization["dspCount"]/board["compute"]["dspCount"],
              1e2*utilization["dspCount"]/(maxDsp*board["compute"]["dspCount"])) +
          "\n  {} / {} LUT ({:.1f}%, {:.1f}% of available)".format(
              utilization["lutCount"], board["compute"]["lutCount"],
              1e2*utilization["lutCount"]/board["compute"]["lutCount"],
              1e2*utilization["lutCount"]/(maxLut*board["compute"]["lutCount"])),
          "\n  {} / {} BRAM ({:.1f}%, {:.1f}% of available)".format(
              utilization["bramCount"], board["compute"]["bramCount"],
              1e2*utilization["bramCount"]/board["compute"]["bramCount"],
              1e2*utilization["bramCount"]/(maxBram*board["compute"]["bramCount"])))

  if roofline:

    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
    from _plotstyle import new_plot

    memoryBandwidth = (board["memory"]["clock"] *
                       board["memory"]["width"] *
                       (board["memory"]["dimms"] if not sdaccel else 1)) / 8 # Convert to bytes

    # Plot roofline
    if useLatex:
      from _plotstyle import new_plot
      fig, ax = new_plot()
    else:
      fig, ax = plt.subplots()
    ax.set_xscale("log", basex=2)
    ax.set_yscale("log")
    match = re.search("(.+)\.[^\.]*", operationsFilename)
    if match:
      title = match.group(1)
    else:
      title = ""
    ax.set_title(title if not sdaccel else "SDAccel " + title)
    ax.set_ylabel("Performance [GOP/s]")
    ax.set_xlabel("Computational intensity [OP/byte]")
    ax.get_yaxis().set_major_formatter(ticker.ScalarFormatter())
    draw_roofline(ax, peak, memoryBandwidth)
    # ax.legend(loc=4)
    ax.set_xticks(ax.get_xticks()[1:-1])
    if rooflineFilename:
      fig.savefig(rooflineFilename, bbox_inches="tight")
    else:
      fig.show()
      input()

  return bestPerm, nInstances, nOps, peak

if __name__ == "__main__":

  if len(sys.argv) < 2 or sys.argv[1] in ["-help", "--help"]:
    print("Usage: <operations file>\n" +
          "  [-board=<path to board specification [Default: ADM-PCIE-7V3.board]>]"
          + "\n  [-costs=<path to cost specifications [Default: Virtex7Datasheet.costs]>]"
          + "\n  [-clock=<clock rate in MHz [Default: 200]]"
          + "\n  [-sdaccel=<assume SDAccel restrictions [Default: false]>]"
          + "\n  [-maxluts=<max fraction of LUTs to consume [Default: 1.0]>]"
          + "\n  [-maxdsps=<max fraction of DSPs to consume [Default: 1.0]>]"
          + "\n  [-maxbrams=<max fraction of BRAMs to consume [Default: 1.0]>]"
          + "\n  [-roofline=<draw roofline [Default: false]> "
          + "[-saveas=<plot filename>] "
          + "[-latex=<latex formatting [Default: False]>]]")
    sys.exit(1)

  operationsFilename = sys.argv[1]

  with open(operationsFilename) as operationsFile:
    operations = json.load(operationsFile)

  args = parse_args(sys.argv[2:])

  utilization(operations, args)

