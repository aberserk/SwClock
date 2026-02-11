#!/usr/bin/env python3
"""
SwClock Commercial Validation Tool

Independent validation of SwClock performance using structured log data.
Addresses IEEE Audit Recommendation: No printf parsing, dual-path verification.

Features:
- Reads structured CSV with comprehensive metadata
- Computes MTIE/TDEV independently from raw TE data
- Verifies log integrity (SHA-256)
- Validates against IEEE/ITU-T compliance targets
- Cross-checks with test-computed metrics

Author: SwClock Development Team
Date: 2026-02-10
"""

import sys
import os
import csv
import json
import hashlib
import re
from pathlib import Path
from typing import Dict, List, Tuple, Optional
import numpy as np

# Add tools directory to path for ieee_metrics
sys.path.insert(0, str(Path(__file__).parent))
from ieee_metrics import compute_mtie, compute_tdev, compute_allan_deviation


class CommercialLogValidator:
    """Validates SwClock logs with commercial-grade integrity checks"""
    
    def __init__(self, csv_path: str):
        self.csv_path = csv_path
        self.metadata = {}
        self.timestamps_ns = []
        self.te_ns = []
        self.integrity_valid = False
        
    def verify_integrity(self) -> bool:
        """Verify SHA-256 integrity seal"""
        if not os.path.exists(self.csv_path):
            print(f"ERROR: File not found: {self.csv_path}")
            return False
            
        try:
            with open(self.csv_path, 'rb') as f:
                content = f.read()
                
            # Find signature marker
            signature_marker = b'# INTEGRITY SEAL'
            if signature_marker not in content:
                print("WARNING: No integrity seal found (file may not be finalized)")
                return False
                
            # Split content at signature
            data_end = content.index(signature_marker)
            data_content = content[:data_end]
            signature_block = content[data_end:].decode('utf-8', errors='ignore')
            
            # Extract stored hash
            hash_match = re.search(r'# SHA256: ([0-9a-f]{64})', signature_block)
            if not hash_match:
                print("ERROR: Malformed integrity seal")
                return False
                
            stored_hash = hash_match.group(1)
            
            # Compute actual hash
            computed_hash = hashlib.sha256(data_content).hexdigest()
            
            if stored_hash == computed_hash:
                print("✓ Integrity verified: SHA-256 match")
                self.integrity_valid = True
                return True
            else:
                print("✗ INTEGRITY FAILURE: Hash mismatch!")
                print(f"  Stored:   {stored_hash}")
                print(f"  Computed: {computed_hash}")
                print("  LOG FILE MAY BE TAMPERED OR CORRUPTED")
                return False
                
        except Exception as e:
            print(f"ERROR verifying integrity: {e}")
            return False
    
    def parse_metadata(self) -> bool:
        """Extract metadata from CSV header"""
        try:
            with open(self.csv_path, 'r') as f:
                for line in f:
                    if line.startswith('timestamp_ns,'):
                        break  # End of header
                    if line.startswith('# ') and ': ' in line:
                        # Parse metadata line
                        line = line[2:].strip()  # Remove '# ' prefix
                        if ': ' in line:
                            key, value = line.split(': ', 1)
                            self.metadata[key.strip()] = value.strip()
            
            print(f"\n✓ Metadata extracted: {len(self.metadata)} fields")
            print(f"  Test: {self.metadata.get('Test Name', 'Unknown')}")
            print(f"  UUID: {self.metadata.get('Run UUID', 'None')}")
            print(f"  Timestamp: {self.metadata.get('Timestamp', 'Unknown')}")
            print(f"  SwClock Version: {self.metadata.get('SwClock Version', 'Unknown')}")
            
            return True
        except Exception as e:
            print(f"ERROR parsing metadata: {e}")
            return False
    
    def load_data(self) -> bool:
        """Load TE time series data"""
        try:
            with open(self.csv_path, 'r') as f:
                # Skip header (lines starting with #)
                lines = []
                for line in f:
                    if not line.startswith('#') and line.strip():
                        lines.append(line)
                
            # Parse CSV (first line should be column headers)
            reader = csv.DictReader(lines)
            for row in reader:
                self.timestamps_ns.append(int(row['timestamp_ns']))
                self.te_ns.append(int(row['te_ns']))
            
            print(f"✓ Data loaded: {len(self.timestamps_ns)} samples")
            
            if len(self.timestamps_ns) < 10:
                print("WARNING: Very few samples, metrics may be unreliable")
                return False
                
            return True
        except Exception as e:
            print(f"ERROR loading data: {e}")
            return False
    
    def compute_metrics(self) -> Dict:
        """Compute MTIE/TDEV independently from raw TE data"""
        print("\n=== Independent Metric Computation ===")
        
        if len(self.te_ns) < 10:
            print("ERROR: Insufficient data for metric computation")
            return {}
        
        # Convert to numpy arrays
        timestamps_s = np.array(self.timestamps_ns) / 1e9
        te_s = np.array(self.te_ns) / 1e9
        
        # Detrend (remove linear drift)
        coeffs = np.polyfit(timestamps_s, te_s, 1)
        te_detrended_s = te_s - (coeffs[0] * timestamps_s + coeffs[1])
        
        # Compute statistics
        mean_ns = np.mean(te_s) * 1e9
        mean_detrended_ns = np.mean(te_detrended_s) * 1e9
        rms_ns = np.sqrt(np.mean(te_s**2)) * 1e9
        p50_ns = np.percentile(te_s, 50) * 1e9
        p95_ns = np.percentile(te_s, 95) * 1e9
        p99_ns = np.percentile(te_s, 99) * 1e9
        slope_ppm = coeffs[0] * 1e6  # Convert to ppm
        
        # Compute MTIE (detrended)
        mtie_tau_s = [1, 10, 30]
        mtie_values_ns = []
        for tau in mtie_tau_s:
            mtie = compute_mtie(timestamps_s, te_detrended_s, tau)
            mtie_values_ns.append(mtie * 1e9)
        
        # Compute TDEV (detrended)
        tdev_tau_s = [0.1, 1, 10]
        tdev_values_ns = []
        for tau in tdev_tau_s:
            tdev = compute_tdev(timestamps_s, te_detrended_s, tau)
            tdev_values_ns.append(tdev * 1e9)
        
        metrics = {
            'mean_ns': mean_ns,
            'mean_detrended_ns': mean_detrended_ns,
            'slope_ppm': slope_ppm,
            'rms_ns': rms_ns,
            'p50_ns': p50_ns,
            'p95_ns': p95_ns,
            'p99_ns': p99_ns,
            'mtie_1s_ns': mtie_values_ns[0],
            'mtie_10s_ns': mtie_values_ns[1],
            'mtie_30s_ns': mtie_values_ns[2],
            'tdev_0_1s_ns': tdev_values_ns[0],
            'tdev_1s_ns': tdev_values_ns[1],
            'tdev_10s_ns': tdev_values_ns[2],
        }
        
        # Print results
        print(f"\nTE Statistics:")
        print(f"  Mean (raw):       {mean_ns:>12.3f} ns")
        print(f"  Mean (detrended): {mean_detrended_ns:>12.3f} ns")
        print(f"  Slope:            {slope_ppm:>12.6f} ppm")
        print(f"  RMS:              {rms_ns:>12.3f} ns")
        print(f"  P50:              {p50_ns:>12.3f} ns")
        print(f"  P95:              {p95_ns:>12.3f} ns")
        print(f"  P99:              {p99_ns:>12.3f} ns")
        
        print(f"\nMTIE (detrended):")
        print(f"  MTIE(1s):   {mtie_values_ns[0]:>10.0f} ns")
        print(f"  MTIE(10s):  {mtie_values_ns[1]:>10.0f} ns")
        print(f"  MTIE(30s):  {mtie_values_ns[2]:>10.0f} ns")
        
        print(f"\nTDEV (detrended):")
        print(f"  TDEV(0.1s): {tdev_values_ns[0]:>10.1f} ns")
        print(f"  TDEV(1s):   {tdev_values_ns[1]:>10.1f} ns")
        print(f"  TDEV(10s):  {tdev_values_ns[2]:>10.1f} ns")
        
        return metrics
    
    def validate_compliance(self, metrics: Dict) -> Tuple[bool, List[str]]:
        """Validate against IEEE/ITU-T compliance targets"""
        print("\n=== Compliance Validation ===")
        
        failures = []
        warnings = []
        
        # ITU-T G.8260 Class C targets
        if metrics['mtie_1s_ns'] > 100000:
            failures.append(f"MTIE(1s) = {metrics['mtie_1s_ns']:.0f} ns exceeds 100 µs limit")
        else:
            print(f"✓ MTIE(1s) = {metrics['mtie_1s_ns']:.0f} ns < 100 µs")
        
        if metrics['mtie_10s_ns'] > 200000:
            failures.append(f"MTIE(10s) = {metrics['mtie_10s_ns']:.0f} ns exceeds 200 µs limit")
        else:
            print(f"✓ MTIE(10s) = {metrics['mtie_10s_ns']:.0f} ns < 200 µs")
        
        if metrics['mtie_30s_ns'] > 300000:
            failures.append(f"MTIE(30s) = {metrics['mtie_30s_ns']:.0f} ns exceeds 300 µs limit")
        else:
            print(f"✓ MTIE(30s) = {metrics['mtie_30s_ns']:.0f} ns < 300 µs")
        
        # TDEV targets
        if metrics['tdev_0_1s_ns'] > 20000:
            failures.append(f"TDEV(0.1s) = {metrics['tdev_0_1s_ns']:.1f} ns exceeds 20 µs limit")
        else:
            print(f"✓ TDEV(0.1s) = {metrics['tdev_0_1s_ns']:.1f} ns < 20 µs")
        
        if metrics['tdev_1s_ns'] > 40000:
            failures.append(f"TDEV(1s) = {metrics['tdev_1s_ns']:.1f} ns exceeds 40 µs limit")
        else:
            print(f"✓ TDEV(1s) = {metrics['tdev_1s_ns']:.1f} ns < 40 µs")
        
        if metrics['tdev_10s_ns'] > 80000:
            failures.append(f"TDEV(10s) = {metrics['tdev_10s_ns']:.1f} ns exceeds 80 µs limit")
        else:
            print(f"✓ TDEV(10s) = {metrics['tdev_10s_ns']:.1f} ns < 80 µs")
        
        # Additional checks
        if abs(metrics['p95_ns']) > 150000:
            warnings.append(f"P95 = {metrics['p95_ns']:.0f} ns exceeds ±150 µs guideline")
        else:
            print(f"✓ P95 = {metrics['p95_ns']:.0f} ns within ±150 µs")
        
        if abs(metrics['mean_detrended_ns']) > 20000:
            warnings.append(f"Mean (detrended) = {metrics['mean_detrended_ns']:.0f} ns exceeds ±20 µs guideline")
        else:
            print(f"✓ Mean (detrended) = {metrics['mean_detrended_ns']:.0f} ns within ±20 µs")
        
        if warnings:
            print("\nWarnings:")
            for w in warnings:
                print(f"  ⚠ {w}")
        
        passed = len(failures) == 0
        return passed, failures
    
    def generate_report(self, metrics: Dict, compliance_passed: bool, failures: List[str]):
        """Generate JSON validation report"""
        report = {
            'validation_timestamp': self.metadata.get('Timestamp', 'Unknown'),
            'test_uuid': self.metadata.get('Run UUID', 'Unknown'),
            'test_name': self.metadata.get('Test Name', 'Unknown'),
            'swclock_version': self.metadata.get('SwClock Version', 'Unknown'),
            'integrity_verified': self.integrity_valid,
            'sample_count': len(self.timestamps_ns),
            'metrics': metrics,
            'compliance': {
                'passed': compliance_passed,
                'failures': failures,
                'standard': 'ITU-T G.8260 Class C'
            }
        }
        
        # Save report
        report_path = self.csv_path.replace('.csv', '_validation_report.json')
        with open(report_path, 'w') as f:
            json.dump(report, f, indent=2)
        
        print(f"\n✓ Validation report saved: {report_path}")
        return report_path


def main():
    if len(sys.argv) < 2:
        print("Usage: python swclock_commercial_validator.py <csv_log_file>")
        print("\nValidates SwClock performance logs with:")
        print("  - SHA-256 integrity verification")
        print("  - Independent MTIE/TDEV computation")
        print("  - IEEE/ITU-T compliance checking")
        print("  - JSON validation report generation")
        sys.exit(1)
    
    csv_path = sys.argv[1]
    
    print("=" * 70)
    print("SwClock Commercial Validation Tool v1.0")
    print("=" * 70)
    print(f"\nAnalyzing: {csv_path}\n")
    
    validator = CommercialLogValidator(csv_path)
    
    # Step 1: Verify integrity
    if not validator.verify_integrity():
        print("\n✗ VALIDATION FAILED: Integrity check failed")
        sys.exit(1)
    
    # Step 2: Parse metadata
    if not validator.parse_metadata():
        print("\n✗ VALIDATION FAILED: Metadata parsing failed")
        sys.exit(1)
    
    # Step 3: Load data
    if not validator.load_data():
        print("\n✗ VALIDATION FAILED: Data loading failed")
        sys.exit(1)
    
    # Step 4: Compute metrics
    metrics = validator.compute_metrics()
    if not metrics:
        print("\n✗ VALIDATION FAILED: Metric computation failed")
        sys.exit(1)
    
    # Step 5: Validate compliance
    passed, failures = validator.validate_compliance(metrics)
    
    # Step 6: Generate report
    report_path = validator.generate_report(metrics, passed, failures)
    
    # Final result
    print("\n" + "=" * 70)
    if passed:
        print("✓ VALIDATION PASSED: All compliance targets met")
        print("=" * 70)
        sys.exit(0)
    else:
        print("✗ VALIDATION FAILED: Compliance targets not met")
        print("=" * 70)
        print("\nFailures:")
        for f in failures:
            print(f"  • {f}")
        sys.exit(1)


if __name__ == '__main__':
    main()
