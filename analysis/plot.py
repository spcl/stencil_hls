#!/usr/bin/env python3
from db import *
import matplotlib.pyplot as plt
import matplotlib.lines as mlines
import numpy as np
from optimize import optimize
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker
from sqlalchemy.orm.exc import NoResultFound

if __name__ == "__main__":

  if len(sys.argv) > 2:
    print("Usage: ./plot.py [<path>]")
    sys.exit(1)

  boardName = "TUL-KU115"
  maxCompute = 328

  plt.rcParams.update({'font.size': 15})

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
  handlesOverhead = []
  optHandles = []
  types = ["float"]
  colors = ["darkgreen"]
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
      width = []
      perf = []
      perfOverhead = []
      for i, conf in enumerate(confs):
        time = np.array(session.query(Measurement.timeKernel).filter(
            Measurement.configurationId == conf.id).all()).flatten()/1000
        timeProgram = np.array(session.query(
            Measurement.timeCreateProgram).filter(
                Measurement.configurationId == conf.id).all()).flatten()/1000
        timeContext = np.array(session.query(
            Measurement.timeCreateContext).filter(
                Measurement.configurationId == conf.id).all()).flatten()/1000
        timeOverhead = time + timeProgram + timeContext
        width.append(ticks[conf.width])
        perf.append(1e-9*conf.total_ops() / time)
        perfOverhead.append(1e-9*conf.total_ops() / timeOverhead)
        perfMax = optimize(
            [conf.targetClock],
            [1e-3 * conf.width * conf.targetClock * (
                4 if dataType == "float" else 8)],
            [maxCompute],
            [board.bram],
            512)[0]
        peak = 1e-3*perfMax.performance()
        optHandle = ax.plot([ticks[conf.width]-0.1, ticks[conf.width]+0.1],
                            [peak, peak], lw=2,
                            color=color, linestyle="--")
      plot0 = ax.plot(width, [np.median(p) for p in perf], "+", color=color,
                      markersize=13, markeredgewidth=2)
      plot1 = ax.plot(width, [np.median(p) for p in perfOverhead], "o",
                      color=color, markersize=10, markeredgewidth=2,
                      fillstyle="none")
      # bp0 = ax.boxplot(perf, positions=width, notch=True)
      # bp1 = ax.boxplot(perfOverhead, positions=width, notch=True)
      optHandles.append(optHandle[0])
      # for bp in [bp0, bp1]:
      #   for l in bp["whiskers"]:
      #     l.set(color=color, lw=2, linestyle="--", dashes=(2, 2))
      #   for l in bp["caps"]:
      #     l.set(color=color, lw=2)
      #   for l in bp["medians"]:
      #     l.set(lw=2)
      #   for l in bp["fliers"]:
      #     l.set(color=color, lw=2)
      #   for l in bp["boxes"]:
      #     l.set(color=color, lw=2)
      handles.append(mlines.Line2D([], [], color=color, marker="+",
                     linestyle="none", markersize=13, markeredgewidth=2))
      handlesOverhead.append(mlines.Line2D([], [], color=color, marker="o",
                             linestyle="none", markersize=10, fillstyle="none"))

  plt.xticks(list(ticks.values()), list(ticks.keys()))
  ax.set_xlim((0.5, len(widths)+0.5))
  ax.legend(handles + handlesOverhead + optHandles,
            types + [t + " with overhead" for t in types] +
            [t + " optimized" for t in types], loc=2,
            fontsize=13)
  ax_flops.set_ylim((ax.get_ylim()[0]*4, ax.get_ylim()[1]*4))
  ax_flops.set_yticks(4*ax.get_yticks())
  if len(sys.argv) >= 2:
    fig.savefig(sys.argv[1], bbox_inches="tight")
  else:
    plt.show()
