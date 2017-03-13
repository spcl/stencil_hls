#!/usr/bin/env python3
from db import *
import matplotlib.pyplot as plt
import matplotlib.lines as mlines
import numpy as np
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker
from sqlalchemy.orm.exc import NoResultFound

if __name__ == "__main__":

  if len(sys.argv) != 2:
    print("Usage: ./plot.py <board name>")
    sys.exit(1)
  boardName = sys.argv[1]

  dbDir = os.path.dirname(os.path.realpath(__file__))
  dbPath = os.path.join(dbDir, "benchmarks.sqlite")
  engine = create_engine("sqlite:///" + dbPath)
  Session = sessionmaker()
  Session.configure(bind=engine)
  session = Session()

  widths = np.array(sorted(session.query(
      Configuration.width).distinct().all())).flatten()
  ticks = dict(zip(widths, range(1, len(widths)+1)))

  fig, ax = plt.subplots()
  ax.set_xlabel("Data width")
  ax.set_ylabel("operand/s")
  ax_flops = ax.twinx()
  ax_flops.set_ylabel("GOp/s")

  try:
    board = session.query(Board).filter(Board.name == boardName).one()
  except NoResultFound:
    print("Invalid board name: \"{}\".".format(boardName))
    sys.exit(1)

  handles = []
  types = ["float", "double"]
  colors = ["darkgreen", "navy"]
  for (dataType, color) in zip(types, colors):
    confs = []
    for width in session.query(Configuration.width).filter(
        Configuration.board == board,
        Configuration.dataType == dataType).all():
      width = width[0]
      maxCompute = 0
      for conf in session.query(Configuration).filter(
          Configuration.board == board,
          Configuration.dataType == dataType,
          Configuration.width == width).all():
        compute = conf.width * conf.depth
        if compute >= maxCompute:
          maxConf = conf
          maxCompute = compute
      confs.append(maxConf)
    confs = sorted(confs, key=lambda c: c.width)
    if len(confs) > 0:
      x = []
      y = []
      for conf in confs:
        time = np.array(session.query(Measurement.timeKernel).filter(
            Measurement.configurationId == conf.id).all())
        x.append(ticks[conf.width])
        y.append(1e-9*conf.total_ops() / time)
      bp = ax.boxplot(y, positions=x, notch=True)
      for l in bp["whiskers"]:
        l.set(color=color, lw=2, linestyle="--", dashes=(2, 2))
      for l in bp["caps"]:
        l.set(color=color, lw=2)
      for l in bp["medians"]:
        l.set(lw=2)
      for l in bp["fliers"]:
        l.set(color=color, lw=2)
      for l in bp["boxes"]:
        l.set(color=color, lw=2)
      handles.append(mlines.Line2D([], [], color=color, lw=2))

  plt.xticks(list(ticks.values()), list(ticks.keys()))
  ax.set_xlim((0.5, len(widths)+0.5))
  ax.legend(handles, types, loc=2)
  ax_flops.set_ylim((ax.get_ylim()[0]*4, ax.get_ylim()[1]*4))
  ax_flops.set_yticks(4*ax.get_yticks())
  fig.show()
  input("Press return to exit...")
