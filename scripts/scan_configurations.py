#!/usr/bin/env python3
from datetime import timedelta
import itertools
import multiprocessing as mp
import os
import re
import signal
import subprocess as sp
import sys
import time

def conf_string(conf):
  return (str(conf.dtype) + "_" + str(conf.targetClock) + "_" +
          str(conf.units) + "_" + str(conf.width) + "_" + str(conf.tileSize))

def run_process(command, directory, logPath="log"):
  proc = sp.Popen(command, stdout=sp.PIPE, stderr=sp.PIPE,
                  universal_newlines=True, cwd=directory)
  stdout, stderr = proc.communicate()
  with open(os.path.join(directory, logPath + ".out"), "a") as outFile:
    outFile.write(stdout)
  with open(os.path.join(directory, logPath + ".err"), "a") as outFile:
    outFile.write(stderr)
  return proc.returncode

class Consumption(object):
  def __init__(self, conf, status, lut, ff, dsp, bram, power, clock):
    self.conf = conf
    self.status = status
    self.lut = lut
    self.ff = ff
    self.dsp = dsp
    self.bram = bram
    self.power = power
    self.clock = clock
  def csv_cols():
    return "status,dtype,targetClock,units,width,depth,tileSize,clock,dsp,lut,ff,bram,power"
  def __repr__(self):
    return ",".join(map(str, [
        self.status, self.conf.dtype, self.conf.targetClock, self.conf.units,
        self.conf.width, self.conf.depth, self.conf.tileSize, self.clock,
        self.dsp, self.lut, self.ff, self.bram, self.power]))

class Configuration(object):
  def __init__(self, dtype, targetClock, units, width, tileSize, options):
    self.dtype = dtype
    if self.dtype not in ["float", "double"]:
      raise ValueError("Data type must be float or double")
    self.targetClock = targetClock
    self.tileSize = tileSize
    self.width = width
    self.units = units
    self.depth = int(self.units / self.width)
    self.options = options
    self.consumption = None

def cmake_command(conf, options=""):
  timesteps = 256 * conf.depth
  dim = conf.tileSize * 4 * conf.width
  return ("cmake ../../ " + " ".join(options) +
          " -DSTENCIL_KEEP_INTERMEDIATE=ON" +
          " -DSTENCIL_TARGET_CLOCK={}".format(conf.targetClock) +
          " -DSTENCIL_TARGET_TIMING={}".format(1000/conf.targetClock) +
          " -DSTENCIL_ROWS={}".format(dim) +
          " -DSTENCIL_COLS={}".format(dim) +
          " -DSTENCIL_BLOCKS={}".format(8) +
          " -DSTENCIL_TIME={}".format(timesteps) +
          " -DSTENCIL_DATA_TYPE={}".format(conf.dtype) +
          " -DSTENCIL_DATA_WIDTH={}".format(conf.width) +
          " -DSTENCIL_DEPTH={}".format(conf.depth))

def run_build(conf):
  cmakeCommand = cmake_command(conf, options=conf.options)
  confStr = conf_string(conf)
  confDir = os.path.join("scan", "build_" + confStr)
  try:
    os.makedirs(confDir)
  except:
    pass
  print(confStr + ": configuring...")
  if run_process(cmakeCommand.split(), confDir) != 0:
    raise Exception(confStr + ": Configuration failed.")
  print(confStr + ": cleaning folder...")
  if run_process("make clean".split(), confDir) != 0:
    raise Exception(confStr + ": Clean failed.")
  print(confStr + ": building software...")
  if run_process("make".split(), confDir) != 0:
    raise Exception(confStr + ": Software build failed.")
  begin = time.time()
  print(confStr + ": running HLS...")
  if run_process("make synthesis".split(), confDir) != 0:
    raise Exception(confStr + ": HLS failed.")
  print(confStr + ": finished HLS after {}.".format(
      str(timedelta(seconds=time.time() - begin))))
  begin = time.time()
  print(confStr + ": starting kernel build...")
  if run_process("make kernel".split(), confDir) != 0:
    try:
      with open(os.path.join(confDir, "log.out")) as logFile:
        m = re.search("auto frequency scaling failed", logFile.read())
        if not m:
          print(confStr + ": failed after {}.".format(
              str(timedelta(seconds=time.time() - begin))))
        else:
          print(confStr + ": failed timing after {}.".format(
              str(timedelta(seconds=time.time() - begin))))
    except FileNotFoundError:
      print(confStr + ": failed after {}.".format(
          str(timedelta(seconds=time.time() - begin))))
  else:
    print(confStr + ": finished in {}.".format(
        str(timedelta(seconds=time.time() - begin))))

def extract_result_build(conf):
  implFolder = os.path.join(
      "scan", "build_" + conf_string(conf), "_xocc_Stencil_sdaccel_hw.dir",
      "impl", "build","system", "sdaccel_hw", "bitstream", "sdaccel_hw_ipi",
      "ipiimpl", "ipiimpl.runs", "impl_1")
  if not os.path.exists(implFolder):
    conf.consumption = Consumption(conf, "no_build", None, None, None, None, None, None)
    return
  status = check_build_status(implFolder)
  reportPath = os.path.join(
      implFolder, "xcl_design_wrapper_utilization_placed.rpt")
  if not os.path.isfile(reportPath):
    conf.consumption = Consumption(conf, status, None, None, None, None, None, None)
    return
  report = open(reportPath).read()
  luts = int(re.search(
      "Slice LUTs[ \t]*\|[ \t]*([0-9]+)", report).group(1))
  ff = int(re.search(
      "Slice Registers[ \t]*\|[ \t]*([0-9]+)", report).group(1))
  bram = int(re.search(
      "Block RAM Tile[ \t]*\|[ \t]*([0-9]+)", report).group(1))
  dsp = int(re.search(
      "DSPs[ \t]*\|[ \t]*([0-9]+)", report).group(1))
  reportPath = os.path.join(implFolder, "xcl_design_wrapper_power_routed.rpt")
  if not os.path.isfile(reportPath):
    power = 0
  else:
    report = open(reportPath).read()
    power = float(re.search(
        "Total On-Chip Power \(W\)[ \t]*\|[ \t]*([0-9\.]+)", report).group(1))
  conf.consumption = Consumption(conf, status, luts, ff, dsp, bram, power)

def check_build_status(implFolder):
  try:
    report = open(os.path.join(implFolder, "build.log")).read()
  except:
    try:
      report = open(os.path.join(implFolder, "runme.log")).read()
    except:
      return "no_build"
  m = re.search("The design failed to meet the timing requirements|The design did not meet timing requirements", report)
  if m:
    return "failed_timing"
  m = re.search("The packing of instances into the device could not be obeyed|Placer could not place all instances",
                report)
  if m:
    return "failed_place"
  m = re.search("Error(s) found during DRC", report)
  if m:
    return "failed_route"
  m = re.search("Design utilization is very high.", report)
  if m:
    return "failed_utilization"
  m = re.search("Unable to write message PB_Results to top_power_routed.rpx", report)
  if m:
    return "failed_log"
  for fileName in os.listdir(implFolder):
    if len(fileName) >= 4 and fileName[-4:] == ".bit":
      return "success"
  return "failed_unknown"

def get_conf(folderName):
  m = re.search("build_(float|double)_([0-9]+)_([0-9]+)_([0-9]+)",
                folderName)
  if not m:
    return None
  return Configuration(m.group(1), int(m.group(2)), int(m.group(3)),
                       int(m.group(4)))

def extract_to_file():
  confs = []
  for fileName in os.listdir("scan"):
    conf = get_conf(fileName)
    if not conf:
      continue
    print("Extracting {}...".format(fileName))
    extract_result_build(conf)
    confs.append(conf)
  with open(os.path.join("scan", "results.csv"), "w") as resultFile:
    resultFile.write(Consumption.csv_cols() + "\n")
    for conf in confs:
      resultFile.write(str(conf.consumption) + "\n")

def scan_configurations(numProcs, configurations):
  try:
    os.makedirs("scan")
  except FileExistsError:
    pass
  pool = mp.Pool(processes=numProcs)
  try:
    pool.map(run_build, configurations)
  except KeyboardInterrupt:
    pool.terminate()
  print("All configurations finished running.")

def print_usage():
  print("Usage: " +
        "\n  ./scan_configurations.py " +
        "<number of processes> " +
        "<data type...> " +
        "<target clock...> " +
        "<tile size...> " +
        "<data width...> " +
        "<compute units...> " +
        "<additional CMake options...>" +
        "\n  ./scan_configurations.py extract",
        file=sys.stderr)

if __name__ == "__main__":

  if len(sys.argv) == 2 and sys.argv[1] == "extract":
    extract_to_file()
    sys.exit(0)

  if len(sys.argv) < 7:
    print_usage()
    sys.exit(1)

  numProcs = int(sys.argv[1])
  types = sys.argv[2].split(",")
  targetClocks = [int(x) for x in sys.argv[3].split(",")]
  tileSizes = [int(x) for x in sys.argv[4].split(",")]
  dataWidths = [int(x) for x in sys.argv[5].split(",")]
  computeUnits = [int(x) for x in sys.argv[6].split(",")]
  options = sys.argv[7:]
  configurations = [Configuration(*x, options=options) for x in itertools.product(
      types, targetClocks, computeUnits, dataWidths, tileSizes)]
  scan_configurations(numProcs, configurations)
