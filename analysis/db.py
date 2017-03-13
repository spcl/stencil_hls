#!/usr/bin/env python3
from sqlalchemy import *
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import relationship
import datetime
import json
import os
import re
import sys

Base = declarative_base()

class Board(Base):

  __tablename__ = "board"

  name = Column(String, primary_key=True)
  ddrDimms = Column(Integer)
  ddrClock = Column(Float)
  dsp = Column(Integer)
  lut = Column(Integer)
  ff = Column(Integer)
  bram = Column(Integer)

  def __init__(self, name, ddrDimms, ddrClock, dsp, lut, ff, bram):
    self.name = name
    self.ddrDimms = ddrDimms
    self.ddrClock = ddrClock
    self.dsp = dsp
    self.lut = lut
    self.ff = ff
    self.bram = bram

  def __repr__(self):
    return ("{" + ",".join(map(str, [
                self.name, ddrDimms, ddrClock, dsp, lut, ff, bram]))
            + "}")

class Configuration(Base):

  __tablename__ = "configuration"

  id = Column(Integer, primary_key=True)
  boardName = Column(String, ForeignKey("board.name"))
  board = relationship("Board", backref="configurations",
                       cascade_backrefs=False)
  dataType = Column(String)
  targetClock = Column(Float)
  domainSize = Column(Integer)
  timesteps = Column(Integer)
  width = Column(Integer)
  depth = Column(Integer)
  blocks = Column(Integer)

  def __init__(self, boardName, dataType, targetClock, domainSize, timesteps,
               width, depth, blocks):
    self.boardName = boardName
    self.dataType = dataType
    self.targetClock = targetClock
    self.domainSize = domainSize
    self.timesteps = timesteps
    self.width = width
    self.depth = depth
    self.blocks = blocks

  def total_ops(self):
    return self.domainSize*self.domainSize*self.timesteps

  def __repr__(self):
    return ("{" + ",".join(map(str, [
                self.boardName, self.targetClock, self.domainSize,
                self.timesteps, self.width, self.depth, self.blocks]))
            + "}")

  def __eq__(self, other):
    if type(other) is type(self):
      return (self.board == other.board and
              self.targetClock == other.targetClock and
              self.domainSize == other.domainSize and
              self.timesteps == other.timesteps and
              self.width == other.width and
              self.depth == other.depth and
              self.blocks == other.blocks)

class Measurement(Base):

  __tablename__ = "measurement"

  id = Column(Integer, primary_key=True)
  timestamp = Column(DateTime)
  configurationId = Column(Integer, ForeignKey("configuration.id"))
  configuration = relationship("Configuration", backref="measurements",
                               cascade_backrefs=False)
  timeKernel = Column(Float)
  timeCreateProgram = Column(Float)
  timeCreateContext = Column(Float)
  totalDataTransferred = Column(Float)

  def __init__(self, timestamp, configuration, timeKernel, timeCreateProgram,
               timeCreateContext, totalDataTransferred):
    self.timestamp = timestamp
    self.configuration = configuration
    self.timeKernel = timeKernel
    self.timeCreateProgram = timeCreateProgram
    self.timeCreateContext = timeCreateContext
    self.totalDataTransferred = totalDataTransferred

  def __repr__(self):
    return ("{" + ",".join(map(str, [
                self.configuration, self.timestamp, self.timeKernel,
                self.timeCreateProgram, self.timeCreateContext,
                self.totalDataTransferred]))
            + "}")

def get_conf(boardName, folderName):
  m = re.search("(float|double)_[0-9]+_[0-9]+_[0-9]+_[0-9]+",
                folderName)
  if not m:
    return None
  with open(os.path.join(folderName, "Configure.sh"), "r") as confFile:
    confText = confFile.read();
    dataType = re.search("-DSTENCIL_DATA_TYPE=([^ ]+)", confText).group(1)
    targetClock = float(re.search(
        "-DSTENCIL_TARGET_CLOCK=([0-9\.]+)", confText).group(1))
    domainSize = int(re.search("-DSTENCIL_ROWS=([0-9]+)", confText).group(1))
    timesteps = int(re.search("-DSTENCIL_TIME=([0-9]+)", confText).group(1))
    width = int(re.search("-DSTENCIL_DATA_WIDTH=([0-9]+)", confText).group(1))
    depth = int(re.search("-DSTENCIL_DEPTH=([0-9]+)", confText).group(1))
    blocks = int(re.search("-DSTENCIL_BLOCKS=([0-9]+)", confText).group(1))
    return Configuration(boardName, dataType, targetClock, domainSize,
                         timesteps, width, depth, blocks)

if __name__ == "__main__":

  if len(sys.argv) != 3:
    print("Usage: ./db.py <board name> <benchmark folder>")
    sys.exit(1)
  boardName = sys.argv[1]
  benchmarkDir = sys.argv[2]

  from sqlalchemy import create_engine
  from sqlalchemy.orm import sessionmaker

  dbDir = os.path.dirname(os.path.realpath(__file__))
  dbPath = os.path.join(dbDir, "benchmarks.sqlite")
  engine = create_engine("sqlite:///" + dbPath)
  Session = sessionmaker()
  Session.configure(bind=engine)
  session = Session()

  Base.metadata.create_all(engine)

  adm_7v3 = Board("ADM-7V3", 1, 1333, 3600, 433200, 866400, 1470)
  tul_ku115 = Board("TUL-KU115", 4, 2133, 5520, 663360, 1326720, 2160)
  session.merge(adm_7v3)
  session.merge(tul_ku115)
  session.commit()

  for folderName in os.listdir(benchmarkDir):
    configDir = os.path.join(benchmarkDir, folderName)
    conf = get_conf(boardName, configDir)
    q = session.query(Configuration).filter(
        Configuration.boardName == conf.boardName,
        Configuration.dataType == conf.dataType,
        Configuration.targetClock == conf.targetClock,
        Configuration.domainSize == conf.domainSize,
        Configuration.timesteps == conf.timesteps,
        Configuration.width == conf.width,
        Configuration.depth == conf.depth,
        Configuration.blocks == conf.blocks)
    if q.count() == 0:
      session.add(conf)
    else:
      conf = q.one()
    for fileName in os.listdir(configDir):
      if not fileName.endswith(".csv"):
        continue
      timestamp = datetime.datetime.strptime(
          fileName[:-4],
          "%Y-%m-%d_%H:%M:%S.%f")
      if session.query(Measurement).filter(
          Measurement.timestamp == timestamp,
          Measurement.configuration == conf.id).count() > 0:
        continue
      with open(os.path.join(configDir, fileName), "r") as fileHandle:
        fileText = fileHandle.read()
        timeCreateProgram = float(re.search(
            "clCreateProgramWithBinary,[^,]*,([^,]+)", fileText).group(1))
        timeCreateContext = float(re.search(
            "clCreateContext,[^,]*,([^,]+)", fileText).group(1))
        timeKernel = float(re.search(
            "Total Time \(ms\).+\nKernel,[^,]*,([^,]+)", fileText).group(1))
        totalDataTransferred = float(re.search(
            "Total Data Transfer \(MB\).+\n[^,]*,[^,]*,[^,]*,[^,]*,[^,]*,([^,]+)",
            fileText).group(1))
        measurement = Measurement(timestamp, conf.id, timeKernel,
                                  timeCreateProgram, timeCreateContext,
                                  totalDataTransferred)
        session.add(measurement)
    session.commit()

