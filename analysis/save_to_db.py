#!/usr/bin/env python
import sys
from opt_db import Optimized
from scaling import DataPoint

def save_optimized(optimized):

  from sqlalchemy import create_engine
  from sqlalchemy.orm import sessionmaker
  engine = create_engine('sqlite:///opt.sqlite')
  Session = sessionmaker()
  Session.configure(bind=engine)
  session = Session()

  for opt in optimized:
    session.merge(opt)

  session.commit()

def load_from_file(path):

  from sqlalchemy import create_engine
  from sqlalchemy.orm import sessionmaker
  engine = create_engine('sqlite:///opt.sqlite')
  Session = sessionmaker()
  Session.configure(bind=engine)
  session = Session()

  with open(path, "r") as inFile:
    inFile.readline()
    for line in inFile:
      dp = DataPoint.from_string(line)
      opt = Optimized(frequency=dp.frequency,
                      bandwidth=dp.bandwidth,
                      compute=dp.compute,
                      bram=dp.bram,
                      bramDepth=dp.bramDepth,
                      frequencyOpt=dp.frequencyOpt,
                      depthOpt=dp.depthOpt,
                      dataWidthOpt=dp.dataWidthOpt,
                      tileSizeOpt=dp.tileSizeOpt)
      session.add(opt)

  session.commit()

if __name__ == "__main__":

  if len(sys.argv) != 2:
    print("Usage: ./save_to_db.py <path to csv file>")
    sys.exit(1)

  load_from_file(sys.argv[1])

