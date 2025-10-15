
#!/usr/bin/env python3
import argparse
import glob
import os
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

def allan_deviation(times, values, taus):
    """
    Compute Allan deviation for a time series (times in seconds, values in seconds).
    Assumes times are reasonably monotonic; resamples to uniform grid by linear interp.
    """
    times = np.asarray(times, dtype=float)
    values = np.asarray(values, dtype=float)
    # Build uniform grid
    t0, t1 = times[0], times[-1]
    dt = np.median(np.diff(times))
    if dt <= 0:
        dt = (t1 - t0) / (len(times) - 1)
    t_uniform = np.arange(t0, t1 + 1e-12, dt)
    v_uniform = np.interp(t_uniform, times, values)

    adev = {}
    for tau in taus:
        m = max(int(round(tau / dt)), 1)
        # Overlapping Allan deviation
        y = v_uniform  # fractional frequency not available; use time-error series for TE Allan-like metric
        # Convert time error to fractional frequency by first difference
        ydot = np.diff(y) / dt
        if len(ydot) < 2*m:
            adev[tau] = np.nan
            continue
        # Allan deviation of frequency: sqrt(0.5 * mean( (y_{k+2m}-2 y_{k+m}+y_k)^2 ))
        z = ydot
        d = z[2*m:] - 2*z[m:-m] + z[:-2*m]
        adev[tau] = np.sqrt(0.5 * np.mean(d**2))
    return adev

def load_csvs(data_dir):
    files = sorted(glob.glob(os.path.join(data_dir, "compare_wifi_*.csv")))
    if not files:
        raise FileNotFoundError(f"No CSVs found in {data_dir}. Expected files like compare_wifi_Good.csv")
    presets = {}
    for f in files:
        name = Path(f).stem.replace("compare_wifi_","")
        df = pd.read_csv(f)
        presets[name] = df
    return presets

def metrics_for(df, burn_in_s=10.0, taus=(0.5,1,2,5,10)):
    out = {}
    for servo, g in df.groupby("servo"):
        g = g.sort_values("t_s")
        g_ss = g[g["t_s"] >= (g["t_s"].min() + burn_in_s)].copy()
        if g_ss.empty:
            g_ss = g.copy()
        err = g_ss["offset_s"].values
        abs_err = np.abs(err)
        met = {}
        met["count"] = len(g_ss)
        met["rmse_ms"] = float(np.sqrt(np.mean(err**2)) * 1e3)
        met["mae_ms"] = float(np.mean(abs_err) * 1e3)
        met["median_ae_ms"] = float(np.median(abs_err) * 1e3)
        met["std_ms"] = float(np.std(err) * 1e3)
        met["p95_ae_ms"] = float(np.percentile(abs_err, 95) * 1e3)
        met["max_ae_ms"] = float(np.max(abs_err) * 1e3)
        # Allan deviation
        adev = allan_deviation(g_ss["t_s"].values, g_ss["offset_s"].values, taus)
        for tau, val in adev.items():
            met[f"adev_tau{tau}s"] = float(val) if val==val else np.nan  # val==val to check NaN
        out[servo] = met
    return pd.DataFrame(out).T

def rank_servos(metrics_df, weights=None, taus=(1.0,)):
    """
    Weighted score (lower is better):
      score = w_rmse*rmse + w_p95*p95 + w_std*std + sum w_adev[tau]*adev_tau
    All in ms for readability; Allan is in seconds/sec for frequency-like metric,
    we convert to ms-equivalent by multiplying by 1e3 * tau (approximate TE impact over tau).
    """
    if weights is None:
        weights = {"rmse_ms": 0.5, "p95_ae_ms": 0.3, "std_ms": 0.2}
    for tau in taus:
        weights[f"adev_tau{tau}s"] = weights.get(f"adev_tau{tau}s", 0.2/len(taus))

    scores = []
    for servo, row in metrics_df.iterrows():
        score = 0.0
        for k, w in weights.items():
            if k.startswith("adev_tau"):
                tau = float(k.replace("adev_tau","").replace("s",""))
                v = row.get(k, np.nan)
                if np.isnan(v):
                    continue
                # Roughly scale Allan freq dev to ms-equivalent drift over tau
                v_ms = v * 1e3 * tau
                score += w * v_ms
            else:
                v = row.get(k, np.nan)
                if np.isnan(v):
                    continue
                score += w * v
        scores.append((servo, score))
    ranks = pd.DataFrame(scores, columns=["servo","score"]).sort_values("score")
    return ranks

def plot_time_series(df, preset, outdir):
    plt.figure()
    for servo, g in df.groupby("servo"):
        g = g.sort_values("t_s")
        plt.plot(g["t_s"], g["offset_s"]*1e3, label=servo)
    plt.xlabel("Time (s)")
    plt.ylabel("Offset (ms)")
    plt.title(f"Offset vs Time — {preset}")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, f"{preset}_offset_time.png"))
    plt.close()

def plot_cdf(df, preset, outdir, burn_in_s=10.0):
    plt.figure()
    for servo, g in df.groupby("servo"):
        g = g.sort_values("t_s")
        g = g[g["t_s"] >= (g["t_s"].min()+burn_in_s)]
        if g.empty: g = g.sort_values("t_s")
        abs_err_ms = np.abs(g["offset_s"].values)*1e3
        x = np.sort(abs_err_ms)
        y = np.linspace(0,1,len(x))
        plt.plot(x, y, label=servo)
    plt.xlabel("|Offset| (ms)")
    plt.ylabel("CDF")
    plt.title(f"CDF(|offset|) — {preset}")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, f"{preset}_cdf_abs_err.png"))
    plt.close()

def plot_bars(metrics_df, preset, outdir):
    plt.figure()
    sel = ["rmse_ms","p95_ae_ms","std_ms","median_ae_ms"]
    md = metrics_df[sel]
    md.plot(kind="bar")
    plt.ylabel("ms")
    plt.title(f"Metrics — {preset}")
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, f"{preset}_metrics_bar.png"))
    plt.close()

def plot_allan(metrics_df, preset, outdir, taus):
    plt.figure()
    # Create a simple plot of adev at taus for each servo
    for servo, row in metrics_df.iterrows():
        ys = [row.get(f"adev_tau{t}s", np.nan) for t in taus]
        # scale to ms-equivalent over tau for readability
        ys_ms = [y*1e3*t if (y==y) else np.nan for y,t in zip(ys,taus)]
        plt.plot(taus, ys_ms, marker="o", label=servo)
    plt.xscale("log"); plt.yscale("log")
    plt.xlabel("Tau (s)")
    plt.ylabel("Allan-derived ms (approx)")
    plt.title(f"Allan (scaled) — {preset}")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(outdir, f"{preset}_allan.png"))
    plt.close()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", default=".", help="Directory containing compare_wifi_*.csv")
    ap.add_argument("--burn-in", type=float, default=10.0, help="Seconds to ignore for steady-state metrics")
    ap.add_argument("--taus", type=float, nargs="+", default=[0.5,1,2,5,10], help="taus (s) for Allan deviation")
    ap.add_argument("--out-dir", default="wifi_analysis_out", help="Output directory for plots & reports")
    ap.add_argument("--weights", type=str, default="", help="JSON dict of metric weights")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    presets = load_csvs(args.data_dir)
    weights = None
    if args.weights.strip():
        weights = json.loads(args.weights)

    summary_rows = []
    for preset, df in presets.items():
        # Plots
        plot_time_series(df, preset, args.out_dir)
        plot_cdf(df, preset, args.out_dir, burn_in_s=args.burn_in)
        metrics_df = metrics_for(df, burn_in_s=args.burn_in, taus=args.taus)
        plot_bars(metrics_df, preset, args.out_dir)
        plot_allan(metrics_df, preset, args.out_dir, args.taus)

        ranks = rank_servos(metrics_df, weights=weights, taus=args.taus)
        best_servo = ranks.iloc[0]["servo"]
        best_score = ranks.iloc[0]["score"]
        metrics_path = os.path.join(args.out_dir, f"{preset}_metrics.csv")
        ranks_path = os.path.join(args.out_dir, f"{preset}_ranks.csv")
        metrics_df.to_csv(metrics_path, index=True)
        ranks.to_csv(ranks_path, index=False)

        summary_rows.append({
            "preset": preset,
            "best_servo": best_servo,
            "score": best_score
        })
        print(f"[{preset}] Best: {best_servo} (score={best_score:.3f})")
        print(ranks.to_string(index=False))
        print()

    summary = pd.DataFrame(summary_rows).sort_values("preset")
    summary_path = os.path.join(args.out_dir, "summary.csv")
    summary.to_csv(summary_path, index=False)

    # Also write a human-friendly report
    md = ["# Wi‑Fi Servo Comparison Summary\n"]
    for row in summary_rows:
        md.append(f"- **{row['preset']}** → **{row['best_servo']}** (score={row['score']:.3f})")
    with open(os.path.join(args.out_dir, "SUMMARY.md"), "w") as f:
        f.write("\n".join(md))

if __name__ == "__main__":
    main()
