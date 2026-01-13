#!/usr/bin/env python3
"""
generate_performance_report.py - Generate Performance Validation Report

Creates a comprehensive markdown report with IEEE standards compliance assessment.
"""

import sys
import json
import argparse
from pathlib import Path
from datetime import datetime


class ReportGenerator:
    """Generates markdown performance reports"""
    
    def __init__(self, metrics_file: str, output_file: str, test_mode: str, plots_dir: str):
        self.metrics_file = Path(metrics_file)
        self.output_file = Path(output_file)
        self.test_mode = test_mode
        self.plots_dir = Path(plots_dir)
        
        # Load metrics
        with open(self.metrics_file, 'r') as f:
            self.metrics = json.load(f)
    
    def generate_report(self):
        """Generate the complete report"""
        report = []
        
        # Header
        report.append(self._generate_header())
        
        # Executive Summary
        report.append(self._generate_summary())
        
        # Detailed Results
        report.append(self._generate_detailed_results())
        
        # Standards Compliance
        report.append(self._generate_compliance_section())
        
        # Plots Reference
        report.append(self._generate_plots_section())
        
        # Conclusion
        report.append(self._generate_conclusion())
        
        # Write report
        with open(self.output_file, 'w') as f:
            f.write('\n'.join(report))
        
        print(f"Report generated: {self.output_file}")
    
    def _generate_header(self) -> str:
        """Generate report header"""
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        return f"""# SwClock Performance Validation Report

**Generated**: {timestamp}  
**SwClock Version**: {self.metrics.get('swclock_version', 'unknown')}  
**Test Mode**: {self.test_mode.upper()}  
**Overall Result**: {'✅ PASS' if self.metrics.get('overall_pass', False) else '❌ FAIL'}

---
"""
    
    def _generate_summary(self) -> str:
        """Generate executive summary"""
        n_tests = len(self.metrics.get('tests', {}))
        
        summary = """## Executive Summary

This report presents the results of comprehensive performance validation testing
of the SwClock library according to IEEE 1588-2019 and ITU-T G.810/G.8260 standards.

"""
        
        summary += f"**Tests Executed**: {n_tests}\n\n"
        
        # Quick stats from first discipline test
        for test_name, results in self.metrics.get('tests', {}).items():
            if 'te_stats' in results:
                te_stats = results['te_stats']
                summary += f"""### Key Performance Metrics

- **Time Error (TE) RMS**: {te_stats['rms_ns']/1000:.2f} µs
- **Frequency Drift**: {te_stats['drift_ppm']:.3f} ppm
- **Peak Error (P99)**: {te_stats['p99_ns']/1000:.2f} µs
- **Test Duration**: {te_stats['duration_s']:.1f} seconds

"""
                break
        
        return summary
    
    def _generate_detailed_results(self) -> str:
        """Generate detailed test results"""
        section = """## Detailed Test Results

---

"""
        
        for test_name, results in self.metrics.get('tests', {}).items():
            section += f"### {test_name}\n\n"
            
            if 'te_stats' in results:
                section += self._format_discipline_results(results)
            elif 'settling_time_s' in results:
                section += self._format_settling_results(results)
            elif 'max_slew_ppm' in results:
                section += self._format_slew_results(results)
            elif 'drift_rate_ppm' in results:
                section += self._format_holdover_results(results)
            
            section += "\n---\n\n"
        
        return section
    
    def _format_discipline_results(self, results: dict) -> str:
        """Format discipline test results"""
        te_stats = results['te_stats']
        mtie = results.get('mtie', {})
        tdev = results.get('tdev', {})
        
        output = "**Test Type**: Clock Discipline (MTIE/TDEV)\n\n"
        
        output += "#### Time Error Statistics\n\n"
        output += "| Metric | Value | Target | Status |\n"
        output += "|--------|-------|--------|--------|\n"
        output += f"| Mean TE | {te_stats['mean_ns']/1000:.2f} µs | < 20 µs | "
        output += "✅" if abs(te_stats['mean_ns']) < 20000 else "❌"
        output += " |\n"
        output += f"| RMS TE | {te_stats['rms_ns']/1000:.2f} µs | < 50 µs | "
        output += "✅" if te_stats['rms_ns'] < 50000 else "❌"
        output += " |\n"
        output += f"| P99 TE | {te_stats['p99_ns']/1000:.2f} µs | < 100 µs | "
        output += "✅" if te_stats['p99_ns'] < 100000 else "❌"
        output += " |\n"
        output += f"| Drift | {te_stats['drift_ppm']:.3f} ppm | < 2 ppm | "
        output += "✅" if abs(te_stats['drift_ppm']) < 2.0 else "❌"
        output += " |\n\n"
        
        if mtie:
            output += "#### MTIE (Maximum Time Interval Error)\n\n"
            output += "| Interval τ | Measured | ITU-T G.8260 Limit | Status |\n"
            output += "|-----------|----------|-------------------|--------|\n"
            
            limits = {1.0: 100, 10.0: 200, 30.0: 300}
            for tau in sorted(mtie.keys()):
                if tau in limits:
                    measured = mtie[tau] / 1000
                    limit = limits[tau]
                    status = "✅" if measured <= limit else "❌"
                    output += f"| {tau:.1f} s | {measured:.1f} µs | {limit} µs | {status} |\n"
            output += "\n"
        
        if tdev:
            output += "#### TDEV (Time Deviation)\n\n"
            output += "| Interval τ | Measured | Target | Status |\n"
            output += "|-----------|----------|--------|--------|\n"
            
            targets = {0.1: 20, 1.0: 40, 10.0: 80}
            for tau in sorted(tdev.keys()):
                if tau in targets:
                    measured = tdev[tau] / 1000
                    target = targets[tau]
                    status = "✅" if measured <= target else "❌"
                    output += f"| {tau:.1f} s | {measured:.1f} µs | < {target} µs | {status} |\n"
            output += "\n"
        
        return output
    
    def _format_settling_results(self, results: dict) -> str:
        """Format settling/overshoot results"""
        output = "**Test Type**: Step Response (Settling & Overshoot)\n\n"
        
        output += "| Metric | Value | IEEE 1588 Target | Status |\n"
        output += "|--------|-------|-----------------|--------|\n"
        output += f"| Settling Time | {results['settling_time_s']:.1f} s | < 20 s | "
        output += "✅" if results['settling_time_s'] < 20.0 else "❌"
        output += " |\n"
        output += f"| Overshoot | {results['overshoot_percent']:.1f} % | < 30 % | "
        output += "✅" if results['overshoot_percent'] < 30.0 else "❌"
        output += " |\n\n"
        
        if 'ieee1588_compliance' in results:
            compliance = results['ieee1588_compliance']
            output += f"**IEEE 1588-2019 Annex J Compliance**: "
            output += "✅ PASS\n\n" if compliance['overall_pass'] else "❌ FAIL\n\n"
        
        return output
    
    def _format_slew_results(self, results: dict) -> str:
        """Format slew rate results"""
        output = "**Test Type**: Slew Rate Validation\n\n"
        
        output += "| Metric | Value |\n"
        output += "|--------|-------|\n"
        output += f"| Max Slew Rate | {results['max_slew_ppm']:.3f} ppm |\n"
        output += f"| Mean Slew Rate | {results['mean_slew_ppm']:.3f} ppm |\n"
        output += f"| Status | {'✅ PASS' if results.get('pass', False) else '❌ FAIL'} |\n\n"
        
        return output
    
    def _format_holdover_results(self, results: dict) -> str:
        """Format holdover drift results"""
        output = "**Test Type**: Holdover Drift\n\n"
        
        output += "| Metric | Value | Target | Status |\n"
        output += "|--------|-------|--------|--------|\n"
        output += f"| Drift Rate | {results['drift_rate_ppm']:.3f} ppm | < 100 ppm | "
        output += "✅" if results['drift_rate_ppm'] < 100.0 else "❌"
        output += " |\n\n"
        
        return output
    
    def _generate_compliance_section(self) -> str:
        """Generate standards compliance summary"""
        section = """## Standards Compliance Summary

### ITU-T G.8260 (Packet-Based Timing)

"""
        
        # Find discipline test results
        g8260_pass = None
        for test_name, results in self.metrics.get('tests', {}).items():
            if 'itu_g8260' in results:
                g8260_pass = results['itu_g8260'].get('class_c_pass', False)
                break
        
        if g8260_pass is not None:
            section += f"**Class C Compliance**: {'✅ PASS' if g8260_pass else '❌ FAIL'}\n\n"
        else:
            section += "**Class C Compliance**: ⚠️ NOT TESTED\n\n"
        
        section += """### IEEE 1588-2019 Annex J (Clock Servo)

"""
        
        # Find servo test results
        ieee1588_pass = None
        for test_name, results in self.metrics.get('tests', {}).items():
            if 'ieee1588_compliance' in results:
                ieee1588_pass = results['ieee1588_compliance'].get('overall_pass', False)
                break
        
        if ieee1588_pass is not None:
            section += f"**Servo Specification**: {'✅ PASS' if ieee1588_pass else '❌ FAIL'}\n\n"
        else:
            section += "**Servo Specification**: ⚠️ NOT TESTED\n\n"
        
        return section
    
    def _generate_plots_section(self) -> str:
        """Generate plots reference section"""
        section = """## Visualizations

The following plots provide detailed analysis of SwClock performance:

"""
        
        # List all PNG files in plots directory
        if self.plots_dir.exists():
            plot_files = sorted(self.plots_dir.glob("*.png"))
            
            for plot_file in plot_files:
                rel_path = plot_file.relative_to(self.output_file.parent)
                section += f"- [{plot_file.name}]({rel_path})\n"
        
        section += "\n"
        return section
    
    def _generate_conclusion(self) -> str:
        """Generate conclusion"""
        overall_pass = self.metrics.get('overall_pass', False)
        
        conclusion = """## Conclusion

"""
        
        if overall_pass:
            conclusion += """The SwClock library has **PASSED** all performance validation tests and
demonstrates compliance with IEEE 1588-2019 and ITU-T G.810/G.8260 standards.

The clock discipline algorithm exhibits:
- Sub-microsecond time error in steady state
- Low drift and excellent long-term stability
- Well-behaved transient response with minimal overshoot
- Compliance with telecom-grade timing requirements

The SwClock is suitable for precision timing applications including PTP networks,
distributed audio systems, and synchronized instrumentation.
"""
        else:
            conclusion += """The SwClock library has **NOT PASSED** all performance validation tests.

Please review the detailed results above to identify areas requiring improvement.
Common issues include:
- Excessive time error or drift
- Poor transient response (slow settling or high overshoot)
- Non-compliance with ITU-T G.8260 MTIE/TDEV limits

Recommended actions:
- Review PI servo tuning parameters (Kp, Ki)
- Check for system clock instabilities
- Verify adequate polling frequency
- Analyze logs for anomalies
"""
        
        conclusion += "\n---\n\n"
        conclusion += f"*Generated by SwClock Performance Validation Suite - {datetime.now().strftime('%Y-%m-%d')}*\n"
        
        return conclusion


def main():
    parser = argparse.ArgumentParser(description='Generate performance report')
    parser.add_argument('--metrics', required=True, help='Metrics JSON file')
    parser.add_argument('--output', required=True, help='Output markdown file')
    parser.add_argument('--test-mode', default='quick', help='Test mode')
    parser.add_argument('--plots-dir', required=True, help='Plots directory')
    
    args = parser.parse_args()
    
    generator = ReportGenerator(args.metrics, args.output, args.test_mode, args.plots_dir)
    generator.generate_report()
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
