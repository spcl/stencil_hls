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

def conf_string(conf):
  return (conf.target + "_" + conf.dtype + "_clk" + str(conf.targetClock) + "_w" +
          str(conf.width) + "_c" + str(conf.compute) + "_" +
          str(conf.dim) + "x" + str(conf.dim) + "_b" + str(conf.blocks) + "_t" +
          str(conf.timeFactor))

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
        self.status, str(self.conf), self.clock,
        self.dsp, self.lut, self.ff, self.bram, self.power]))

class Configuration(object):
  def __init__(self, target, dtype, targetClock, width, compute, dim, blocks,
               timeFactor, options=None):
    self.target = target
    self.dtype = dtype
    self.targetClock = targetClock
    self.width = width
    self.compute = compute
    self.dim = dim
    self.blocks = blocks
    self.timeFactor = timeFactor
    self.options = options
    self.consumption = None
  def csv_cols():
    return "target,dtype,targetClock,width,compute,dim,blocks,timeFactor"
  def __repr__(self):
    return ",".join(map(str, [
        self.target, self.dtype, self.targetClock, self.width, self.compute,
        self.dim, self.blocks, self.timeFactor]))

def cmake_command(conf, options=""):
  if conf.compute % conf.width != 0:
    raise RuntimeError(
        "Invalid configuration: compute {} not divisable by width {}.".format(
            conf.compute, conf.width))
  depth = int(conf.compute / conf.width)
  time = int(conf.timeFactor * conf.compute)
  return ("cmake ../../ " + " ".join(options) +
          " -DSTENCIL_KEEP_INTERMEDIATE=ON" +
          " -DSTENCIL_TARGET={}".format(conf.target) +
          " -DSTENCIL_DATA_TYPE={}".format(conf.dtype) +
          " -DSTENCIL_TARGET_CLOCK={}".format(int(conf.targetClock)) +
          " -DSTENCIL_TARGET_TIMING={}".format(1000/conf.targetClock) +
          " -DSTENCIL_KERNEL_WIDTH={}".format(conf.width) +
          " -DSTENCIL_MEMORY_WIDTH={}".format(16) +
          " -DSTENCIL_DEPTH={}".format(depth) +
          " -DSTENCIL_ROWS={}".format(conf.dim) +
          " -DSTENCIL_COLS={}".format(conf.dim) +
          " -DSTENCIL_BLOCKS={}".format(conf.blocks) +
          " -DSTENCIL_TIME={}".format(time))

def create_builds(conf):
  cmakeCommand = cmake_command(conf, options=conf.options)
  confStr = conf_string(conf)
  confDir = os.path.join("scan", "build_" + confStr)
  try:
    os.makedirs(confDir)
  except:
    pass
  print_status(conf, "configuring...")
  with open(os.path.join(confDir, "Configure.sh"), "w") as confFile:
    confFile.write("#!/bin/sh\n{}".format(cmakeCommand))
  run_build(conf, clean=True, hardware=True)

def time_only(t):
  if isinstance(t, datetime.datetime):
    return t.strftime("%H:%M:%S")
  else:
    totalSeconds = int(t.total_seconds())
    hours, rem = divmod(totalSeconds, 3600)
    minutes, seconds = divmod(rem, 60)
    minutes = "0" + str(minutes) if minutes < 10 else str(minutes)
    seconds = "0" + str(seconds) if seconds < 10 else str(seconds)
    return "{}:{}:{}".format(hours, minutes, seconds)

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
    timeStart = datetime.datetime.now()
    print_status(conf, "running HLS...")
    if run_process("make synthesis".split(), confDir) != 0:
      raise Exception(confStr + ": HLS failed.")
    print_status(conf, "finished HLS after {}.".format(
        time_only(datetime.datetime.now() - timeStart)))
    timeStart = datetime.datetime.now()
    print_status(conf, "starting kernel build...")
    if run_process("make kernel".split(), confDir) != 0:
      try:
        with open(os.path.join(confDir, "log.out")) as logFile:
          m = re.search("auto frequency scaling failed", logFile.read())
          if not m:
            print_status(conf, "FAILED after {}.".format(
                time_only(datetime.datetime.now() - timeStart)))
          else:
            print(conf, "TIMING failed after {}.".format(
                time_only(datetime.datetime.now() - timeStart)))
      except FileNotFoundError:
        print_status(conf, "FAILED after {}.".format(
            time_only(datetime.datetime.now() - timeStart)))
    else:
      print_status(conf, "SUCCESS in {}.".format(
          time_only(datetime.datetime.now() - timeStart)))
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
  kernelString = kernel_string(conf)
  xoccFolder = "_xocc_Stencil_" + kernelString + ".dir"
  if not os.path.exists(os.path.join(buildFolder, xoccFolder)):
    conf.consumption = Consumption(conf, "no_intermediate", None, None, None,
                                   None, None, None)
    return
  kernelFolder = os.path.join(
      buildFolder, xoccFolder,
      "impl", "build", "system", kernelString, "bitstream")
  implFolder = os.path.join(
      kernelFolder,
      kernelString + "_ipi", "ipiimpl", "ipiimpl.runs", "impl_1")
  if not os.path.exists(implFolder):
    conf.consumption = Consumption(conf, "no_build", None, None, None, None,
                                   None, None)
    return
  status = check_build_status(conf)
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
    with open(os.path.join(kernelFolder, kernelString + "_ipi",
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

def check_build_status(conf):
  buildFolder = os.path.join("scan", "build_" + conf_string(conf))
  kernelString = kernel_string(conf)
  kernelFolder = os.path.join(
      buildFolder, "_xocc_Stencil_" + kernelString + ".dir",
      "impl", "build", "system", kernelString, "bitstream")
  try:
    log = open(
        os.path.join(buildFolder, "log.out"), "r").read()
  except:
    return "no_build"
  try:
    report = open(
        os.path.join(kernelFolder, kernelString + "_ipi", "vivado.log")).read()
  except:
    return "no_build"
  m = re.search("Implementation Feasibility check failed", log)
  if m:
    return "failed_feasibility"
  m = re.search("Detail Placement failed", log)
  if m:
    return "failed_placement"
  m = re.search("Placer could not place all instances", report)
  if m:
    return "failed_placement"
  m = re.search("Routing results verification failed due to partially-conflicted nets", report)
  if m:
    return "failed_routing"
  m = re.search("Internal Data Exception", log)
  if m:
    return "crashed"
  m = re.search("auto frequency scaling failed", report)
  if m:
    return "failed_timing"
  m = re.search("Unable to write message .+ as it exceeds maximum size", report)
  if m:
    return "failed_report"
  for fileName in os.listdir(kernelFolder):
    if len(fileName) >= 7 and fileName[-7:] == ".xclbin":
      return "success"
  return "failed_unknown"

def get_conf(folderName):
  m = re.search("([^_]+)_([^_]+)_clk([1-9][0-9]*)_w([1-9][0-9]*)_c([1-9][0-9]*)_" +
                "([1-9][0-9]*)x[1-9][0-9]*_b([1-9][0-9]*)_t([1-9][0-9]*)",
                folderName)
  if not m:
    return None
  return Configuration(m.group(1), m.group(2), int(m.group(3)),
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

def kernel_string(conf):
  return "jacobi_{}_{}_c{}_w{}_d{}_{}x{}_b{}_t{}".format(
      conf.target, conf.dtype, int(conf.targetClock), conf.width,
      int(conf.compute / conf.width), conf.dim, conf.dim, conf.blocks,
      conf.timeFactor * conf.compute)

def files_to_copy(conf):
  filesToCopy = ["Configure.sh", "frequency.txt",
                 kernel_string(conf) + ".xclbin"]
  kernelString = kernel_string(conf)
  xoccFolder = "_xocc_Stencil_" + kernel_string(conf) + ".dir"
  kernelFolder = os.path.join(
      xoccFolder, "impl", "build",
      "system", kernelString, "bitstream", kernelString + "_ipi")
  filesToCopy.append(os.path.join(kernelFolder, "vivado.log"))
  filesToCopy.append(os.path.join(kernelFolder, "vivado_warning.txt"))
  implFolder = os.path.join(
      kernelFolder, "ipiimpl", "ipiimpl.runs", "impl_1")
  filesToCopy.append(os.path.join(
      implFolder, "xcl_design_wrapper_utilization_placed.rpt"))
  filesToCopy.append(os.path.join(
      implFolder, "xcl_design_wrapper_power_routed.rpt"))
  return implFolder, filesToCopy

def package_configurations(target):
  packagedSomething = False
  for fileName in os.listdir("scan"):
    conf = get_conf(fileName)
    if not conf:
      continue
    if conf.target != target:
      continue
    sourceDir = os.path.join("scan", fileName)
    packageFolder = os.path.join(target, fileName)
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
    for path in filesToCopy:
      try:
        shutil.copy(os.path.join(sourceDir, path),
                    os.path.join(packageFolder, path))
      except FileNotFoundError as err:
        if path.endswith("vivado_warning.txt"):
          with open(os.path.join(packageFolder, path), "w") as outFile:
            pass
        else:
          raise err
    packagedSomething = True
  if packagedSomething:
    print(("Successfully packaged kernels and configuration " +
           "files into \"{}\".").format(target))
  else:
    print("No kernels for target \"{}\" found in \"scan\".".format(target))

def unpackage_configuration(conf):
  confStr = conf_string(conf)
  fileName = "build_" + confStr
  print("Unpackaging {}...".format(confStr))
  sourceDir = os.path.join(conf.target, fileName)
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

def unpackage_configurations(target):
  unpackagedSomething = False
  confs = []
  for fileName in os.listdir(target):
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
    print("No kernels found in \"{}\".".format(target))

def benchmark(repetitions):
  for fileName in os.listdir("scan"):
    conf = get_conf(fileName)
    if not conf:
      continue
    confStr = conf_string(conf)
    folderName = "benchmark_" + confStr
    kernelFolder = os.path.join("scan", fileName)
    kernelString = kernel_string(conf)
    kernelPath = os.path.join(kernelFolder, kernelString + ".xclbin")
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
      if run_process("./ExecuteKernel.exe off".split(),
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
        "<target board,...> " +
        "<data type,...> " +
        "<target clock,...> " +
        "<data width,...> " +
        "<compute,...> " +
        "<grid size,...> " +
        "<blocks,...> " +
        "<time factor,...> " +
        "[<additional CMake options...>]" +
        "\n  ./scan_configurations.py extract" +
        "\n  ./scan_configurations.py package_kernels <target>" +
        "\n  ./scan_configurations.py unpackage_kernels" +
        "\n  ./scan_configurations.py benchmark <number of repetitions...>",
        file=sys.stderr)

if __name__ == "__main__":

  if len(sys.argv) < 2:
    print_usage()
    sys.exit(1)

  if sys.argv[1] == "extract":
    if len(sys.argv) != 2:
      print_usage()
      sys.exit(1)
    extract_to_file()
    sys.exit(0)

  if sys.argv[1] == "unpackage_kernels":
    if len(sys.argv) != 3:
      print_usage()
      sys.exit(1)
    target = sys.argv[2]
    unpackage_configurations(target)
    sys.exit(0)

  if sys.argv[1] == "benchmark":
    if len(sys.argv) != 3:
      print_usage()
      sys.exit(1)
    benchmark(int(sys.argv[2]))
    sys.exit(0)

  if sys.argv[1] == "package_kernels":
    if len(sys.argv) != 3:
      print_usage()
      sys.exit(1)
    target = sys.argv[2]
    package_configurations(sys.argv[2])
    sys.exit(0)

  if len(sys.argv) < 10:
    print_usage()
    sys.exit(1)

  numProcs = int(sys.argv[1])
  targets = sys.argv[2].split(",")
  types = sys.argv[3].split(",")
  targetClocks = [int(x) for x in sys.argv[4].split(",")]
  widths = [int(x) for x in sys.argv[5].split(",")]
  compute = [int(x) for x in sys.argv[6].split(",")]
  dims = [int(x) for x in sys.argv[7].split(",")]
  blocks = [int(x) for x in sys.argv[8].split(",")]
  timeFactors = [int(x) for x in sys.argv[9].split(",")]
  options = sys.argv[10:]
  configurations = [Configuration(*x, options=options) for x in itertools.product(
      targets, types, targetClocks, widths, compute, dims, blocks, timeFactors)]
  scan_configurations(numProcs, configurations)
