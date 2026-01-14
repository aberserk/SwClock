#!/usr/bin/env python3
"""
test_output_parser.py - Parse GTest output and extract metrics

Extracts MTIE/TDEV values from test output for independent validation.
Part of Priority 2 Recommendation 6: Independent Metric Validation.

Author: SwClock Development Team
Date: 2026-01-13
"""

import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import json


class TestOutputParser:
    """Parse GoogleTest output and extract metrics for validation"""
    
    def __init__(self):
        self.patterns = {
            # MTIE patterns - various formats from test output
            'mtie_paren': re.compile(r'MTIE\(\s*(\d+)\s*s\)\s*=\s*([\d.]+)\s*(?:ns|µs|us)'),
            'mtie_bracket': re.compile(r'MTIE\[\s*(\d+)\s*s\]\s*=\s*([\d.]+)\s*(?:ns|µs|us)'),
            'mtie_at': re.compile(r'MTIE\s+@\s*(\d+)\s*s\s*[:=]\s*([\d.]+)\s*(?:ns|µs|us)'),
            'mtie_colon': re.compile(r'MTIE_(\d+)s\s*[:=]\s*([\d.]+)'),
            
            # TDEV patterns
            'tdev_paren': re.compile(r'TDEV\(\s*([\d.]+)\s*s\)\s*=\s*([\d.]+)\s*(?:ns|µs|us)'),
            'tdev_bracket': re.compile(r'TDEV\[\s*([\d.]+)\s*s\]\s*=\s*([\d.]+)\s*(?:ns|µs|us)'),
            'tdev_at': re.compile(r'TDEV\s+@\s*([\d.]+)\s*s\s*[:=]\s*([\d.]+)\s*(?:ns|µs|us)'),
            'tdev_colon': re.compile(r'TDEV_([\d.]+)s\s*[:=]\s*([\d.]+)'),
            
            # TE statistics
            'mean_te': re.compile(r'Mean\s+TE[^:]*[:=]\s*([-\d.]+)\s*(?:ns|µs|us)'),
            'std_te': re.compile(r'(?:Std|RMS)\s+TE[^:]*[:=]\s*([\d.]+)\s*(?:ns|µs|us)'),
            'max_te': re.compile(r'Max\s+TE[^:]*[:=]\s*([\d.]+)\s*(?:ns|µs|us)'),
            'min_te': re.compile(r'Min\s+TE[^:]*[:=]\s*([-\d.]+)\s*(?:ns|µs|us)'),
            'p95_te': re.compile(r'P95[^:]*[:=]\s*([\d.]+)\s*(?:ns|µs|us)'),
            'p99_te': re.compile(r'P99[^:]*[:=]\s*([\d.]+)\s*(?:ns|µs|us)'),
            
            # Test info
            'test_name': re.compile(r'\[\s*RUN\s*\]\s+(\S+)'),
            'test_status': re.compile(r'\[\s*(OK|FAILED)\s*\]\s+(\S+)'),
            
            # CSV file references
            'csv_file': re.compile(r'(?:Created|Wrote|Log):\s*([^\s]+\.csv)'),
        }
    
    def _convert_to_ns(self, value: float, unit: str) -> float:
        """Convert time values to nanoseconds"""
        unit = unit.lower().strip()
        if unit == 'ns':
            return value
        elif unit in ['µs', 'us']:
            return value * 1000.0
        elif unit == 'ms':
            return value * 1e6
        elif unit == 's':
            return value * 1e9
        return value
    
    def parse_file(self, file_path: Path) -> Dict[str, any]:
        """
        Parse test output file and extract all metrics.
        
        Returns:
            Dictionary with structure:
            {
                'tests': [
                    {
                        'name': 'TestSuite.TestName',
                        'status': 'OK' or 'FAILED',
                        'metrics': {
                            'mtie_1s': 123.4,
                            'tdev_1.0s': 56.7,
                            ...
                        },
                        'csv_files': ['path/to/log.csv']
                    }
                ]
            }
        """
        with open(file_path, 'r') as f:
            content = f.read()
        
        result = {
            'file': str(file_path),
            'tests': []
        }
        
        current_test = None
        
        for line in content.split('\n'):
            # Check for test start
            match = self.patterns['test_name'].search(line)
            if match:
                if current_test:
                    result['tests'].append(current_test)
                
                current_test = {
                    'name': match.group(1),
                    'status': 'RUNNING',
                    'metrics': {},
                    'csv_files': []
                }
                continue
            
            if not current_test:
                continue
            
            # Check for test completion
            match = self.patterns['test_status'].search(line)
            if match:
                status, test_name = match.groups()
                if test_name == current_test['name']:
                    current_test['status'] = status
                continue
            
            # Extract MTIE values
            for pattern_name in ['mtie_paren', 'mtie_bracket', 'mtie_at', 'mtie_colon']:
                match = self.patterns[pattern_name].search(line)
                if match:
                    tau_s = int(match.group(1))
                    value = float(match.group(2))
                    # Check for unit and convert
                    if 'µs' in line or 'us' in line:
                        value *= 1000.0
                    elif 'ms' in line:
                        value *= 1e6
                    current_test['metrics'][f'mtie_{tau_s}s'] = value
                    break
            
            # Extract TDEV values
            for pattern_name in ['tdev_paren', 'tdev_bracket', 'tdev_at', 'tdev_colon']:
                match = self.patterns[pattern_name].search(line)
                if match:
                    tau_s = float(match.group(1))
                    value = float(match.group(2))
                    # Check for unit and convert
                    if 'µs' in line or 'us' in line:
                        value *= 1000.0
                    elif 'ms' in line:
                        value *= 1e6
                    current_test['metrics'][f'tdev_{tau_s}s'] = value
                    break
            
            # Extract TE statistics
            for stat_name, pattern in [
                ('mean_te_ns', self.patterns['mean_te']),
                ('std_te_ns', self.patterns['std_te']),
                ('max_te_ns', self.patterns['max_te']),
                ('min_te_ns', self.patterns['min_te']),
                ('p95_te_ns', self.patterns['p95_te']),
                ('p99_te_ns', self.patterns['p99_te']),
            ]:
                match = pattern.search(line)
                if match:
                    value = float(match.group(1))
                    # Convert to ns if needed
                    if 'µs' in line or 'us' in line:
                        value *= 1000.0
                    current_test['metrics'][stat_name] = value
            
            # Extract CSV file references
            match = self.patterns['csv_file'].search(line)
            if match:
                csv_path = match.group(1)
                if csv_path not in current_test['csv_files']:
                    current_test['csv_files'].append(csv_path)
        
        # Add last test
        if current_test:
            result['tests'].append(current_test)
        
        return result
    
    def find_csv_for_test(
        self,
        test_name: str,
        csv_files: List[str],
        logs_dir: Path = Path('logs')
    ) -> Optional[Path]:
        """
        Find the CSV file corresponding to a test.
        
        Args:
            test_name: Name of the test (e.g., 'SwClockV1.SmallAdjustment')
            csv_files: List of CSV file paths from test output
            logs_dir: Directory to search for logs
        
        Returns:
            Path to CSV file, or None if not found
        """
        # Extract test case name (after the dot)
        if '.' in test_name:
            test_case = test_name.split('.', 1)[1]
        else:
            test_case = test_name
        
        # Check explicit CSV files first
        for csv_path in csv_files:
            csv_file = Path(csv_path)
            if csv_file.exists():
                return csv_file
            
            # Try relative to logs_dir
            rel_path = logs_dir / csv_file.name
            if rel_path.exists():
                return rel_path
        
        # Search by test name pattern
        if logs_dir.exists():
            pattern = f"*{test_case}*.csv"
            matches = list(logs_dir.glob(pattern))
            
            if matches:
                # Return most recent
                return max(matches, key=lambda p: p.stat().st_mtime)
        
        return None
    
    def generate_validation_config(
        self,
        test_output_path: Path,
        output_json: Optional[Path] = None
    ) -> Dict[str, Dict[str, float]]:
        """
        Generate validation configuration from test output.
        
        Creates a JSON file mapping each test to its expected metrics,
        which can be used for validation against recomputed values.
        
        Returns:
            Dictionary: {test_name: {metric_name: value}}
        """
        parsed = self.parse_file(test_output_path)
        
        validation_config = {}
        
        for test in parsed['tests']:
            if test['status'] == 'OK' and test['metrics']:
                validation_config[test['name']] = test['metrics']
        
        if output_json:
            with open(output_json, 'w') as f:
                json.dump(validation_config, f, indent=2)
        
        return validation_config


def main():
    """CLI for test output parser"""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='Parse GTest output and extract metrics for validation'
    )
    parser.add_argument('input', type=Path, help='Test output file')
    parser.add_argument('--output', '-o', type=Path,
                       help='Output JSON file with validation config')
    parser.add_argument('--pretty', action='store_true',
                       help='Pretty-print extracted metrics')
    
    args = parser.parse_args()
    
    if not args.input.exists():
        print(f"Error: File not found: {args.input}")
        return 1
    
    parser = TestOutputParser()
    result = parser.parse_file(args.input)
    
    if args.pretty:
        print(f"\nExtracted from: {args.input}")
        print("=" * 60)
        
        for test in result['tests']:
            print(f"\n[{test['status']}] {test['name']}")
            
            if test['metrics']:
                print("  Metrics:")
                for key, value in sorted(test['metrics'].items()):
                    print(f"    {key}: {value:.1f}")
            
            if test['csv_files']:
                print("  CSV files:")
                for csv_file in test['csv_files']:
                    print(f"    {csv_file}")
    
    if args.output:
        validation_config = parser.generate_validation_config(args.input, args.output)
        print(f"\nValidation config saved to: {args.output}")
        print(f"Tests with metrics: {len(validation_config)}")
    
    if not args.pretty and not args.output:
        # Default: print JSON
        import json
        print(json.dumps(result, indent=2))
    
    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())
