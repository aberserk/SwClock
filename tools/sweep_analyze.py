
#!/usr/bin/env python3
import sys
import subprocess
from pathlib import Path
import os

def ensure_dependencies():
    """Check and install required packages, creating venv if needed"""
    req_file = Path(__file__).parent / 'requirements.txt'
    venv_path = Path(__file__).parent.parent / 'venv'
    
    # Check if packages are available
    try:
        import numpy
        import pandas  
        import matplotlib
        print("✓ All dependencies available")
        return
    except ImportError:
        pass
    
    print("Missing dependencies, setting up environment...")
    
    # Create virtual environment if it doesn't exist
    if not venv_path.exists():
        print(f"Creating virtual environment at {venv_path}...")
        try:
            subprocess.check_call([sys.executable, '-m', 'venv', str(venv_path)])
        except subprocess.CalledProcessError as e:
            print(f"❌ Failed to create virtual environment: {e}")
            sys.exit(1)
    
    # Get the venv python executable
    if os.name == 'nt':  # Windows
        venv_python = venv_path / 'Scripts' / 'python.exe'
        venv_pip = venv_path / 'Scripts' / 'pip.exe'
    else:  # Unix/macOS
        venv_python = venv_path / 'bin' / 'python'
        venv_pip = venv_path / 'bin' / 'pip'
    
    # If we're not already using the venv python, restart with it
    if sys.executable != str(venv_python) and venv_python.exists():
        print(f"Switching to virtual environment python...")
        os.execv(str(venv_python), [str(venv_python)] + sys.argv)
    
    # Install requirements in the virtual environment
    if req_file.exists() and venv_pip.exists():
        print("Installing dependencies in virtual environment...")
        try:
            subprocess.check_call([str(venv_pip), 'install', '-r', str(req_file)])
            print("✓ Dependencies installed successfully")
        except subprocess.CalledProcessError as e:
            print(f"❌ Failed to install dependencies: {e}")
            sys.exit(1)
    else:
        print("❌ requirements.txt not found or pip not available")
        sys.exit(1)

# Ensure dependencies before importing
ensure_dependencies()

import argparse, os, glob, math, json
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

def allan_deviation(times, values, taus):
    times = np.asarray(times, dtype=float)
    values = np.asarray(values, dtype=float)
    if len(times) < 5:
        return {t: np.nan for t in taus}
    t0, t1 = times[0], times[-1]
    dt = np.median(np.diff(times)) if len(times)>1 else (t1-t0)/max(1,len(times)-1)
    if dt <= 0: dt = (t1-t0)/max(1,len(times)-1)
    t_uniform = np.arange(t0, t1 + 1e-12, dt)
    v_uniform = np.interp(t_uniform, times, values)
    adev = {}
    for tau in taus:
        m = max(int(round(tau / dt)), 1)
        z = np.diff(v_uniform)/dt
        if len(z) < 2*m:
            adev[tau] = np.nan
            continue
        d = z[2*m:] - 2*z[m:-m] + z[:-2*m]
        adev[tau] = float(np.sqrt(0.5 * np.mean(d**2)))
    return adev

def metrics_for(df, burn_in_s=10.0, taus=(1.0,)):
    rows = []
    for servo, g in df.groupby("servo"):
        g = g.sort_values("t_s")
        g_ss = g[g["t_s"] >= (g["t_s"].min() + burn_in_s)].copy()
        if g_ss.empty: g_ss = g.copy()
        err = g_ss["offset_s"].values
        ae = np.abs(err)
        met = {
            "servo": servo,
            "count": len(g_ss),
            "rmse_ms": float(np.sqrt(np.mean(err**2)) * 1e3),
            "mae_ms": float(np.mean(ae) * 1e3),
            "median_ae_ms": float(np.median(ae) * 1e3),
            "std_ms": float(np.std(err) * 1e3),
            "p95_ae_ms": float(np.percentile(ae, 95) * 1e3),
            "max_ae_ms": float(np.max(ae) * 1e3),
        }
        adev = allan_deviation(g_ss["t_s"].values, g_ss["offset_s"].values, taus)
        for tau,val in adev.items():
            met[f"adev_tau{tau}s"] = val
        rows.append(met)
    return pd.DataFrame(rows).set_index("servo")

def rank_servos(df, taus=(1.0,), weights=None):
    if weights is None:
        weights = {"rmse_ms":0.45, "p95_ae_ms":0.35, "std_ms":0.2}
    for tau in taus:
        weights[f"adev_tau{tau}s"] = weights.get(f"adev_tau{tau}s", 0.2/len(taus))
    scores = []
    for servo, row in df.iterrows():
        score = 0.0
        for k,w in weights.items():
            v = row.get(k, np.nan)
            if np.isnan(v): continue
            if k.startswith("adev_tau"):
                tau = float(k.replace("adev_tau","").replace("s",""))
                v = v * 1e3 * tau
            score += w * v
        scores.append((servo, score))
    ranks = pd.DataFrame(scores, columns=["servo","score"]).sort_values("score")
    return ranks

def analyze_folder(in_dir: Path, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    csvs = sorted(in_dir.glob("*.csv"))
    presets = {}
    for f in csvs:
        name = f.stem.split("__")[-1]  # ...__<Preset>
        df = pd.read_csv(f, comment="#")
        low = {c.lower(): c for c in df.columns}
        rename = {}
        for k in ["t_s","servo","offset_s","drift_ppb","z_meas_s","had_meas"]:
            if k not in df.columns and k in low: rename[low[k]] = k
        if rename: df = df.rename(columns=rename)
        presets.setdefault(name, []).append(df)
    taus=(1.0,)
    summary_rows = []
    for preset, dfs in presets.items():
        # concat and compute metrics per (servo,config) pair
        big = pd.concat(dfs, ignore_index=True)
        # compute metrics per unique (servo, config_id) using header metadata isn’t in rows, so infer from filename
        # Instead we’ll compute per (servo) since each CSV is one servo+config; we carry config id via a column
        # Add config from filename
        augmented = []
        for f in csvs:
            if not f.stem.endswith(preset): continue
            parts = f.stem.split("__")
            servo = parts[0]
            config = parts[1]
            df = pd.read_csv(f, comment="#")
            low = {c.lower(): c for c in df.columns}
            rename = {}
            for k in ["t_s","servo","offset_s","drift_ppb","z_meas_s","had_meas"]:
                if k not in df.columns and k in low: rename[low[k]] = k
            if rename: df = df.rename(columns=rename)
            if "servo" in df.columns:
                df["servo"] = servo + "::" + config
            augmented.append(df)
        big = pd.concat(augmented, ignore_index=True)

        m = metrics_for(big, burn_in_s=10.0, taus=taus)
        r = rank_servos(m, taus=taus)
        m.to_csv(out_dir / f"{preset}_metrics.csv")
        r.to_csv(out_dir / f"{preset}_ranks.csv", index=False)
        best = r.iloc[0]
        summary_rows.append({"preset": preset, "best": best["servo"], "score": float(best["score"])})

    summary = pd.DataFrame(summary_rows).sort_values("preset")
    summary.to_csv(out_dir / "summary.csv", index=False)

    # Markdown
    lines = []
    lines.append("# Wi‑Fi Servo Parameter Sweep — Best by Preset\n")
    for _, row in summary.iterrows():
        lines.append(f"- **{row['preset']}** → **{row['best']}** (score={row['score']:.3f})")
    lines.append("\n## How to read entries\n`<SERVO>::<CONFIG_ID>`, where `CONFIG_ID` encodes the grid values (e.g., `AKF_Q0R0.8_Q1Q00.2`, `MIX_KP0.065_KI0.0015`, `PI_KP0.1_KI0.001`).")
    (out_dir / "REPORT.md").write_text("\n".join(lines), encoding="utf-8")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--in_dir", type=str, default="out", help="Directory with CSVs produced by sweep_compare_wifi")
    ap.add_argument("--out_dir", type=str, default="report", help="Directory to write metrics/ranks/report")
    args = ap.parse_args()
    analyze_folder(Path(args.in_dir), Path(args.out_dir))
    print(f"Wrote report to {args.out_dir}/REPORT.md")
