#!/usr/bin/env python3
"""
Independent Validation Tool for SwClock Performance Tests

This tool implements IEEE Audit Recommendation 6: Independent Validation
by recomputing MTIE/TDEV metrics from raw CSV data and comparing them
with test-computed values.

Purpose:
- Eliminates circular validation dependency (parsing printf output)
- Provides independent verification path from first principles
- Catches metric computation bugs in test code
- IEEE audit compliant validation methodology

Usage:
    python3 validate_test.py <csv_file> <expected_metrics.json>
    
Exit Codes:
    0 - Validation passed (metrics match within tolerance)
    1 - Validation failed (discrepancy detected)
    2 - Error (file not found, parse error, etc.)

Example:
    python3 validate_test.py logs/20260113-213000-DisciplineTEStats.csv \\
                             logs/20260113-213000-expected.json

Author: SwClock Development Team
Date: 2026-01-13
Part of: IEEE Audit Priority 2 Recommendation 6
"""

import sys
import json
import csv
import re
from typing import Dict, List, Tuple, Optional
from pathlib import Path
import os


# Standalone MTIE/TDEV computation functions
# (Simplified versions - no NumPy dependency for validation tool)

def detrend_te(te_ns: List[float]) -> List[float]:
    """Remove linear trend from TE data"""
    n = len(te_ns)
    if n < 2:
        return te_ns
    
    # Linear regression
    sum_x = 0.0
    sum_y = 0.0
    sum_xx = 0.0
    sum_xy = 0.0
    
    for i, y in enumerate(te_ns):
        x = float(i)
        sum_x += x
        sum_y += y
        sum_xx += x * x
        sum_xy += x * y
    
    denom = n * sum_xx - sum_x * sum_x
    if abs(denom) < 1e-10:
        return te_ns  # No trend
    
    slope = (n * sum_xy - sum_x * sum_y) / denom
    intercept = (sum_y - slope * sum_x) / n
    
    # Remove trend
    detrended = []
    for i, y in enumerate(te_ns):
        detrended.append(y - (slope * i + intercept))
    
    return detrended


def compute_mtie(te_detrended: List[float], sample_rate_hz: float) -> Dict[float, float]:
    """Compute MTIE at standard tau values"""
    tau_values = [1.0, 10.0, 30.0]  # seconds
    sample_dt_s = 1.0 / sample_rate_hz
    results = {}
    
    for tau_s in tau_values:
        k = max(1, int(round(tau_s / sample_dt_s)))
        
        if k >= len(te_detrended):
            results[tau_s] = float('nan')
            continue
        
        # Find maximum difference over all windows of length k
        max_diff = 0.0
        for i in range(len(te_detrended) - k):
            diff = abs(te_detrended[i + k] - te_detrended[i])
            if diff > max_diff:
                max_diff = diff
        
        results[tau_s] = max_diff
    
    return results


def compute_tdev(te_detrended: List[float], sample_rate_hz: float) -> Dict[float, float]:
    """Compute TDEV at standard tau values (simplified overlapping Allan deviation)"""
    tau_values = [0.1, 1.0, 10.0]  # seconds
    sample_dt_s = 1.0 / sample_rate_hz
    results = {}
    
    for tau_s in tau_values:
        m = max(1, int(round(tau_s / sample_dt_s)))
        n = len(te_detrended)
        
        if n < 2 * m + 1:
            results[tau_s] = float('nan')
            continue
        
        # Second difference: y[i+2m] - 2*y[i+m] + y[i]
        sum_sq = 0.0
        count = 0
        for i in range(n - 2 * m):
            val = te_detrended[i + 2 * m] - 2 * te_detrended[i + m] + te_detrended[i]
            sum_sq += val * val
            count += 1
        
        if count == 0:
            results[tau_s] = float('nan')
        else:
            allan_var = sum_sq / (2.0 * count)
            results[tau_s] = allan_var ** 0.5
    
    return results


class CSVMetadataParser:
    """Parse metadata from CSV header comments"""
    
    @staticmethod
    def parse(csv_path: str) -> Dict[str, any]:
        """Extract metadata from RFC-style CSV header"""
        metadata = {}
        
        with open(csv_path, 'r') as f:
            for line in f:
                if not line.startswith('#'):
                    break  # End of header
                
                # Parse key-value pairs
                if ':' in line:
                    # Remove '#' and whitespace
                    clean_line = line.lstrip('#').strip()
                    
                    # Extract key: value
                    match = re.match(r'([^:]+):\s*(.+)', clean_line)
                    if match:
                        key = match.group(1).strip()
                        value = match.group(2).strip()
                        
                        # Clean key for dict storage
                        dict_key = key.lower().replace(' ', '_').replace('(', '').replace(')', '')
                        metadata[dict_key] = value
        
        return metadata


class TEDataLoader:
    """Load TE time series from CSV"""
    
    @staticmethod
    def load(csv_path: str) -> Tuple[List[float], List[float]]:
        """Load timestamp and TE data from CSV
        
        Returns:
            (timestamps_ns, te_ns) as lists of floats
        """
        timestamps = []
        te_values = []
        
        with open(csv_path, 'r') as f:
            # Skip header comments
            for line in f:
                if not line.startswith('#'):
                    # This is the column header line
                    break
            
            # Read CSV data
            reader = csv.DictReader(f, fieldnames=['timestamp_ns', 'te_ns'])
            for row in reader:
                try:
                    timestamps.append(float(row['timestamp_ns']))
                    te_values.append(float(row['te_ns']))
                except (ValueError, KeyError):
                    pass  # Skip malformed lines
        
        return timestamps, te_values


class IndependentMetricsComputer:
    """Compute MTIE/TDEV from first principles"""
    
    @staticmethod
    def compute_all(timestamps_ns: List[float], te_ns: List[float], 
                   sample_rate_hz: float = 10.0) -> Dict[str, float]:
        """Compute all validation metrics from raw data
        
        Args:
            timestamps_ns: Timestamp array (CLOCK_MONOTONIC_RAW)
            te_ns: Time Error array (nanoseconds)
            sample_rate_hz: Data sample rate (default 10 Hz)
        
        Returns:
            Dictionary with computed metrics:
            - mean_raw_ns: Mean TE before detrending
            - mean_ns: Mean TE after detrending
            - std_ns: Standard deviation after detrending
            - mtie_1s_ns, mtie_10s_ns, mtie_30s_ns: MTIE values
            - tdev_0p1s_ns, tdev_1s_ns, tdev_10s_ns: TDEV values
        """
        if len(te_ns) < 10:
            raise ValueError(f"Insufficient data: {len(te_ns)} samples (need >= 10)")
        
        # Compute raw statistics
        mean_raw = sum(te_ns) / len(te_ns)
        
        # Detrend data (remove linear trend)
        te_detrended = detrend_te(te_ns)
        
        # Statistics on detrended data
        mean_detrended = sum(te_detrended) / len(te_detrended)
        variance = sum((x - mean_detrended)**2 for x in te_detrended) / len(te_detrended)
        std_detrended = variance ** 0.5
        
        # MTIE computation
        mtie_results = compute_mtie(te_detrended, sample_rate_hz)
        
        # TDEV computation  
        tdev_results = compute_tdev(te_detrended, sample_rate_hz)
        
        # Package results
        metrics = {
            'mean_raw_ns': mean_raw,
            'mean_ns': mean_detrended,
            'std_ns': std_detrended,
            'sample_count': len(te_ns),
            'sample_rate_hz': sample_rate_hz,
        }
        
        # Extract MTIE values at specific tau
        for tau, value in mtie_results.items():
            if tau == 1.0:
                metrics['mtie_1s_ns'] = value
            elif tau == 10.0:
                metrics['mtie_10s_ns'] = value
            elif tau == 30.0:
                metrics['mtie_30s_ns'] = value
        
        # Extract TDEV values at specific tau
        for tau, value in tdev_results.items():
            if abs(tau - 0.1) < 0.01:
                metrics['tdev_0p1s_ns'] = value
            elif abs(tau - 1.0) < 0.01:
                metrics['tdev_1s_ns'] = value
            elif abs(tau - 10.0) < 0.01:
                metrics['tdev_10s_ns'] = value
        
        return metrics


class MetricsComparator:
    """Compare expected vs computed metrics"""
    
    # Tolerance for metric comparison (1% relative error)
    RELATIVE_TOLERANCE = 0.01
    
    # Absolute tolerance for near-zero values (10 nanoseconds)
    ABSOLUTE_TOLERANCE = 10.0
    
    @staticmethod
    def compare(expected: Dict[str, float], computed: Dict[str, float]) -> Tuple[bool, List[str]]:
        """Compare expected vs computed metrics
        
        Args:
            expected: Metrics from test output (parsed printf)
            computed: Metrics computed by this tool
        
        Returns:
            (all_passed, discrepancy_messages)
        """
        discrepancies = []
        
        # Metrics to validate
        metrics_to_check = [
            'mtie_1s_ns', 'mtie_10s_ns', 'mtie_30s_ns',
            'tdev_0p1s_ns', 'tdev_1s_ns', 'tdev_10s_ns',
            'mean_ns', 'std_ns'
        ]
        
        for metric in metrics_to_check:
            if metric not in expected or metric not in computed:
                continue  # Skip if metric not present
            
            exp_val = expected[metric]
            cmp_val = computed[metric]
            
            # Check if values match within tolerance
            if abs(exp_val) < MetricsComparator.ABSOLUTE_TOLERANCE:
                # Use absolute tolerance for near-zero values
                error = abs(cmp_val - exp_val)
                tolerance = MetricsComparator.ABSOLUTE_TOLERANCE
                passed = error <= tolerance
            else:
                # Use relative tolerance
                rel_error = abs((cmp_val - exp_val) / exp_val)
                passed = rel_error <= MetricsComparator.RELATIVE_TOLERANCE
                error = rel_error * 100  # Convert to percentage
                tolerance = MetricsComparator.RELATIVE_TOLERANCE * 100
            
            if not passed:
                if abs(exp_val) < MetricsComparator.ABSOLUTE_TOLERANCE:
                    msg = (f"  ✗ {metric}: MISMATCH - "
                          f"expected={exp_val:.1f}ns, computed={cmp_val:.1f}ns, "
                          f"error={error:.1f}ns > {tolerance:.1f}ns")
                else:
                    msg = (f"  ✗ {metric}: MISMATCH - "
                          f"expected={exp_val:.1f}ns, computed={cmp_val:.1f}ns, "
                          f"error={error:.2f}% > {tolerance:.2f}%")
                discrepancies.append(msg)
            else:
                if abs(exp_val) < MetricsComparator.ABSOLUTE_TOLERANCE:
                    print(f"  ✓ {metric}: {cmp_val:.1f}ns (matches expected {exp_val:.1f}ns)")
                else:
                    rel_error = abs((cmp_val - exp_val) / exp_val) * 100
                    print(f"  ✓ {metric}: {cmp_val:.1f}ns (error: {rel_error:.3f}%)")
        
        return len(discrepancies) == 0, discrepancies


def main():
    """Main validation routine"""
    if len(sys.argv) < 3:
        print("Usage: validate_test.py <csv_file> <expected_metrics.json>", file=sys.stderr)
        print("\nValidates performance test by recomputing metrics from CSV data", file=sys.stderr)
        return 2
    
    csv_path = sys.argv[1]
    expected_json = sys.argv[2]
    
    # Check files exist
    if not Path(csv_path).exists():
        print(f"ERROR: CSV file not found: {csv_path}", file=sys.stderr)
        return 2
    
    if not Path(expected_json).exists():
        print(f"ERROR: Expected metrics file not found: {expected_json}", file=sys.stderr)
        return 2
    
    print(f"\n{'='*70}")
    print(f"Independent Validation Tool - IEEE Recommendation 6")
    print(f"{'='*70}")
    print(f"\nCSV Data:       {csv_path}")
    print(f"Expected Metrics: {expected_json}\n")
    
    try:
        # Parse metadata from CSV header
        print("[1/5] Parsing CSV metadata...")
        metadata = CSVMetadataParser.parse(csv_path)
        print(f"      Test: {metadata.get('test_name', 'Unknown')}")
        print(f"      Run ID: {metadata.get('test_run_id', 'Unknown')}")
        print(f"      SwClock Version: {metadata.get('swclock_version', 'Unknown')}")
        
        # Load TE time series
        print("\n[2/5] Loading TE time series from CSV...")
        timestamps, te_values = TEDataLoader.load(csv_path)
        print(f"      Loaded {len(te_values)} samples")
        
        # Compute metrics independently
        print("\n[3/5] Computing MTIE/TDEV from first principles...")
        computed_metrics = IndependentMetricsComputer.compute_all(timestamps, te_values)
        print(f"      Computed {len(computed_metrics)} metrics")
        
        # Load expected metrics
        print("\n[4/5] Loading expected metrics from test output...")
        with open(expected_json, 'r') as f:
            expected_metrics = json.load(f)
        print(f"      Loaded {len(expected_metrics)} expected values")
        
        # Compare metrics
        print("\n[5/5] Comparing computed vs expected metrics...")
        all_passed, discrepancies = MetricsComparator.compare(expected_metrics, computed_metrics)
        
        # Report results
        print(f"\n{'='*70}")
        if all_passed:
            print("✓ VALIDATION PASSED")
            print("  All computed metrics match expected values within tolerance")
            print(f"  Tolerance: ±{MetricsComparator.RELATIVE_TOLERANCE*100:.1f}% relative")
            print(f"             ±{MetricsComparator.ABSOLUTE_TOLERANCE:.1f}ns absolute (near-zero)")
            print(f"{'='*70}\n")
            return 0
        else:
            print("✗ VALIDATION FAILED")
            print(f"  {len(discrepancies)} metric(s) exceeded tolerance:\n")
            for msg in discrepancies:
                print(msg)
            print(f"\n{'='*70}")
            print("INTERPRETATION:")
            print("  This indicates either:")
            print("  1. Bug in test code metric computation")
            print("  2. Bug in validation tool metric computation")
            print("  3. Non-deterministic behavior in data collection")
            print(f"{'='*70}\n")
            return 1
        
    except FileNotFoundError as e:
        print(f"\nERROR: File not found: {e}", file=sys.stderr)
        return 2
    except ValueError as e:
        print(f"\nERROR: Invalid data: {e}", file=sys.stderr)
        return 2
    except Exception as e:
        print(f"\nERROR: Unexpected error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 2


if __name__ == '__main__':
    sys.exit(main())
