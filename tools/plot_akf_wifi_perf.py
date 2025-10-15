#!/usr/bin/env python3
"""
plot_akf_wifi_perf.py — Load CSV from Wi-Fi AKF test and compute/plot stats.
Outputs:
  - akf_wifi_offset.png : offset vs time
  - akf_wifi_drift.png  : drift(ppb) vs time
  - akf_wifi_hist.png   : offset histogram with mean/median/std
  - akf_wifi_allan.png  : Allan deviation vs tau (log-log)
  - akf_wifi_adapt.png  : adaptation parameters (R_adapt, Q00, Q11) vs time
  - stats.json         : summary statistics
"""
import argparse, json, math, csv, os
from typing import List, Tuple
import numpy as np
import matplotlib.pyplot as plt

def load_csv(path: str):
    t, off, drift, z_meas, had, R_adapt, Q00, Q11 = [], [], [], [], [], [], [], []
    with open(path, 'r') as f:
        r = csv.DictReader(f)
        for row in r:
            t.append(float(row['t_s']))
            off.append(float(row['offset_s']))
            drift.append(float(row['drift_ppb']))
            z_meas.append(float(row['z_meas_s']))
            had.append(int(row['had_meas']))
            R_adapt.append(float(row['R_adapt']))
            Q00.append(float(row['Q00']))
            Q11.append(float(row['Q11']))
    return (np.array(t), np.array(off), np.array(drift), np.array(z_meas), 
            np.array(had), np.array(R_adapt), np.array(Q00), np.array(Q11))

def robust_stats(x: np.ndarray, warmup_s: float, t: np.ndarray):
    mask = t >= (t[0] + warmup_s)
    xs = x[mask]
    stats = {
        "count": int(xs.size),
        "mean": float(np.mean(xs)),
        "median": float(np.median(xs)),
        "std": float(np.std(xs, ddof=1)) if xs.size>1 else 0.0,
        "p95": float(np.percentile(xs,95)) if xs.size else 0.0,
        "p99": float(np.percentile(xs,99)) if xs.size else 0.0,
    }
    return stats, mask

def allan_deviation_from_offset(t: np.ndarray, off: np.ndarray, taus: np.ndarray):
    """
    Compute overlapping Allan deviation from phase (offset) data.
    off is time error x(t) in seconds. Frequency y(t)=dx/dt.
    For uniformly sampled data with sampling period tau0, the Allan variance for m=tau/tau0 is:
      sigma^2 = 0.5 * < (y_{k+m} - y_k)^2 >
    We approximate y_k by first difference of x: y_k = (x_{k+1}-x_k)/tau0.
    Here we allow non-uniform sampling by resampling to uniform grid.
    """
    # Resample to uniform grid
    t0, t1 = t[0], t[-1]
    N = len(t)
    tau0 = np.median(np.diff(t))
    grid = np.arange(t0, t1, tau0)
    xg = np.interp(grid, t, off)
    y = np.diff(xg) / tau0  # fractional frequency (s/s)
    out_tau, adev = [], []
    for tau in taus:
        if tau < 2*tau0: continue
        m = int(round(tau / tau0))
        if m < 1 or (len(y) - m) <= 1: continue
        dy = y[m:] - y[:-m]
        sigma2 = 0.5 * np.mean(dy**2)
        out_tau.append(m*tau0)
        adev.append(math.sqrt(sigma2))
    return np.array(out_tau), np.array(adev)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", help="CSV produced by AKF Wi-Fi test")
    ap.add_argument("--warmup", type=float, default=2.0, help="Seconds to discard at start for steady-state stats")
    ap.add_argument("--prefix", default="akf_wifi_", help="Output file prefix")
    args = ap.parse_args()

    # Determine output directory from CSV path
    csv_dir = os.path.dirname(args.csv)
    if csv_dir:
        output_prefix = os.path.join(csv_dir, args.prefix)
    else:
        output_prefix = args.prefix

    t, off, drift, z_meas, had, R_adapt, Q00, Q11 = load_csv(args.csv)
    stats, mask = robust_stats(off, args.warmup, t)
    print("Steady-state stats (offset, seconds):", json.dumps(stats, indent=2))

    # Save stats
    with open(output_prefix + "stats.json", "w") as f:
        json.dump({"offset": stats}, f, indent=2)

    # Plots
    plt.figure()
    plt.plot(t, np.array(off)*1e6, linewidth=1.0)
    plt.xlabel("Time (s)"); plt.ylabel("Offset (µs)"); plt.title("AKF Offset vs Time")
    plt.grid(True, which='both')
    plt.savefig(output_prefix + "offset.png", dpi=160, bbox_inches="tight"); plt.close()

    plt.figure()
    plt.plot(t, drift, linewidth=1.0)
    plt.xlabel("Time (s)"); plt.ylabel("Drift estimate (ppb)"); plt.title("AKF Drift vs Time")
    plt.grid(True, which='both')
    plt.savefig(output_prefix + "drift.png", dpi=160, bbox_inches="tight"); plt.close()

    plt.figure()
    plt.hist(off[mask]*1e6, bins=80, density=True)
    mu = stats["mean"]*1e6; med = stats["median"]*1e6; sd = stats["std"]*1e6
    plt.axvline(mu, linestyle='--', label=f"mean={mu:.2f} µs")
    plt.axvline(med, linestyle=':', label=f"median={med:.2f} µs")
    plt.xlabel("Offset (µs)"); plt.ylabel("PDF"); plt.title("AKF Offset histogram (steady-state)")
    plt.legend()
    plt.grid(True, which='both')
    plt.savefig(output_prefix + "hist.png", dpi=160, bbox_inches="tight"); plt.close()

    # Allan deviation
    taus = np.geomspace(2*np.median(np.diff(t)), (t[-1]-t[0])/4, num=25)
    tau_out, adev = allan_deviation_from_offset(t, off, taus)
    if len(adev):
        plt.figure()
        plt.loglog(tau_out, adev)
        plt.xlabel("Tau (s)"); plt.ylabel("Allan deviation (fractional)"); plt.title("AKF Allan deviation")
        plt.grid(True, which='both')
        plt.savefig(output_prefix + "allan.png", dpi=160, bbox_inches="tight"); plt.close()

    # Adaptation parameters plot (unique to AKF)
    plt.figure(figsize=(12, 8))
    
    plt.subplot(3, 1, 1)
    plt.plot(t, R_adapt, linewidth=1.0, color='red')
    plt.xlabel("Time (s)"); plt.ylabel("R_adapt"); plt.title("AKF Measurement Noise Adaptation")
    plt.grid(True, which='both')
    
    plt.subplot(3, 1, 2)
    plt.plot(t, Q00, linewidth=1.0, color='blue')
    plt.xlabel("Time (s)"); plt.ylabel("Q00 (offset)"); plt.title("AKF Process Noise Q00 (Offset)")
    plt.grid(True, which='both')
    plt.yscale('log')
    
    plt.subplot(3, 1, 3)
    plt.plot(t, Q11, linewidth=1.0, color='green')
    plt.xlabel("Time (s)"); plt.ylabel("Q11 (drift)"); plt.title("AKF Process Noise Q11 (Drift)")
    plt.grid(True, which='both')
    plt.yscale('log')
    
    plt.tight_layout()
    plt.savefig(output_prefix + "adapt.png", dpi=160, bbox_inches="tight"); plt.close()

if __name__ == "__main__":
    main()