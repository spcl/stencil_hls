#!/usr/bin/env python
import matplotlib.pyplot as plt
import numpy as np
import sys
from opt_db import *
from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

linestyles = [
    {"linestyle": "-", "lw": 2, "color": "firebrick"},
    {"dashes": [3, 3], "lw": 2, "color": "navy"},
    {"dashes": [3, 3, 11, 3, 11, 3], "lw": 2, "color": "forestgreen"},
]
legendArgs = {"loc": 3, "bbox_to_anchor": (-0.01, 1, 1.02, 0),
              "ncol": 2, "mode": "expand", "fontsize": 11}

def aos_to_soa(aos):
  soa = {"bandwidth": np.empty(len(aos)),
         "compute": np.empty(len(aos)),
         "bram": np.empty(len(aos)),
         "perf": np.empty(len(aos))}
  for i, elem in enumerate(aos):
    soa["bandwidth"][i] = elem.bandwidth
    soa["compute"][i] = elem.compute
    soa["bram"][i] = elem.bram
    soa["perf"][i] = elem.performance()
  return soa

def plot_compute(ax, session):
  boards = session.query(Board).all()
  xlim = [float("inf"), -float("inf")]
  for board, linestyle in zip(boards, linestyles):
    data = aos_to_soa(session.query(Optimized).filter(
        Optimized.bram == board.bram, Optimized.bandwidth == board.bandwidth,
        Optimized.frequency == board.frequency).filter(
            Optimized.compute <= 1200).all())
    ax.plot(data["compute"], 1e-3*data["perf"], **linestyle,
            label="{:.0f} MHz, {} GB/s, {} RAM".format(
                board.frequency, board.bandwidth, board.bram))
    ax.plot([board.compute],
            1e-3*[data["perf"][data["compute"] == board.compute]][0],
            "x", markeredgewidth=3, markersize=10, color=linestyle["color"])
    xlim[0] = min(data["compute"][0], xlim[0])
    xlim[1] = max(data["compute"][-1], xlim[0])
  ax.plot([], [], "x", markeredgewidth=3, markersize=10, color="black",
          label="Actual configuration")
  ax.legend(**legendArgs)
  ax.set_xlabel("Compute units")
  ax.set_ylabel("$F$ [GStencil/s]")
  ax.set_xlim(xlim)

def plot_bram(ax, session):
  boards = session.query(Board).all()
  xlim = [float("inf"), -float("inf")]
  for board, linestyle in zip(boards, linestyles):
    data = aos_to_soa(session.query(Optimized).filter(
        Optimized.compute == board.compute,
        Optimized.bandwidth == board.bandwidth,
        Optimized.frequency == board.frequency).all())
    ax.plot(data["bram"], 1e-3*data["perf"], **linestyle,
            label="{:.0f} MHz, {} GB/s, {} C".format(
                board.frequency, board.bandwidth, board.compute))
    ax.plot([board.bram],
            1e-3*[data["perf"][data["bram"] == board.bram]][0],
            "x", markeredgewidth=3, markersize=10, color=linestyle["color"])
    xlim[0] = min(data["bram"][0], xlim[0])
    xlim[1] = max(data["bram"][-1], xlim[0])
  ax.plot([], [], "x", markeredgewidth=3, markersize=10, color="black",
          label="Actual configuration")
  ax.legend(**legendArgs)
  ax.set_xlabel("RAM units")
  ax.set_ylabel("$F$ [GStencil/s]")
  ax.set_xlim((xlim[0] - 10, xlim[1] + 10))

def plot_bandwidth(ax, session):
  boards = session.query(Board).all()
  xlim = [float("inf"), -float("inf")]
  for board, linestyle in zip(boards, linestyles):
    data = aos_to_soa(session.query(Optimized).filter(
        Optimized.compute == board.compute,
        Optimized.bram == board.bram,
        Optimized.frequency == board.frequency).all())
    ax.plot(data["bandwidth"], 1e-3*data["perf"], **linestyle,
            label="{} MHz, {} C, {} RAM".format(
                int(board.frequency), board.compute, board.bram))
    ax.plot([board.bandwidth],
            1e-3*[data["perf"][data["bandwidth"] == board.bandwidth]][0],
            "x", markeredgewidth=3, markersize=10, color=linestyle["color"])
    xlim[0] = min(data["bandwidth"][0], xlim[0])
    xlim[1] = max(data["bandwidth"][-1], xlim[0])
  ax.plot([], [], "x", markeredgewidth=3, markersize=10, color="black",
          label="Actual configuration")
  ax.legend(**legendArgs)
  ax.set_xlabel("$B$ [GB/s]")
  ax.set_ylabel("$F$ [GStencil/s]")
  ax.set_xlim((xlim[0] - 10, xlim[1] + 10))

if __name__ == "__main__":
  if not ((len(sys.argv) == 2 and sys.argv[1] == "show") or
          (len(sys.argv) == 3 and sys.argv[1] == "save")):
    print("Usage:\n  ./plot_scaling.py show\n  ./plot_scaling.py save <path>")
    sys.exit(1)

  engine = create_engine('sqlite:///opt.sqlite')
  Session = sessionmaker()
  Session.configure(bind=engine)
  session = Session()

  plt.rcParams.update({'font.size': 15})

  fig, axs = plt.subplots(3, 1, figsize=(8, 9))
  plot_compute(axs[0], session)
  plot_bram(axs[1], session)
  plot_bandwidth(axs[2], session)
  fig.subplots_adjust(hspace=0.7)

  if sys.argv[1] == "show":
    plt.show()
  elif sys.argv[1] == "save":
    fig.savefig(sys.argv[2], bbox_inches="tight")
  else:
    raise RuntimeError("Unknown mode \"{}\".".format(sys.argv[1]))

