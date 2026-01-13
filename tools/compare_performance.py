#!/usr/bin/env python3
"""
compare_performance.py - Regression Testing Tool

Compares current performance metrics against a baseline to detect regressions.
"""

import sys
import json
import argparse
from pathlib import Path
from datetime import datetime


class PerformanceComparator:
    """Compares performance metrics for regression testing"""
    
    def __init__(self, baseline_file: str, current_file: str, output_file: str):
        self.baseline_file = Path(baseline_file)
        self.current_file = Path(current_file)
        self.output_file = Path(output_file)
        
        # Load metrics
        with open(self.baseline_file, 'r') as f:
            self.baseline = json.load(f)
        
        with open(self.current_file, 'r') as f:
            self.current = json.load(f)
        
        self.regressions = []
        self.improvements = []
        self.threshold = 0.10  # 10% degradation threshold
    
    def compare(self):
        """Perform comparison and generate report"""
        report = []
        
        # Header
        report.append(self._generate_header())
        
        # Summary
        report.append(self._generate_summary())
        
        # Detailed comparison
        report.append(self._generate_comparison())
        
        # Conclusion
        report.append(self._generate_conclusion())
        
        # Write report
        with open(self.output_file, 'w') as f:
            f.write('\n'.join(report))
        
        print(f"Regression report generated: {self.output_file}")
        
        # Return exit code (0 if no regressions, 1 if regressions found)
        return 0 if len(self.regressions) == 0 else 1
    
    def _generate_header(self) -> str:
        """Generate report header"""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        return f"""# Performance Regression Report

**Generated**: {timestamp}  
**Baseline**: {self.baseline_file.parent.name}  
**Current**: {self.current_file.parent.name}

---
"""
    
    def _generate_summary(self) -> str:
        """Generate comparison summary"""
        # Compare overall results
        baseline_pass = self.baseline.get('overall_pass', False)
        current_pass = self.current.get('overall_pass', False)
        
        summary = "## Summary\n\n"
        
        if baseline_pass and current_pass:
            summary += "✅ Both baseline and current versions PASS all tests.\n\n"
        elif not baseline_pass and current_pass:
            summary += "✅ **IMPROVEMENT**: Current version PASSES (baseline failed).\n\n"
        elif baseline_pass and not current_pass:
            summary += "❌ **REGRESSION**: Current version FAILS (baseline passed).\n\n"
        else:
            summary += "❌ Both baseline and current versions FAIL tests.\n\n"
        
        return summary
    
    def _generate_comparison(self) -> str:
        """Generate detailed metric comparison"""
        section = "## Detailed Comparison\n\n"
        
        # Compare each test
        baseline_tests = self.baseline.get('tests', {})
        current_tests = self.current.get('tests', {})
        
        all_tests = set(baseline_tests.keys()) | set(current_tests.keys())
        
        for test_name in sorted(all_tests):
            section += f"### {test_name}\n\n"
            
            if test_name not in baseline_tests:
                section += "⚠️ Test not present in baseline\n\n"
                continue
            
            if test_name not in current_tests:
                section += "⚠️ Test not present in current version\n\n"
                continue
            
            baseline_result = baseline_tests[test_name]
            current_result = current_tests[test_name]
            
            # Compare based on test type
            if 'te_stats' in baseline_result and 'te_stats' in current_result:
                section += self._compare_discipline_test(baseline_result, current_result)
            elif 'settling_time_s' in baseline_result and 'settling_time_s' in current_result:
                section += self._compare_settling_test(baseline_result, current_result)
            
            section += "\n"
        
        return section
    
    def _compare_discipline_test(self, baseline: dict, current: dict) -> str:
        """Compare discipline test results"""
        output = "| Metric | Baseline | Current | Change | Status |\n"
        output += "|--------|----------|---------|--------|--------|\n"
        
        # TE Stats
        baseline_te = baseline['te_stats']
        current_te = current['te_stats']
        
        metrics = [
            ('RMS TE (µs)', 'rms_ns', 1000, False),
            ('P99 TE (µs)', 'p99_ns', 1000, False),
            ('Drift (ppm)', 'drift_ppm', 1, True)
        ]
        
        for name, key, scale, abs_val in metrics:
            baseline_val = baseline_te[key] / scale
            current_val = current_te[key] / scale
            
            if abs_val:
                baseline_val = abs(baseline_val)
                current_val = abs(current_val)
            
            change = ((current_val - baseline_val) / baseline_val * 100) if baseline_val != 0 else 0
            
            if abs(change) > self.threshold * 100:
                if current_val > baseline_val:
                    status = "⚠️ Degraded"
                    self.regressions.append(f"{name}: +{change:.1f}%")
                else:
                    status = "✅ Improved"
                    self.improvements.append(f"{name}: {change:.1f}%")
            else:
                status = "✓ Similar"
            
            output += f"| {name} | {baseline_val:.2f} | {current_val:.2f} | {change:+.1f}% | {status} |\n"
        
        return output
    
    def _compare_settling_test(self, baseline: dict, current: dict) -> str:
        """Compare settling test results"""
        output = "| Metric | Baseline | Current | Change | Status |\n"
        output += "|--------|----------|---------|--------|--------|\n"
        
        metrics = [
            ('Settling Time (s)', 'settling_time_s'),
            ('Overshoot (%)', 'overshoot_percent')
        ]
        
        for name, key in metrics:
            baseline_val = baseline[key]
            current_val = current[key]
            
            change = ((current_val - baseline_val) / baseline_val * 100) if baseline_val != 0 else 0
            
            if abs(change) > self.threshold * 100:
                if current_val > baseline_val:
                    status = "⚠️ Degraded"
                    self.regressions.append(f"{name}: +{change:.1f}%")
                else:
                    status = "✅ Improved"
                    self.improvements.append(f"{name}: {change:.1f}%")
            else:
                status = "✓ Similar"
            
            output += f"| {name} | {baseline_val:.2f} | {current_val:.2f} | {change:+.1f}% | {status} |\n"
        
        return output
    
    def _generate_conclusion(self) -> str:
        """Generate conclusion"""
        conclusion = "## Conclusion\n\n"
        
        if len(self.regressions) == 0:
            conclusion += "✅ **No significant performance regressions detected.**\n\n"
            
            if len(self.improvements) > 0:
                conclusion += "Notable improvements:\n"
                for improvement in self.improvements:
                    conclusion += f"- {improvement}\n"
                conclusion += "\n"
        else:
            conclusion += "❌ **Performance regressions detected:**\n\n"
            for regression in self.regressions:
                conclusion += f"- {regression}\n"
            conclusion += "\n"
            
            conclusion += "**Recommendation**: Investigate the root cause of performance degradation.\n\n"
        
        conclusion += f"*Regression threshold: {self.threshold * 100:.0f}% change*\n"
        
        return conclusion


def main():
    parser = argparse.ArgumentParser(description='Compare performance metrics')
    parser.add_argument('--baseline', required=True, help='Baseline metrics JSON')
    parser.add_argument('--current', required=True, help='Current metrics JSON')
    parser.add_argument('--output', required=True, help='Output markdown file')
    
    args = parser.parse_args()
    
    comparator = PerformanceComparator(args.baseline, args.current, args.output)
    return comparator.compare()


if __name__ == '__main__':
    sys.exit(main())
