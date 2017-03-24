#!/usr/bin/env python3
import datetime
import itertools
import multiprocessing as mp
import os
import re
import shutil
import signal
import subprocess as sp
import sys
import time

# Fixed parameters
dim = 8192
blocks = 4
timesteps = 512
memoryWidth = 16

def conf_string(conf):
  return (conf.target + "_" + conf.dtype + "_" + str(conf.targetClock) + "_" +
          str(conf.width) + "_" + str(conf.compute) + "_" +
          str(conf.dim) + "_" + str(conf.blocks) + "_" + str(conf.time))

def run_process(command, directory, pipe=True, logPath="log"):
  if pipe:
    proc = sp.Popen(command, stdout=sp.PIPE, stderr=sp.PIPE,
                    universal_newlines=True, cwd=directory)
    stdout, stderr = proc.communicate()
    with open(os.path.join(directory, logPath + ".out"), "a") as outFile:
      outFile.write(stdout)
    with open(os.path.join(directory, logPath + ".err"), "a") as outFile:
      outFile.write(stderr)
  else:
    proc = sp.Popen(command,
                    universal_newlines=True, cwd=directory)
    proc.communicate()
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
    return ("status," + Configuration.csv_cols() +
            ",clock,dsp,lut,ff,bram,power")
  def __repr__(self):
    return ",".join(map(str, [
        self.status, str(conf), self.clock,
        self.dsp, self.lut, self.ff, self.bram, self.power]))

class Configuration(object):
  def __init__(self, target, dtype, targetClock, width, compute, dim, blocks,
               time, options=None):
    self.target = target
    self.dtype = dtype
    self.targetClock = targetClock
    self.width = width
    self.compute = units
    self.dim = dim
    self.blocks = blocks
    self.options = options
    self.consumption = None

def cmake_command(conf, options=""):
  if conf.compute % conf.width != 0:
    raise RuntimeError(
        "Invalid configuration: compute {} not divisable by width {}.".format(
            conf.compute, conf.width))
  depth = conf.compute / conf.width
  return ("cmake ../../ " + " ".join(options) +
          " -DSTENCIL_KEEP_INTERMEDIATE=ON" +
          " -DSTENCIL_TARGET={}".format(conf.target) +
          " -DSTENCIL_DATA_TYPE={}".format(conf.dtype) +
          " -DSTENCIL_TARGET_CLOCK={}".format(conf.targetClock) +
          " -DSTENCIL_TARGET_TIMING={}".format(1000/conf.targetClock) +
          " -DSTENCIL_KERNEL_WIDTH={}".format(conf.width) +
          " -DSTENCIL_MEMORY_WIDTH={}".format(16) +
          " -DSTENCIL_DEPTH={}".format(depth) +
          " -DSTENCIL_ROWS={}".format(conf.dim) +
          " -DSTENCIL_COLS={}".format(conf.dim) +
          " -DSTENCIL_BLOCKS={}".format(conf.blocks) +
          " -DSTENCIL_TIME={}".format(conf.time))

def create_builds(conf):
  cmakeCommand = cmake_command(conf, options=conf.options)
  confStr = conf_string(conf)
  confDir = os.path.join("scan", "build_" + confStr)
  try:
    os.makedirs(confDir)
  except:
    pass
  print(confStr + ": configuring...")
  with open(os.path.join(confDir, "Configure.sh"), "w") as confFile:
    confFile.write("#!/bin/sh\n{}".format(cmakeCommand))
  run_build(conf, clean=True, hardware=True)

def time_only(datetime):
  return datetime.strftime("%H:%M:%S")

def print_status(conf, status):
  print("[{}] {}: {}".format(time_only(datetime.datetime.now()),
                             str(conf_string(conf)), status))

def run_build(conf, clean=True, hardware=True):
  confStr = conf_string(conf)
  confDir = os.path.join("scan", "build_" + confStr)
  if run_process("sh Configure.sh".split(), confDir) != 0:
    raise Exception(confStr + ": Configuration failed.")
  if clean:
    print_status(conf, "cleaning folder...")
    if run_process("make clean".split(), confDir) != 0:
      raise Exception(confStr + ": Clean failed.")
  print_status(conf, "building software...")
  if run_process("make".split(), confDir) != 0:
    raise Exception(confStr + ": Software build failed.")
  if hardware:
    begin = time.time()
    print_status(conf, "running HLS...")
    if run_process("make synthesis".split(), confDir) != 0:
      raise Exception(confStr + ": HLS failed.")
    print_status(conf, "finished HLS after {}.".format(
        time_only(datetime.timedelta(seconds=time.time() - begin))))
    begin = time.time()
    print_status(conf, "starting kernel build...")
    if run_process("make kernel".split(), confDir) != 0:
      try:
        with open(os.path.join(confDir, "log.out")) as logFile:
          m = re.search("auto frequency scaling failed", logFile.read())
          if not m:
            print_status(conf, "FAILED after {}.".format(
                time_only(datetime.timedelta(seconds=time.time() - begin))))
          else:
            print(conf, "TIMING failed after {}.".format(
                time_only(datetime.timedelta(seconds=time.time() - begin))))
      except FileNotFoundError:
        print_status(conf, "FAILED after {}.".format(
            time_only(datetime.timedelta(seconds=time.time() - begin))))
    else:
      print_status(conf, " SUCCESS in {}.".format(
          time_only(datetime.timedelta(seconds=time.time() - begin))))
  with open(os.path.join(confDir, "log.out"), "r") as logFile:
    m = re.search(
        "The frequency is being automatically changed to ([0-9]+) MHz",
        logFile.read())
    with open(os.path.join(confDir, "frequency.txt"), "w") as clockFile:
      if not m:
        clockFile.write(str(conf.targetClock))
      else:
        clockFile.write(m.group(1))

def extract_result_build(conf):
  buildFolder = os.path.join("scan", "build_" + conf_string(conf))
  xoccFolder = None
  for fileName in os.listdir(buildFolder):
    if fileName.startswith("_xocc"):
      xoccFolder = fileName
      break
  if not xoccFolder:
    raise RuntimeError("Build folder not found for {}.".format(str(conf)))
  kernelFolder = os.path.join(
      buildFolder, xoccFolder,
      "impl", "build", "system", "sdaccel_hw", "bitstream")
  implFolder = os.path.join(
      kernelFolder,
      "sdaccel_hw_ipi", "ipiimpl", "ipiimpl.runs", "impl_1")
  if not os.path.exists(implFolder):
    conf.consumption = Consumption(conf, "no_build", None, None, None, None,
                                   None, None)
    return
  status = check_build_status(buildFolder)
  reportPath = os.path.join(
      implFolder, "xcl_design_wrapper_utilization_placed.rpt")
  if not os.path.isfile(reportPath):
    conf.consumption = Consumption(conf, status, None, None, None, None, None,
                                   None)
    return
  report = open(reportPath).read()
  try:
    luts = int(re.search(
        "CLB LUTs[ \t]*\|[ \t]*([0-9]+)", report).group(1))
    ff = int(re.search(
        "CLB Registers[ \t]*\|[ \t]*([0-9]+)", report).group(1))
  except AttributeError:
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
  try:
    with open(os.path.join(kernelFolder, "sdaccel_hw_ipi",
                           "vivado_warning.txt"), "r") as clockFile:
      warningText = clockFile.read()
      m = re.search("automatically changed to ([0-9]+) MHz", warningText)
      if m:
        clock = int(m.group(1))
      else:
        clock = conf.targetClock
  except FileNotFoundError:
    clock = conf.targetClock
  conf.consumption = Consumption(
      conf, status, luts, ff, dsp, bram, power, clock)

def check_build_status(buildFolder):
  kernelFolder = os.path.join(
      buildFolder, "_xocc_Stencil_sdaccel_hw.dir",
      "impl", "build", "system", "sdaccel_hw", "bitstream")
  try:
    log = open(
        os.path.join(buildFolder, "log.out"), "r").read()
  except:
    return "no_build"
  try:
    report = open(
        os.path.join(kernelFolder, "sdaccel_hw_ipi", "vivado.log")).read()
  except:
    return "no_build"
  m = re.search("Detail Placement failed", log)
  if m:
    return "failed_placement"
  m = re.search("Internal Data Exception", log)
  if m:
    return "crashed"
  m = re.search("auto frequency scaling failed", report)
  if m:
    return "failed_timing"
  for fileName in os.listdir(kernelFolder):
    if len(fileName) >= 7 and fileName[-7:] == ".xclbin":
      return "success"
  return "failed_unknown"

def get_conf(folderName):
  m = re.search("([^_]+)_([^_]+)_([1-9][0-9]*)_([1-9][0-9]*)_([1-9][0-9]*)_" +
                "([1-9][0-9]*)_([1-9][0-9]*)_([1-9][0-9]*)",
                folderName)
  if not m:
    return None
  return Configuration(m.group(1), int(m.group(2)), int(m.group(3)),
                       int(m.group(4)), int(m.group(5)), int(m.group(6)),
                       int(m.group(7)), int(m.group(8)))

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
    pool.map(create_builds, configurations)
  except KeyboardInterrupt:
    pool.terminate()
  print("All configurations finished running.")

def xocc_folder(conf):
  return "_xocc_Stencil_jacobi_{}_{}_c{}_w{}_d{}_{}x{}_b{}_t{}.dir".format(
      conf.target, conf.dtype, int(conf.targetClock), conf.width,
      conf.compute / conf.width, conf.dim, conf.dim, conf.blocks, conf.time)

def files_to_copy(conf):
  filesToCopy = ["Configure.sh", "frequency.txt"]
  xoccFolder = xocc_folder(conf)
  kernelFolder = os.path.join(
      xoccFolder, "impl", "build",
      "system", "sdaccel_hw", "bitstream", "sdaccel_hw_ipi")
  filesToCopy.append(os.path.join(kernelFolder, "vivado.log"))
  filesToCopy.append(os.path.join(kernelFolder, "vivado_warning.txt"))
  implFolder = os.path.join(
      kernelFolder, "ipiimpl", "ipiimpl.runs", "impl_1")
  filesToCopy.append(os.path.join(
      sourceDir, implFolder, "xcl_design_wrapper_utilization_placed.rpt"))
  filesToCopy.append(os.path.join(
      sourceDir, implFolder, "xcl_design_wrapper_power_routed.rpt"))
  return implFolder, filesToCopy

def package_configurations():
  packagedSomething = False
  for fileName in os.listdir("scan"):
    conf = get_conf(fileName)
    if not conf:
      continue
    sourceDir = os.path.join("scan", fileName)
    packageFolder = os.path.join("scan_packaged", fileName)
    kernelName = None
    kernelPath = None
    for subFile in os.listdir(sourceDir):
      if subFile.endswith(".xclbin"):
        kernelName = subFile[:-7]
        kernelPath = os.path.join(sourceDir, subFile)
        break
    if kernelPath == None or not os.path.exists(kernelPath):
      continue
    print("Packaging {}...".format(fileName))
    implFolder, filesToCopy = files_to_copy(conf)
    try:
      os.makedirs(os.path.join(packageFolder, implFolder))
    except FileExistsError:
      pass
    shutil.copy(kernelPath, packageFolder)
    for path in filesToCopy:
      shutil.copy(os.path.join(sourceDir, path),
                  os.path.join(packageFolder, path))
    packagedSomething = True
  if packagedSomething:
    print("Kernels and configuration files packaged into \"scan_packaged\".")
  else:
    print("No kernels found in \"scan\".")

def unpackage_configuration(conf):
  confStr = conf_string(conf)
  fileName = "build_" + confStr
  print("Unpackaging {}...".format(confStr))
  sourceDir = os.path.join("scan_packaged", fileName)
  targetDir = os.path.join("scan", fileName)
  implFolder, filesToCopy = files_to_copy(conf)
  try:
    os.makedirs(os.path.join(targetDir, implFolder))
  except FileExistsError:
    pass
  for path in filesToCopy:
    shutil.copy(os.path.join(sourceDir, path), os.path.join(targetDir, path))
  with open(os.path.join(targetDir, "Configure.sh"), "r") as inFile:
    confStr = inFile.read()
  with open(os.path.join(targetDir, "Configure.sh"), "w") as outFile:
    # Remove specific compiler paths
    fixed = re.sub(" -DCMAKE_C(XX)?_COMPILER=[^ ]*", "", confStr)
    outFile.write(fixed)
  run_build(conf, clean=False, hardware=False)

def unpackage_configurations():
  unpackagedSomething = False
  confs = []
  for fileName in os.listdir("scan_packaged"):
    conf = get_conf(fileName)
    if not conf:
      continue
    confs.append(conf)
    unpackagedSomething = True
  pool = mp.Pool(processes=len(confs))
  try:
    pool.map(unpackage_configuration, confs)
  except KeyboardInterrupt:
    pool.terminate()
  if unpackagedSomething:
    print("Successfully unpackaged kernels into \"scan\".")
  else:
    print("No kernels found in \"scan_packaged\".")

def benchmark(repetitions):
  for fileName in os.listdir("scan"):
    conf = get_conf(fileName)
    if not conf:
      continue
    confStr = conf_string(conf)
    folderName = "benchmark_" + confStr
    kernelFolder = os.path.join("scan", fileName)
    kernelPath = os.path.join(kernelFolder, "sdaccel_hw.xclbin")
    if not os.path.exists(kernelPath):
      continue
    with open(os.path.join(kernelFolder, "frequency.txt"), "r") as clockFile:
      realClock = clockFile.read()
    pattern = re.compile("(benchmark_[^_]+)_[0-9]+_([0-9]+_[0-9]+_[0-9]+)")
    benchmarkFolder = os.path.join("benchmarks",
                                   pattern.sub("\\1_{}_\\2".format(realClock),
                                   folderName))
    try:
      os.makedirs(benchmarkFolder)
    except FileExistsError:
      pass
    shutil.copy(os.path.join(kernelFolder, "Configure.sh"), benchmarkFolder)
    print("Running {}...".format(confStr))
    if run_process("make".split(), kernelFolder, pipe=False) != 0:
      raise Exception(confStr + ": software build failed.")
    for i in range(repetitions):
      if run_process("./ExecuteKernel no".split(),
                     kernelFolder, pipe=False) != 0:
        raise Exception(confStr + ": kernel execution failed.")
      shutil.copy(os.path.join(kernelFolder, "sdaccel_profile_summary.csv"),
                  os.path.join(benchmarkFolder,
                               str(datetime.datetime.now()).replace(" ", "_")
                               + ".csv"))

def print_usage():
  print("Usage: " +
        "\n  ./scan_configurations.py " +
        "<number of processes> " +
        "<data type...> " +
        "<target clock...> " +
        "<data width...> " +
        "<compute units...> " +
        "<additional CMake options...>" +
        "\n  ./scan_configurations.py extract" +
        "\n  ./scan_configurations.py package_kernels" +
        "\n  ./scan_configurations.py unpackage_kernels" +
        "\n  ./scan_configurations.py benchmark <number of repetitions...>",
        file=sys.stderr)

if __name__ == "__main__":

  if len(sys.argv) == 2:
    if sys.argv[1] == "extract":
      extract_to_file()
      sys.exit(0)
    elif sys.argv[1] == "package_kernels":
      package_configurations()
      sys.exit(0)
    elif sys.argv[1] == "unpackage_kernels":
      unpackage_configurations()
      sys.exit(0)
    else:
      raise "Unknown command \"{}\"/".format(sys.argv[1])

  if len(sys.argv) == 3 and sys.argv[1] == "benchmark":
    benchmark(int(sys.argv[2]))
    sys.exit(0)

  if len(sys.argv) < 6:
    print_usage()
    sys.exit(1)

  numProcs = int(sys.argv[1])
  types = sys.argv[2].split(",")
  targetClocks = [int(x) for x in sys.argv[3].split(",")]
  dataWidths = [int(x) for x in sys.argv[4].split(",")]
  computeUnits = [int(x) for x in sys.argv[5].split(",")]
  options = sys.argv[6:]
  configurations = [Configuration(*x, options=options) for x in itertools.product(
      types, targetClocks, computeUnits, dataWidths, [blocks])]
  scan_configurations(numProcs, configurations)
