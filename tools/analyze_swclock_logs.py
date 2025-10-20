#!/usr/bin/env python3
"""
SwClock Log Analyzer
Analyzes and plots SwClock log files to visualize clock behavior over time.
"""

import sys
import subprocess
import os
from pathlib import Path

def install_package(package):
    """Install a Python package using pip with user flag."""
    print(f"Installing {package}...")
    try:
        # Try with --user flag first (safer for externally managed environments)
        subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", package])
    except subprocess.CalledProcessError:
        try:
            # If that fails, try with --break-system-packages
            subprocess.check_call([sys.executable, "-m", "pip", "install", "--break-system-packages", package])
        except subprocess.CalledProcessError:
            print(f"Failed to install {package} automatically.")
            print("Please install manually using one of these methods:")
            print(f"  pip3 install --user {package}")
            print(f"  brew install python-{package.replace('_', '-')}")
            print(f"  Or create a virtual environment:")
            print(f"    python3 -m venv venv")
            print(f"    source venv/bin/activate") 
            print(f"    pip install {package}")
            sys.exit(1)

def check_and_install_dependencies():
    """Check for required packages and install them if missing."""
    required_packages = {
        'pandas': 'pandas',
        'matplotlib': 'matplotlib', 
        'numpy': 'numpy'
    }
    
    missing_packages = []
    
    for module_name, package_name in required_packages.items():
        try:
            __import__(module_name)
        except ImportError:
            missing_packages.append(package_name)
    
    if missing_packages:
        print(f"Missing required packages: {', '.join(missing_packages)}")
        print("Installing missing packages...")
        for package in missing_packages:
            install_package(package)
        print("All packages installed successfully!")

# Check and install dependencies before importing
check_and_install_dependencies()

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import glob

def find_latest_log():
    """Find the most recent SwClock log file in the logs directory."""
    # Get the directory where this script is located
    script_dir = Path(__file__).parent
    # Navigate to the logs directory relative to the script
    logs_dir = script_dir.parent / "logs"
    
    # Look for any CSV files that contain "SwClock" in the name
    log_pattern = str(logs_dir / "*SwClock*.csv")
    log_files = glob.glob(log_pattern)
    
    if not log_files:
        raise FileNotFoundError(f"No SwClock log files found in {logs_dir}/ directory")
    
    # Sort by modification time, most recent first
    log_files.sort(key=os.path.getmtime, reverse=True)
    return log_files[0]

def extract_datetime_from_log(log_file):
    """Extract datetime from log file for organizing plots."""
    try:
        # Try to extract from filename first (format: YYYYMMDD-HHMMSS-SwClockLogs.csv)
        filename = Path(log_file).name
        if filename.startswith('202') and '-SwClockLogs.csv' in filename:
            datetime_part = filename.split('-SwClockLogs.csv')[0]
            return datetime_part
        
        # Fallback: read from log file header
        with open(log_file, 'r') as f:
            for line in f:
                if line.startswith('# Started at:'):
                    # Extract datetime from "# Started at: 2025-10-19 18:51:26"
                    datetime_str = line.split('# Started at: ')[1].strip()
                    # Convert to filename-safe format
                    return datetime_str.replace(' ', '-').replace(':', '')
                elif not line.startswith('#'):
                    break
        
        # Final fallback: use current timestamp
        import datetime
        return datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
        
    except Exception:
        # If all else fails, use current timestamp
        import datetime
        return datetime.datetime.now().strftime('%Y%m%d-%H%M%S')

def load_log_data(log_file):
    """Load and parse SwClock log data."""
    print(f"Loading log file: {log_file}")
    
    # Read the CSV, skipping comment lines
    df = pd.read_csv(log_file, comment='#')
    
    print(f"Loaded {len(df)} data points")
    print(f"Time range: {df['timestamp_ns'].iloc[0]} to {df['timestamp_ns'].iloc[-1]} ns")
    
    return df

def convert_to_relative_time(df):
    """Convert absolute timestamps to relative time in seconds."""
    df = df.copy()
    
    # Convert to relative time in seconds
    start_time = df['timestamp_ns'].iloc[0]
    df['time_s'] = (df['timestamp_ns'] - start_time) / 1e9
    
    # Convert base times to relative as well
    df['base_rt_rel_s'] = (df['base_rt_ns'] - df['base_rt_ns'].iloc[0]) / 1e9
    df['base_mono_rel_s'] = (df['base_mono_ns'] - df['base_mono_ns'].iloc[0]) / 1e9
    
    return df

def plot_timestamps(df):
    """Plot timestamp_ns, base_rt_ns, base_mono_ns on the same plot."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
    
    # Plot 1: All three timestamps on the same scale (relative to their start times)
    ax1.plot(df['time_s'], df['time_s'], 'b-', label='timestamp (log time)', linewidth=2)
    ax1.plot(df['time_s'], df['base_rt_rel_s'], 'r-', label='base_rt (realtime)', linewidth=2)
    ax1.plot(df['time_s'], df['base_mono_rel_s'], 'g-', label='base_mono (monotonic)', linewidth=2)
    
    ax1.set_xlabel('Time (seconds)')
    ax1.set_ylabel('Relative Time (seconds)')
    ax1.set_title('SwClock Time Bases Comparison (All Relative to Start)')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # Plot 2: Differences between time bases (more interesting for clock analysis)
    rt_vs_log = df['base_rt_rel_s'] - df['time_s']
    mono_vs_log = df['base_mono_rel_s'] - df['time_s']
    rt_vs_mono = df['base_rt_rel_s'] - df['base_mono_rel_s']
    
    ax2.plot(df['time_s'], rt_vs_log * 1e6, 'r-', label='(base_rt - timestamp) [μs]', linewidth=2)
    ax2.plot(df['time_s'], mono_vs_log * 1e6, 'g-', label='(base_mono - timestamp) [μs]', linewidth=2)
    ax2.plot(df['time_s'], rt_vs_mono * 1e6, 'm-', label='(base_rt - base_mono) [μs]', linewidth=2)
    
    ax2.set_xlabel('Time (seconds)')
    ax2.set_ylabel('Time Difference (microseconds)')
    ax2.set_title('Time Base Differences (Clock Drift Analysis)')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    return fig

def plot_frequency_data(df):
    """Plot frequency-related parameters."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
    
    # Frequency adjustments
    ax1.plot(df['time_s'], df['freq_scaled_ppm'], 'b-', label='freq_scaled_ppm', linewidth=2)
    ax1.plot(df['time_s'], df['pi_freq_ppm'], 'r-', label='pi_freq_ppm', linewidth=2)
    ax1.set_xlabel('Time (seconds)')
    ax1.set_ylabel('Frequency (ppm)')
    ax1.set_title('Frequency Adjustments')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # PI Servo parameters
    ax2.plot(df['time_s'], df['pi_int_error_s'], 'g-', label='pi_int_error_s', linewidth=2)
    ax2.plot(df['time_s'], df['remaining_phase_ns'] / 1e6, 'm-', label='remaining_phase_ms', linewidth=2)
    ax2.set_xlabel('Time (seconds)')
    ax2.set_ylabel('Error / Phase')
    ax2.set_title('PI Servo Error and Phase')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    return fig

def plot_system_parameters(df):
    """Plot system-level parameters."""
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10))
    
    # Error estimates
    ax1.plot(df['time_s'], df['maxerror'], 'r-', label='maxerror', linewidth=2)
    ax1.plot(df['time_s'], df['esterror'], 'b-', label='esterror', linewidth=2)
    ax1.set_xlabel('Time (seconds)')
    ax1.set_ylabel('Error (units)')
    ax1.set_title('System Error Estimates')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # Other parameters
    ax2.plot(df['time_s'], df['constant'], 'g-', label='constant', linewidth=2)
    ax2.plot(df['time_s'], df['tick'], 'm-', label='tick', linewidth=2)
    ax2.plot(df['time_s'], df['tai'], 'c-', label='tai', linewidth=2)
    ax2.plot(df['time_s'], df['pi_servo_enabled'], 'k-', label='pi_servo_enabled', linewidth=2)
    ax2.set_xlabel('Time (seconds)')
    ax2.set_ylabel('Value')
    ax2.set_title('System Parameters')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    return fig

def print_statistics(df):
    """Print basic statistics about the log data."""
    print("\n" + "="*60)
    print("SWCLOCK LOG ANALYSIS SUMMARY")
    print("="*60)
    
    duration = df['time_s'].iloc[-1] - df['time_s'].iloc[0]
    sample_rate = len(df) / duration if duration > 0 else 0
    
    print(f"Log Duration: {duration:.3f} seconds")
    print(f"Sample Count: {len(df)} samples")
    print(f"Sample Rate: {sample_rate:.1f} Hz")
    
    print(f"\nFrequency Adjustments:")
    print(f"  freq_scaled_ppm: min={df['freq_scaled_ppm'].min():.3f}, max={df['freq_scaled_ppm'].max():.3f}, mean={df['freq_scaled_ppm'].mean():.3f}")
    print(f"  pi_freq_ppm: min={df['pi_freq_ppm'].min():.6f}, max={df['pi_freq_ppm'].max():.6f}, mean={df['pi_freq_ppm'].mean():.6f}")
    
    print(f"\nPI Servo Status:")
    print(f"  PI Servo Enabled: {df['pi_servo_enabled'].iloc[0] == 1}")
    print(f"  Integral Error: min={df['pi_int_error_s'].min():.9f}, max={df['pi_int_error_s'].max():.9f}")
    print(f"  Remaining Phase: min={df['remaining_phase_ns'].min():.0f} ns, max={df['remaining_phase_ns'].max():.0f} ns")
    
    print(f"\nSystem Parameters:")
    print(f"  Max Error: min={df['maxerror'].min()}, max={df['maxerror'].max()}")
    print(f"  Est Error: min={df['esterror'].min()}, max={df['esterror'].max()}")
    print(f"  TAI Offset: {df['tai'].iloc[0]}")

def main():
    """Main analysis function."""
    try:
        # Find and load the latest log
        log_file = find_latest_log()
        df = load_log_data(log_file)
        
        # Convert to relative time
        df = convert_to_relative_time(df)
        
        # Print statistics
        print_statistics(df)
        
        # Create plots
        print(f"\nGenerating plots...")
        
        # Plot 1: Time bases comparison
        fig1 = plot_timestamps(df)
        
        # Plot 2: Frequency data
        fig2 = plot_frequency_data(df)
        
        # Plot 3: System parameters
        fig3 = plot_system_parameters(df)
        
        # Save plots in timestamped directory
        script_dir = Path(__file__).parent
        log_datetime = extract_datetime_from_log(log_file)
        plots_base_dir = script_dir.parent / "plots"
        plots_dir = plots_base_dir / f"swclock_analysis_{log_datetime}"
        plots_dir.mkdir(parents=True, exist_ok=True)
        
        base_name = Path(log_file).stem
        fig1.savefig(plots_dir / f'{base_name}_timestamps.png', dpi=150, bbox_inches='tight')
        fig2.savefig(plots_dir / f'{base_name}_frequency.png', dpi=150, bbox_inches='tight')
        fig3.savefig(plots_dir / f'{base_name}_system.png', dpi=150, bbox_inches='tight')
        
        print(f"Plots saved to {plots_dir}/ directory")
        
        # Show plots
        plt.show()
        
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())