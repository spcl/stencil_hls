#!/usr/bin/env python3
import os, sys
from sqlalchemy import Column, Integer, Float, String, UniqueConstraint
from sqlalchemy.ext.declarative import declarative_base
from scaling import bram_requirement

Base = declarative_base()

class Board(Base):

  __tablename__ = "board"

  name = Column(String, primary_key=True)
  frequency = Column(Float)
  bandwidth = Column(Float)
  compute = Column(Integer)
  bram = Column(Integer)

  def __repr__(self):
    return ("{" + ",".join(map(str, [
                self.name, self.frequency, self.bandwidth, self.compute,
                self.bram]))
            + "}")

class Optimized(Base):

  __tablename__ = "optimized"

  frequency = Column(Float, primary_key=True)
  bandwidth = Column(Float, primary_key=True)
  compute = Column(Integer, primary_key=True)
  bram = Column(Integer, primary_key=True)
  bramDepth = Column(Integer, primary_key=True)

  frequencyOpt = Column(Float)
  depthOpt = Column(Integer)
  dataWidthOpt = Column(Integer)
  tileSizeOpt = Column(Integer)

  def __init__(self, frequency, bandwidth, compute, bram, bramDepth):
    self.frequency = frequency
    self.bandwidth = bandwidth
    self.compute = compute
    self.bram = bram
    self.bramDepth = bramDepth

  def efficiency(self):
    return self.tileSizeOpt / (self.tileSizeOpt + 2 * self.depthOpt)

  def performance(self):
    return self.frequencyOpt * self.depthOpt * self.dataWidthOpt * self.efficiency()

  def bram_requirement(self):
    return bram_requirement(self.dataWidthOpt, self.depthOpt, self.tileSizeOpt,
                            self.bramDepth)

  def add_optimized(self, frequencyOpt, depthOpt, dataWidthOpt, tileSizeOpt):
    self.frequencyOpt = frequencyOpt
    self.depthOpt = depthOpt
    self.dataWidthOpt = dataWidthOpt
    self.tileSizeOpt = tileSizeOpt

  def __repr__(self):
    return ("{" + ",".join(map(str, [
                self.compute, self.bram, self.bramDepth, self.bandwidth,
                self.frequency, self.frequencyOpt, self.depthOpt,
                self.dataWidthOpt, self.tileSizeOpt]))
            + "}")

if __name__ == "__main__":
  from sqlalchemy import create_engine
  from sqlalchemy.orm import sessionmaker
  engine = create_engine("sqlite:///opt.sqlite")
  Session = sessionmaker()
  Session.configure(bind=engine)
  Base.metadata.create_all(engine)
  session = Session()
  boards = [
    Board(name="7v3", frequency=200.0, bandwidth=10.66, compute=450,
          bram=1470),
    # Board(name="vu115p", frequency=250.0, bandwidth=19.2, compute=1488,
    #       bram=2688),
    # Board(name="stratix10", frequency=800.0, bandwidth=256.0, compute=1440,
    #       bram=11271),
    Board(name="vu37p", frequency=200.0, bandwidth=120, compute=1215,
          bram=4033),
    Board(name="ku115", frequency=200.0, bandwidth=17.06, compute=690,
          bram=2160),
  ]
  for board in boards:
    session.merge(board)
  session.commit()

