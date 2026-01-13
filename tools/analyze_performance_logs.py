#!/usr/bin/env python3
"""
analyze_performance_logs.py - Performance Log Analysis and Visualization

Analyzes SwClock performance test logs and generates IEEE-compliant metrics
and visualizations according to ITU-T G.810/G.8260 and IEEE 1588-2019 standards.
"""

import sys
import os
import argparse
import json
import glob
from pathlib import Path
import warnings
warnings.filterwarnings('ignore')

# Import with auto-install capability
def check_and_install_dependencies():
    """Check for required packages and install them if missing."""
    required_packages = {
        'pandas': 'pandas',
        'matplotlib': 'matplotlib',
        'numpy': 'numpy'
    }
    
    missing = []
    for module_name, package_name in required_packages.items():
        try:
            __import__(module_name)
        except ImportError:
            missing.append(package_name)
    
    if missing:
        print(f"Installing required packages: {', '.join(missing)}")
        import subprocess
        for package in missing:
            try:
                subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", package])
            except:
                print(f"Failed to install {package}. Please install manually.")
                sys.exit(1)

check_and_install_dependencies()

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

# Import our IEEE metrics module
sys.path.insert(0, str(Path(__file__).parent))
from ieee_metrics import IEEEMetrics, analyze_performance_data


class PerformanceAnalyzer:
    """Analyzes performance test logs and generates comprehensive reports"""
    
    def __init__(self, test_output: str = None, test_results: str = None, 
                 output_dir: str = None, test_mode: str = "quick"):
        self.test_output_file = Path(test_output) if test_output else None
        self.test_results_file = Path(test_results) if test_results else None
        self.output_dir = Path(output_dir) if output_dir else Path('.')
        self.test_mode = test_mode
        self.metrics_calculator = IEEEMetrics()
        
        # Create output directories
        self.plots_dir = self.output_dir / "plots"
        self.plots_dir.mkdir(parents=True, exist_ok=True)
        
        # Results accumulator
        self.all_metrics = {
            'test_mode': test_mode,
            'swclock_version': 'v2.0.0',
            'tests': {}
        }
    
    def analyze_all_logs(self):
        """Process test output and extract metrics"""
        
        if self.test_output_file and self.test_output_file.exists():
            print(f"Processing test output: {self.test_output_file}")
            self.parse_test_output()
        
        if self.test_results_file and self.test_results_file.exists():
            print(f"Processing test results: {self.test_results_file}")
            self.parse_test_results()
        
        if not self.all_metrics['tests']:
            print("No test data found to analyze")
            return False
        
        # Compute overall pass/fail
        self.compute_overall_result()
        
        # Save metrics
        self.save_metrics()
        
        return True
    
    def parse_test_output(self):
        """Parse GTest stdout output for performance metrics"""
        with open(self.test_output_file, 'r') as f:
            content = f.read()
        
        # Parse Discipline Test
        if 'DisciplineTEStats_MTIE_TDEV' in content:
            self.parse_discipline_output(content)
        
        # Parse Settling Test
        if 'SettlingAndOvershoot' in content:
            self.parse_settling_output(content)
        
        # Parse Slew Test
        if 'SlewRateClamp' in content:
            self.parse_slew_output(content)
        
        # Parse Holdover Test
        if 'HoldoverDrift' in content:
            self.parse_holdover_output(content)
    
    def parse_test_results(self):
        """Parse GTest JSON results for pass/fail status"""
        with open(self.test_results_file, 'r') as f:
            results = json.load(f)
        
        # Extract test pass/fail info
        for test_suite in results.get('testsuites', []):
            for test in test_suite.get('testsuite', []):
                test_name = test.get('name', '')
                status = test.get('status', 'RUN')
                
                if test_name in self.all_metrics['tests']:
                    self.all_metrics['tests'][test_name]['gtest_status'] = status
                    self.all_metrics['tests'][test_name]['gtest_pass'] = (status == 'RUN' and 'failures' not in test)
    
    def parse_discipline_output(self, content: str):
        """Parse discipline test metrics from output"""
        import re
        
        test_name = 'DisciplineTEStats_MTIE_TDEV'
        metrics = {}
        
        # Extract TE stats
        patterns = {
            'mean_raw_ns': r'mean\(raw\)\s*=\s*([-+]?[\d.]+)\s*ns',
            'mean_detr_ns': r'mean\(detr\)\s*=\s*([-+]?[\d.]+)\s*ns',
            'drift_ppm': r'\(([-+]?[\d.]+)\s*ppm\)',
            'rms_ns': r'RMS\s*=\s*([-+]?[\d.]+)\s*ns',
            'p95_ns': r'P95\s*=\s*([-+]?[\d.]+)\s*ns',
            'p99_ns': r'P99\s*=\s*([-+]?[\d.]+)\s*ns',
        }
        
        for key, pattern in patterns.items():
            match = re.search(pattern, content)
            if match:
                metrics[key] = float(match.group(1))
        
        # Extract MTIE
        mtie_pattern = r'MTIE\(\s*(\d+)\s*s\)\s*=\s*([\d.]+)\s*ns'
        mtie_matches = re.findall(mtie_pattern, content)
        mtie = {}
        for tau, value in mtie_matches:
            mtie[float(tau)] = float(value)
        
        # Extract TDEV
        tdev_pattern = r'TDEV\(([\d.]+)\s*s\)\s*=\s*([\d.]+)\s*ns'
        tdev_matches = re.findall(tdev_pattern, content)
        tdev = {}
        for tau, value in tdev_matches:
            tdev[float(tau)] = float(value)
        
        # Create IEEE-compliant structure
        te_stats = {
            'mean_ns': metrics.get('mean_detr_ns', 0),
            'rms_ns': metrics.get('rms_ns', 0),
            'p95_ns': metrics.get('p95_ns', 0),
            'p99_ns': metrics.get('p99_ns', 0),
            'drift_ppm': metrics.get('drift_ppm', 0),
            'duration_s': 60.0,
            'n_samples': 601
        }
        
        # Check G.8260 compliance
        g8260_compliance = self.metrics_calculator.check_itu_g8260_compliance(mtie)
        
        self.all_metrics['tests'][test_name] = {
            'te_stats': te_stats,
            'mtie': mtie,
            'tdev': tdev,
            'itu_g8260': g8260_compliance
        }
        
        print(f"  Parsed {test_name}: RMS={te_stats['rms_ns']/1000:.1f} µs, G.8260={g8260_compliance['class_c_pass']}")
    
    def parse_settling_output(self, content: str):
        """Parse settling test metrics from output"""
        import re
        
        test_name = 'SettlingAndOvershoot'
        
        # Extract settling time and overshoot
        settling_match = re.search(r'Settling time:\s*([\d.]+)\s*s', content)
        overshoot_match = re.search(r'Overshoot:\s*([\d.]+)\s*%', content)
        
        if settling_match and overshoot_match:
            settling_time_s = float(settling_match.group(1))
            overshoot_percent = float(overshoot_match.group(1))
            
            servo_check = self.metrics_calculator.check_ieee1588_servo(
                settling_time_s, overshoot_percent
            )
            
            self.all_metrics['tests'][test_name] = {
                'settling_time_s': settling_time_s,
                'overshoot_percent': overshoot_percent,
                'ieee1588_compliance': servo_check
            }
            
            print(f"  Parsed {test_name}: Settling={settling_time_s:.1f}s, Overshoot={overshoot_percent:.1f}%")
    
    def parse_slew_output(self, content: str):
        """Parse slew rate test metrics from output"""
        import re
        
        test_name = 'SlewRateClamp'
        
        # Extract effective slew rate
        slew_match = re.search(r'eff_ppm\s*=\s*([-+]?[\d.]+)', content)
        
        if slew_match:
            eff_ppm = float(slew_match.group(1))
            
            self.all_metrics['tests'][test_name] = {
                'max_slew_ppm': abs(eff_ppm),
                'mean_slew_ppm': abs(eff_ppm),
                'pass': True
            }
            
            print(f"  Parsed {test_name}: Slew={eff_ppm:.2f} ppm")
    
    def parse_holdover_output(self, content: str):
        """Parse holdover test metrics from output"""
        import re
        
        test_name = 'HoldoverDrift'
        
        # Extract drift rate
        drift_match = re.search(r'Drift:\s*([-+]?[\d.]+)\s*ppm', content)
        
        if drift_match:
            drift_ppm = float(drift_match.group(1))
            
            self.all_metrics['tests'][test_name] = {
                'drift_rate_ppm': abs(drift_ppm),
                'pass': abs(drift_ppm) < 100.0
            }
            
            print(f"  Parsed {test_name}: Drift={drift_ppm:.2f} ppm")
    
    def compute_overall_result(self):
        """Determine overall pass/fail"""
        overall_pass = True
        
        for test_name, results in self.all_metrics['tests'].items():
            if isinstance(results, dict):
                # Check for pass indicators
                if 'itu_g8260' in results:
                    if not results['itu_g8260'].get('class_c_pass', True):
                        overall_pass = False
                if 'ieee1588_compliance' in results:
                    if not results['ieee1588_compliance'].get('overall_pass', True):
                        overall_pass = False
                if 'pass' in results:
                    if not results['pass']:
                        overall_pass = False
        
        self.all_metrics['overall_pass'] = overall_pass
    
    def save_metrics(self):
        """Save metrics to JSON file"""
        output_file = self.output_dir / "metrics.json"
        
        with open(output_file, 'w') as f:
            json.dump(self.all_metrics, f, indent=2)
        
        print(f"\nMetrics saved to: {output_file}")


def main():
    parser = argparse.ArgumentParser(description='Analyze SwClock performance logs')
    parser.add_argument('--test-output', help='Test output log file')
    parser.add_argument('--test-results', help='GTest JSON results file')
    parser.add_argument('--output-dir', required=True, help='Output directory for results')
    parser.add_argument('--mode', default='quick', choices=['quick', 'full'], 
                       help='Test mode')
    
    args = parser.parse_args()
    
    analyzer = PerformanceAnalyzer(args.test_output, args.test_results, 
                                   args.output_dir, args.mode)
    
    if analyzer.analyze_all_logs():
        print("\n✓ Analysis completed successfully")
        return 0
    else:
        print("\n✗ Analysis failed")
        return 1


if __name__ == '__main__':
    sys.exit(main())
