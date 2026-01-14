#!/usr/bin/env python3
"""
validate_test_run.py - End-to-end test validation with dual-path verification

Implements Priority 2 Recommendation 6: Independent Metric Validation
- Extracts metrics from test output (test-computed values)
- Recomputes metrics from raw CSV data (independent computation)
- Asserts values match within tolerance (<1%)
- Generates comprehensive validation report

This breaks circular dependencies and provides confidence in both:
1. Test code correctness (metrics computation)
2. Analysis tool correctness (metrics recomputation)

Author: SwClock Development Team
Date: 2026-01-13
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Optional
from dataclasses import dataclass, asdict
import datetime

# Import our validation tools
from test_output_parser import TestOutputParser
from validate_metrics import IndependentMetricsValidator


@dataclass
class TestValidationResult:
    """Result of validating a single test"""
    test_name: str
    status: str  # 'PASS', 'FAIL', 'SKIP'
    csv_file: Optional[str]
    test_metrics: Dict[str, float]
    computed_metrics: Dict[str, float]
    discrepancies: List[Dict[str, any]]
    max_error_percent: float
    
    def to_dict(self):
        return asdict(self)


@dataclass
class ValidationSummary:
    """Overall validation summary"""
    timestamp: str
    test_output_file: str
    total_tests: int
    passed_tests: int
    failed_tests: int
    skipped_tests: int
    tolerance_percent: float
    results: List[TestValidationResult]
    
    def to_dict(self):
        return {
            'timestamp': self.timestamp,
            'test_output_file': self.test_output_file,
            'total_tests': self.total_tests,
            'passed_tests': self.passed_tests,
            'failed_tests': self.failed_tests,
            'skipped_tests': self.skipped_tests,
            'tolerance_percent': self.tolerance_percent,
            'pass_rate': f"{100.0 * self.passed_tests / max(self.total_tests, 1):.1f}%",
            'results': [r.to_dict() for r in self.results]
        }


class TestRunValidator:
    """
    Validates entire test run by comparing test-computed metrics
    against independently recomputed metrics from raw data.
    """
    
    def __init__(self, tolerance_percent: float = 1.0, logs_dir: Path = Path('logs')):
        """
        Args:
            tolerance_percent: Maximum acceptable relative error
            logs_dir: Directory containing CSV log files
        """
        self.tolerance_percent = tolerance_percent
        self.logs_dir = logs_dir
        self.parser = TestOutputParser()
        self.validator = IndependentMetricsValidator(tolerance_percent)
    
    def validate_test(
        self,
        test_name: str,
        test_metrics: Dict[str, float],
        csv_file: Optional[Path]
    ) -> TestValidationResult:
        """
        Validate a single test by comparing test metrics with recomputed values.
        
        Args:
            test_name: Name of the test
            test_metrics: Metrics extracted from test output
            csv_file: Path to CSV log file
        
        Returns:
            TestValidationResult
        """
        # Skip if no CSV file
        if csv_file is None or not csv_file.exists():
            return TestValidationResult(
                test_name=test_name,
                status='SKIP',
                csv_file=str(csv_file) if csv_file else None,
                test_metrics=test_metrics,
                computed_metrics={},
                discrepancies=[],
                max_error_percent=0.0
            )
        
        # Recompute metrics from CSV
        validation_report = self.validator.validate_csv_file(csv_file, test_metrics)
        
        # Extract computed metrics (flatten nested structure)
        computed_flat = {}
        for category, metrics in validation_report.computed_metrics.items():
            if isinstance(metrics, dict):
                computed_flat.update(metrics)
        
        # Find discrepancies
        discrepancies = []
        max_error = 0.0
        
        for metric_name, test_value in test_metrics.items():
            computed_value = computed_flat.get(metric_name)
            
            if computed_value is None:
                discrepancies.append({
                    'metric': metric_name,
                    'test_value': test_value,
                    'computed_value': None,
                    'error_percent': None,
                    'message': 'Metric not recomputed (insufficient data or unknown metric)'
                })
                continue
            
            # Calculate relative error
            if abs(test_value) < 1e-9:
                # Avoid division by zero for very small values
                abs_error = abs(computed_value - test_value)
                rel_error = 0.0 if abs_error < 1e-9 else 100.0
            else:
                rel_error = 100.0 * abs(computed_value - test_value) / abs(test_value)
            
            max_error = max(max_error, rel_error)
            
            if rel_error > self.tolerance_percent:
                discrepancies.append({
                    'metric': metric_name,
                    'test_value': test_value,
                    'computed_value': computed_value,
                    'error_percent': rel_error,
                    'message': f'Exceeds tolerance ({self.tolerance_percent}%)'
                })
        
        # Determine status
        if not discrepancies:
            status = 'PASS'
        elif any(d['error_percent'] is not None and d['error_percent'] > self.tolerance_percent 
                for d in discrepancies):
            status = 'FAIL'
        else:
            status = 'PASS'  # Missing metrics don't fail the test
        
        return TestValidationResult(
            test_name=test_name,
            status=status,
            csv_file=str(csv_file),
            test_metrics=test_metrics,
            computed_metrics=computed_flat,
            discrepancies=discrepancies,
            max_error_percent=max_error
        )
    
    def validate_test_run(self, test_output_file: Path) -> ValidationSummary:
        """
        Validate entire test run from output file.
        
        Args:
            test_output_file: Path to test output (stdout/log)
        
        Returns:
            ValidationSummary with all results
        """
        # Parse test output
        parsed = self.parser.parse_file(test_output_file)
        
        results = []
        
        for test in parsed['tests']:
            # Only validate tests that passed with metrics
            if test['status'] != 'OK' or not test['metrics']:
                continue
            
            # Find CSV file for this test
            csv_file = self.parser.find_csv_for_test(
                test['name'],
                test['csv_files'],
                self.logs_dir
            )
            
            # Validate test
            result = self.validate_test(
                test['name'],
                test['metrics'],
                csv_file
            )
            
            results.append(result)
        
        # Generate summary
        passed = sum(1 for r in results if r.status == 'PASS')
        failed = sum(1 for r in results if r.status == 'FAIL')
        skipped = sum(1 for r in results if r.status == 'SKIP')
        
        summary = ValidationSummary(
            timestamp=datetime.datetime.now().isoformat(),
            test_output_file=str(test_output_file),
            total_tests=len(results),
            passed_tests=passed,
            failed_tests=failed,
            skipped_tests=skipped,
            tolerance_percent=self.tolerance_percent,
            results=results
        )
        
        return summary


def print_summary(summary: ValidationSummary):
    """Pretty-print validation summary"""
    print("\n" + "=" * 70)
    print("TEST VALIDATION REPORT - Independent Metric Verification")
    print("=" * 70)
    print(f"Timestamp:    {summary.timestamp}")
    print(f"Test Output:  {summary.test_output_file}")
    print(f"Tolerance:    ±{summary.tolerance_percent}%")
    print()
    print(f"Total Tests:  {summary.total_tests}")
    print(f"  ✓ Passed:   {summary.passed_tests}")
    print(f"  ✗ Failed:   {summary.failed_tests}")
    print(f"  ⊘ Skipped:  {summary.skipped_tests}")
    print(f"  Pass Rate:  {100.0 * summary.passed_tests / max(summary.total_tests, 1):.1f}%")
    print("=" * 70)
    
    # Print detailed results
    for result in summary.results:
        status_icon = {
            'PASS': '✓',
            'FAIL': '✗',
            'SKIP': '⊘'
        }[result.status]
        
        print(f"\n{status_icon} {result.test_name} [{result.status}]")
        
        if result.status == 'SKIP':
            print(f"  Reason: No CSV file found")
            continue
        
        print(f"  CSV: {result.csv_file}")
        print(f"  Metrics: {len(result.test_metrics)}")
        print(f"  Max Error: {result.max_error_percent:.3f}%")
        
        if result.discrepancies:
            print(f"  Discrepancies: {len(result.discrepancies)}")
            for disc in result.discrepancies[:5]:  # Show first 5
                metric = disc['metric']
                test_val = disc['test_value']
                comp_val = disc['computed_value']
                error = disc['error_percent']
                
                if error is not None:
                    print(f"    • {metric}: test={test_val:.1f}, "
                          f"computed={comp_val:.1f}, error={error:.3f}%")
                else:
                    print(f"    • {metric}: {disc['message']}")
    
    print("\n" + "=" * 70)
    
    if summary.failed_tests > 0:
        print("VALIDATION FAILED: Metric discrepancies exceed tolerance")
        print("ACTION REQUIRED: Investigate metric computation differences")
    else:
        print("VALIDATION PASSED: All metrics within tolerance")
        print("✓ Independent verification confirms test accuracy")
    
    print("=" * 70 + "\n")


def main():
    """CLI entry point"""
    parser = argparse.ArgumentParser(
        description='Validate test run with independent metric verification',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Validate most recent test run
  ./validate_test_run.py test_output.log
  
  # Specify logs directory
  ./validate_test_run.py test_output.log --logs-dir performance/latest/raw_data
  
  # Save validation report
  ./validate_test_run.py test_output.log -o validation_report.json
  
  # Stricter tolerance
  ./validate_test_run.py test_output.log --tolerance 0.5
        """
    )
    
    parser.add_argument('test_output', type=Path,
                       help='Test output file (gtest log)')
    parser.add_argument('--logs-dir', type=Path, default=Path('logs'),
                       help='Directory containing CSV logs (default: logs/)')
    parser.add_argument('--tolerance', type=float, default=1.0,
                       help='Maximum acceptable error percent (default: 1.0)')
    parser.add_argument('--output', '-o', type=Path,
                       help='Save validation report to JSON file')
    parser.add_argument('--quiet', '-q', action='store_true',
                       help='Suppress detailed output')
    
    args = parser.parse_args()
    
    if not args.test_output.exists():
        print(f"Error: Test output file not found: {args.test_output}")
        return 1
    
    if not args.logs_dir.exists():
        print(f"Warning: Logs directory not found: {args.logs_dir}")
        print(f"CSV files may not be located for validation")
    
    # Run validation
    validator = TestRunValidator(
        tolerance_percent=args.tolerance,
        logs_dir=args.logs_dir
    )
    
    summary = validator.validate_test_run(args.test_output)
    
    # Print results
    if not args.quiet:
        print_summary(summary)
    
    # Save JSON report
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(summary.to_dict(), f, indent=2)
        
        if not args.quiet:
            print(f"Validation report saved to: {args.output}\n")
    
    # Exit with failure if any tests failed
    return 0 if summary.failed_tests == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
