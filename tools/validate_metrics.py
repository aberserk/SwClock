#!/usr/bin/env python3
"""
validate_metrics.py - Independent Metrics Validation

Recomputes MTIE/TDEV from raw CSV data and validates against test assertions.
This breaks the circular dependency where analysis tools parse test-computed values.

Part of Priority 1 implementation (Recommendation 1: Eliminate Printf Parsing).

Author: SwClock Development Team
Date: 2026-01-13
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

# Import existing IEEE metrics computation
from ieee_metrics import IEEEMetrics, analyze_performance_data


class ValidationReport:
    """Container for validation results"""
    
    def __init__(self):
        self.passed = False
        self.computed_metrics = {}
        self.test_assertions = {}
        self.validation_errors = []
        self.warnings = []
    
    def to_dict(self):
        return {
            'passed': self.passed,
            'computed_metrics': self.computed_metrics,
            'test_assertions': self.test_assertions,
            'validation_errors': self.validation_errors,
            'warnings': self.warnings
        }
    
    def __str__(self):
        status = "PASS" if self.passed else "FAIL"
        error_count = len(self.validation_errors)
        warning_count = len(self.warnings)
        return (f"ValidationReport({status}, "
                f"{error_count} errors, {warning_count} warnings)")


class IndependentMetricsValidator:
    """
    Recomputes MTIE/TDEV from raw data and validates against test assertions.
    
    This provides independent verification of metrics without relying on
    test-computed values, ensuring no circular validation dependencies.
    """
    
    def __init__(self, tolerance_percent: float = 1.0):
        """
        Args:
            tolerance_percent: Acceptable relative error for metric validation (%)
        """
        self.tolerance = tolerance_percent / 100.0
        self.metrics_engine = IEEEMetrics()
    
    def load_csv_data(self, csv_path: Path) -> Tuple[np.ndarray, np.ndarray, Dict]:
        """
        Load TE time series from CSV file.
        
        Returns:
            (timestamps_ns, te_ns, metadata)
        """
        metadata = {}
        
        # Parse CSV header for metadata
        with open(csv_path, 'r') as f:
            for line in f:
                if not line.startswith('#'):
                    break
                
                # Extract key metadata fields
                if 'Test Run ID:' in line:
                    metadata['test_run_id'] = line.split(':', 1)[1].strip()
                elif 'Test Name:' in line:
                    metadata['test_name'] = line.split(':', 1)[1].strip()
                elif 'SwClock Version:' in line:
                    metadata['version'] = line.split(':', 1)[1].strip()
                elif 'Start Time (UTC):' in line:
                    metadata['start_time'] = line.split(':', 1)[1].strip()
                elif 'Sample Rate:' in line:
                    # Extract Hz value
                    parts = line.split()
                    for i, part in enumerate(parts):
                        if part == 'Hz' and i > 0:
                            metadata['sample_rate_hz'] = float(parts[i-1])
        
        # Load time series data
        data = np.loadtxt(csv_path, delimiter=',', comments='#', skiprows=1)
        
        if data.shape[0] == 0:
            raise ValueError(f"No data in CSV file: {csv_path}")
        
        timestamps_ns = data[:, 0]
        te_ns = data[:, 1]
        
        # Infer sample rate if not in header
        if 'sample_rate_hz' not in metadata:
            dt_s = np.mean(np.diff(timestamps_ns)) / 1e9
            metadata['sample_rate_hz'] = 1.0 / dt_s
        
        return timestamps_ns, te_ns, metadata
    
    def compute_independent_metrics(
        self,
        te_ns: np.ndarray,
        sample_rate_hz: float
    ) -> Dict[str, any]:
        """
        Compute metrics independently from raw TE data.
        
        Args:
            te_ns: Time error samples (nanoseconds)
            sample_rate_hz: Sample rate (Hz)
        
        Returns:
            Dictionary of computed metrics
        """
        sample_dt_s = 1.0 / sample_rate_hz
        
        # Compute basic TE statistics using analyze_performance_data
        analysis = analyze_performance_data(te_ns, sample_rate_hz)
        te_stats = {
            'mean_te_ns': analysis.get('mean_te_detrended', 0.0),
            'std_te_ns': analysis.get('std_te', 0.0),
            'max_te_ns': analysis.get('max_te', 0.0),
            'min_te_ns': analysis.get('min_te', 0.0),
            'p95_te_ns': analysis.get('p95_te', 0.0),
            'p99_te_ns': analysis.get('p99_te', 0.0)
        }
        
        # Extract MTIE/TDEV from analysis
        mtie_results = {}
        tdev_results = {}
        
        if 'mtie' in analysis:
            for tau_s, value_ns in analysis['mtie'].items():
                mtie_results[f'mtie_{int(tau_s)}s'] = value_ns
        
        if 'tdev' in analysis:
            for tau_s, value_ns in analysis['tdev'].items():
                tdev_results[f'tdev_{tau_s}s'] = value_ns
        
        return {
            'te_stats': te_stats,
            'mtie': mtie_results,
            'tdev': tdev_results
        }
    
    def validate_csv_file(
        self,
        csv_path: Path,
        assertions: Optional[Dict[str, float]] = None
    ) -> ValidationReport:
        """
        Validate test run from CSV log file.
        
        Args:
            csv_path: Path to CSV log file
            assertions: Optional dictionary of expected metric values
        
        Returns:
            ValidationReport with results
        """
        report = ValidationReport()
        
        try:
            # Load raw data
            timestamps_ns, te_ns, metadata = self.load_csv_data(csv_path)
            sample_rate_hz = metadata.get('sample_rate_hz', 10.0)
            
            report.warnings.append(
                f"Loaded {len(te_ns)} samples at {sample_rate_hz:.3f} Hz"
            )
            
            # Compute metrics independently
            computed = self.compute_independent_metrics(te_ns, sample_rate_hz)
            report.computed_metrics = computed
            
            # If no assertions provided, assume pass (metrics computed successfully)
            if assertions is None or len(assertions) == 0:
                report.passed = True
                report.warnings.append(
                    "No assertions provided - validation based on computation only"
                )
                return report
            
            report.test_assertions = assertions
            
            # Cross-validate MTIE
            for key, expected_value in assertions.items():
                if key.startswith('mtie_'):
                    computed_value = computed['mtie'].get(key)
                    
                    if computed_value is None:
                        report.validation_errors.append(
                            f"{key}: Not computed (insufficient data)"
                        )
                        continue
                    
                    rel_error = abs(computed_value - expected_value) / expected_value
                    
                    if rel_error > self.tolerance:
                        report.validation_errors.append(
                            f"{key}: computed={computed_value:.1f} ns, "
                            f"expected={expected_value:.1f} ns, "
                            f"error={rel_error*100:.2f}%"
                        )
            
            # Cross-validate TDEV
            for key, expected_value in assertions.items():
                if key.startswith('tdev_'):
                    computed_value = computed['tdev'].get(key)
                    
                    if computed_value is None:
                        report.validation_errors.append(
                            f"{key}: Not computed (insufficient data)"
                        )
                        continue
                    
                    rel_error = abs(computed_value - expected_value) / expected_value
                    
                    if rel_error > self.tolerance:
                        report.validation_errors.append(
                            f"{key}: computed={computed_value:.1f} ns, "
                            f"expected={expected_value:.1f} ns, "
                            f"error={rel_error*100:.2f}%"
                        )
            
            # Cross-validate TE stats
            for key, expected_value in assertions.items():
                if key in ['mean_te_ns', 'std_te_ns', 'max_te_ns', 'min_te_ns']:
                    computed_value = computed['te_stats'].get(key)
                    
                    if computed_value is None:
                        continue
                    
                    rel_error = abs(computed_value - expected_value) / abs(expected_value + 1e-9)
                    
                    if rel_error > self.tolerance:
                        report.validation_errors.append(
                            f"{key}: computed={computed_value:.1f} ns, "
                            f"expected={expected_value:.1f} ns, "
                            f"error={rel_error*100:.2f}%"
                        )
            
            report.passed = (len(report.validation_errors) == 0)
            
        except Exception as e:
            report.passed = False
            report.validation_errors.append(f"Exception: {str(e)}")
        
        return report
    
    def validate_directory(
        self,
        log_dir: Path,
        pattern: str = "*.csv"
    ) -> Dict[str, ValidationReport]:
        """
        Validate all CSV files in directory.
        
        Args:
            log_dir: Directory containing CSV log files
            pattern: Glob pattern for CSV files
        
        Returns:
            Dictionary mapping file paths to ValidationReports
        """
        results = {}
        
        csv_files = list(log_dir.glob(pattern))
        
        if len(csv_files) == 0:
            print(f"Warning: No CSV files found in {log_dir}", file=sys.stderr)
        
        for csv_file in csv_files:
            # Skip servo state logs (they have different format)
            if 'servo_state' in csv_file.name:
                continue
            
            print(f"Validating: {csv_file.name}...", end=' ')
            
            report = self.validate_csv_file(csv_file)
            results[str(csv_file)] = report
            
            if report.passed:
                print("✓ PASS")
            else:
                print("✗ FAIL")
                for error in report.validation_errors:
                    print(f"  - {error}")
        
        return results


def main():
    parser = argparse.ArgumentParser(
        description='Independent metrics validation for SwClock test logs',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Validate single CSV file
  python3 validate_metrics.py logs/test.csv
  
  # Validate all CSV files in directory
  python3 validate_metrics.py --dir logs/
  
  # With assertions JSON
  python3 validate_metrics.py logs/test.csv --assertions assertions.json
  
  # Generate JSON report
  python3 validate_metrics.py --dir logs/ --output report.json
        """
    )
    
    parser.add_argument(
        'csv_file',
        nargs='?',
        type=Path,
        help='CSV log file to validate'
    )
    
    parser.add_argument(
        '--dir',
        type=Path,
        help='Directory containing CSV files to validate'
    )
    
    parser.add_argument(
        '--assertions',
        type=Path,
        help='JSON file with expected metric values'
    )
    
    parser.add_argument(
        '--tolerance',
        type=float,
        default=1.0,
        help='Relative error tolerance in percent (default: 1.0%%)'
    )
    
    parser.add_argument(
        '--output',
        type=Path,
        help='Output JSON report path'
    )
    
    args = parser.parse_args()
    
    # Validate input
    if not args.csv_file and not args.dir:
        parser.error("Must specify either csv_file or --dir")
    
    # Load assertions if provided
    assertions = None
    if args.assertions:
        with open(args.assertions) as f:
            assertions = json.load(f)
    
    # Create validator
    validator = IndependentMetricsValidator(tolerance_percent=args.tolerance)
    
    # Run validation
    if args.csv_file:
        # Single file validation
        report = validator.validate_csv_file(args.csv_file, assertions)
        
        print(f"\nValidation Report: {args.csv_file.name}")
        print("=" * 60)
        
        if report.passed:
            print("✓ PASSED")
        else:
            print("✗ FAILED")
        
        if report.validation_errors:
            print("\nErrors:")
            for error in report.validation_errors:
                print(f"  - {error}")
        
        if report.warnings:
            print("\nWarnings:")
            for warning in report.warnings:
                print(f"  - {warning}")
        
        print("\nComputed Metrics:")
        for category, metrics in report.computed_metrics.items():
            print(f"  {category}:")
            for key, value in metrics.items():
                if isinstance(value, float):
                    print(f"    {key}: {value:.1f}")
                else:
                    print(f"    {key}: {value}")
        
        if args.output:
            with open(args.output, 'w') as f:
                json.dump(report.to_dict(), f, indent=2)
            print(f"\nReport saved to: {args.output}")
        
        sys.exit(0 if report.passed else 1)
    
    elif args.dir:
        # Directory validation
        results = validator.validate_directory(args.dir)
        
        print("\n" + "=" * 60)
        print("Validation Summary")
        print("=" * 60)
        
        total = len(results)
        passed = sum(1 for r in results.values() if r.passed)
        failed = total - passed
        
        print(f"Total files: {total}")
        print(f"Passed:      {passed}")
        print(f"Failed:      {failed}")
        
        if args.output:
            output_dict = {
                path: report.to_dict()
                for path, report in results.items()
            }
            
            with open(args.output, 'w') as f:
                json.dump(output_dict, f, indent=2)
            
            print(f"\nReport saved to: {args.output}")
        
        sys.exit(0 if failed == 0 else 1)


if __name__ == '__main__':
    main()
