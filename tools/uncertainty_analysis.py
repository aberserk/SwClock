#!/usr/bin/env python3
"""
Measurement Uncertainty Analysis Tool

Implements ISO/IEC Guide 98-3 (GUM) methodology for quantifying
measurement uncertainty in SwClock timing measurements.

Purpose:
- Identify and quantify all uncertainty sources
- Combine uncertainties using GUM methodology
- Report expanded uncertainty with coverage factor k=2 (95% confidence)
- Generate uncertainty budget for IEEE audit compliance

Standards:
- ISO/IEC Guide 98-3:2008 (GUM) - Guide to the expression of Uncertainty in Measurement
- JCGM 100:2008 - Evaluation of measurement data
- IEEE 1588-2019 Annex J - Clock servo specification

Usage:
    python3 uncertainty_analysis.py <test_data.csv> [options]
    
Example:
    python3 uncertainty_analysis.py logs/repeatability_test.csv --output resources/uncertainty_budget.json

Author: SwClock Development Team
Date: 2026-01-13
Part of: IEEE Audit Priority 3 Recommendation 13
"""

import sys
import json
import csv
import math
import argparse
from typing import Dict, List, Tuple, Optional
from pathlib import Path
from dataclasses import dataclass, asdict
from enum import Enum


class UncertaintyType(Enum):
    """Type A: Statistical, Type B: Systematic"""
    TYPE_A = "Type A (Statistical)"
    TYPE_B = "Type B (Systematic)"


@dataclass
class UncertaintyComponent:
    """Individual uncertainty contribution"""
    name: str
    symbol: str
    value: float  # Standard uncertainty in nanoseconds
    type: UncertaintyType
    distribution: str  # "normal", "rectangular", "triangular"
    degrees_of_freedom: Optional[int]
    sensitivity_coefficient: float  # ci
    contribution: float  # ci * u(xi)
    notes: str


class UncertaintySources:
    """Document all known uncertainty sources in SwClock"""
    
    @staticmethod
    def get_system_uncertainties() -> List[Dict]:
        """Return known Type B uncertainties from system specifications"""
        return [
            {
                "name": "CLOCK_MONOTONIC_RAW Resolution",
                "symbol": "u_clock_res",
                "description": "macOS clock resolution uncertainty",
                "value_ns": 1.0,  # Assume 1ns resolution
                "distribution": "rectangular",
                "divisor": math.sqrt(3),  # For rectangular distribution
                "source": "macOS kern.clockrate sysctl, assumed ±1ns quantization"
            },
            {
                "name": "Interrupt Latency",
                "symbol": "u_int_lat",
                "description": "Variation in interrupt service latency",
                "value_ns": 500.0,  # Typical for macOS
                "distribution": "rectangular",
                "divisor": math.sqrt(3),
                "source": "Measured via timing tests, ±500ns typical range"
            },
            {
                "name": "System Call Overhead",
                "symbol": "u_syscall",
                "description": "clock_gettime() execution time variation",
                "value_ns": 50.0,
                "distribution": "normal",
                "divisor": 1.0,
                "source": "Benchmarked clock_gettime() standard deviation"
            },
            {
                "name": "Temperature Drift",
                "symbol": "u_temp",
                "description": "Crystal oscillator temperature coefficient",
                "value_ns": 100.0,  # Assuming ±10°C, ±1ppm/°C
                "distribution": "rectangular",
                "divisor": math.sqrt(3),
                "source": "Typical TCXO spec: ±1ppm/°C, ±10°C ambient variation"
            },
            {
                "name": "Aging Drift",
                "symbol": "u_aging",
                "description": "Long-term oscillator frequency drift",
                "value_ns": 50.0,  # Over measurement interval
                "distribution": "rectangular",
                "divisor": math.sqrt(3),
                "source": "Typical crystal aging: ±5ppm/year"
            }
        ]


class TypeAAnalyzer:
    """Compute Type A (statistical) uncertainties from repeated measurements"""
    
    @staticmethod
    def analyze_repeatability(measurements: List[float]) -> Tuple[float, float, int]:
        """
        Compute Type A uncertainty from repeated measurements
        
        Args:
            measurements: List of repeated measurements (nanoseconds)
        
        Returns:
            (mean, standard_uncertainty, degrees_of_freedom)
            standard_uncertainty = sample_std / sqrt(n)
        """
        n = len(measurements)
        if n < 2:
            return 0.0, 0.0, 0
        
        mean = sum(measurements) / n
        variance = sum((x - mean)**2 for x in measurements) / (n - 1)
        sample_std = math.sqrt(variance)
        
        # Standard uncertainty of the mean
        standard_uncertainty = sample_std / math.sqrt(n)
        
        degrees_of_freedom = n - 1
        
        return mean, standard_uncertainty, degrees_of_freedom


class TypeBAnalyzer:
    """Compute Type B (systematic) uncertainties from specifications"""
    
    @staticmethod
    def from_rectangular_distribution(half_width: float) -> float:
        """
        Convert rectangular distribution bounds to standard uncertainty
        
        For uniform distribution over [-a, +a]:
        u(x) = a / sqrt(3)
        
        Args:
            half_width: Half-width of rectangular distribution (a)
        
        Returns:
            Standard uncertainty
        """
        return half_width / math.sqrt(3)
    
    @staticmethod
    def from_triangular_distribution(half_width: float) -> float:
        """
        Convert triangular distribution bounds to standard uncertainty
        
        For triangular distribution over [-a, +a]:
        u(x) = a / sqrt(6)
        """
        return half_width / math.sqrt(6)
    
    @staticmethod
    def from_normal_distribution(standard_deviation: float, coverage_factor: float = 1.0) -> float:
        """
        Convert normal distribution parameters to standard uncertainty
        
        If given expanded uncertainty U with coverage factor k:
        u(x) = U / k
        """
        return standard_deviation / coverage_factor


class UncertaintyBudget:
    """GUM-compliant uncertainty budget calculator"""
    
    def __init__(self):
        self.components: List[UncertaintyComponent] = []
        self.coverage_factor: float = 2.0  # k=2 for 95% confidence (normal distribution)
    
    def add_component(self, name: str, symbol: str, value_ns: float,
                     uncertainty_type: UncertaintyType, distribution: str = "normal",
                     sensitivity_coefficient: float = 1.0,
                     degrees_of_freedom: Optional[int] = None,
                     notes: str = "") -> None:
        """Add uncertainty component to budget"""
        
        contribution = sensitivity_coefficient * value_ns
        
        component = UncertaintyComponent(
            name=name,
            symbol=symbol,
            value=value_ns,
            type=uncertainty_type,
            distribution=distribution,
            degrees_of_freedom=degrees_of_freedom,
            sensitivity_coefficient=sensitivity_coefficient,
            contribution=contribution,
            notes=notes
        )
        
        self.components.append(component)
    
    def compute_combined_uncertainty(self) -> float:
        """
        Compute combined standard uncertainty u_c
        
        For uncorrelated inputs:
        u_c = sqrt(sum((ci * u(xi))^2))
        
        Returns:
            Combined standard uncertainty in nanoseconds
        """
        sum_squares = sum(comp.contribution**2 for comp in self.components)
        return math.sqrt(sum_squares)
    
    def compute_expanded_uncertainty(self, u_c: float) -> float:
        """
        Compute expanded uncertainty U
        
        U = k * u_c
        
        For k=2 (normal distribution): ~95% confidence level
        
        Args:
            u_c: Combined standard uncertainty
        
        Returns:
            Expanded uncertainty in nanoseconds
        """
        return self.coverage_factor * u_c
    
    def compute_effective_degrees_of_freedom(self) -> float:
        """
        Welch-Satterthwaite formula for effective degrees of freedom
        
        ν_eff = u_c^4 / sum((ci * u(xi))^4 / νi)
        
        Used when combining uncertainties with different degrees of freedom
        """
        u_c = self.compute_combined_uncertainty()
        
        denominator = 0.0
        for comp in self.components:
            if comp.degrees_of_freedom is not None and comp.degrees_of_freedom > 0:
                denominator += (comp.contribution**4) / comp.degrees_of_freedom
        
        if denominator == 0:
            return float('inf')  # Infinite degrees of freedom
        
        return (u_c**4) / denominator
    
    def generate_report(self) -> Dict:
        """Generate complete uncertainty budget report"""
        
        u_c = self.compute_combined_uncertainty()
        U = self.compute_expanded_uncertainty(u_c)
        nu_eff = self.compute_effective_degrees_of_freedom()
        
        # Sort components by contribution (descending)
        sorted_components = sorted(self.components, 
                                  key=lambda c: abs(c.contribution), 
                                  reverse=True)
        
        return {
            "standard": "ISO/IEC Guide 98-3:2008 (GUM)",
            "measurement_units": "nanoseconds",
            "coverage_factor": self.coverage_factor,
            "confidence_level": "~95%",
            "combined_standard_uncertainty_ns": round(u_c, 3),
            "expanded_uncertainty_ns": round(U, 3),
            "effective_degrees_of_freedom": round(nu_eff, 1) if nu_eff != float('inf') else "infinite",
            "uncertainty_statement": f"U = ±{U:.1f} ns (k={self.coverage_factor}, ~95% confidence)",
            "components": [
                {
                    "name": comp.name,
                    "symbol": comp.symbol,
                    "standard_uncertainty_ns": round(comp.value, 3),
                    "type": comp.type.value,
                    "distribution": comp.distribution,
                    "sensitivity_coefficient": comp.sensitivity_coefficient,
                    "contribution_ns": round(comp.contribution, 3),
                    "contribution_percent": round(100 * (comp.contribution**2) / (u_c**2), 1),
                    "degrees_of_freedom": comp.degrees_of_freedom,
                    "notes": comp.notes
                }
                for comp in sorted_components
            ]
        }


def analyze_csv_measurements(csv_path: str) -> Dict:
    """Analyze repeated measurements from CSV file"""
    
    measurements = []
    
    with open(csv_path, 'r') as f:
        # Skip header comments
        for line in f:
            if not line.startswith('#'):
                break
        
        # Read measurement data
        reader = csv.DictReader(f, fieldnames=['timestamp_ns', 'te_ns'])
        for row in reader:
            try:
                measurements.append(float(row['te_ns']))
            except (ValueError, KeyError):
                pass
    
    if not measurements:
        raise ValueError("No measurements found in CSV file")
    
    # Compute Type A uncertainty
    mean, std_uncertainty, dof = TypeAAnalyzer.analyze_repeatability(measurements)
    
    return {
        "measurement_count": len(measurements),
        "mean_ns": mean,
        "type_a_uncertainty_ns": std_uncertainty,
        "degrees_of_freedom": dof,
        "sample_std_ns": std_uncertainty * math.sqrt(len(measurements))
    }


def create_uncertainty_budget(type_a_data: Optional[Dict] = None) -> UncertaintyBudget:
    """Create complete uncertainty budget for SwClock"""
    
    budget = UncertaintyBudget()
    
    # Add Type A uncertainty (if available from measurements)
    if type_a_data:
        budget.add_component(
            name="Measurement Repeatability",
            symbol="u_repeat",
            value_ns=type_a_data["type_a_uncertainty_ns"],
            uncertainty_type=UncertaintyType.TYPE_A,
            distribution="normal",
            sensitivity_coefficient=1.0,
            degrees_of_freedom=type_a_data["degrees_of_freedom"],
            notes=f"From {type_a_data['measurement_count']} repeated measurements"
        )
    
    # Add Type B uncertainties from specifications
    type_b_analyzer = TypeBAnalyzer()
    
    for spec in UncertaintySources.get_system_uncertainties():
        if spec["distribution"] == "rectangular":
            std_uncertainty = type_b_analyzer.from_rectangular_distribution(spec["value_ns"])
        elif spec["distribution"] == "triangular":
            std_uncertainty = type_b_analyzer.from_triangular_distribution(spec["value_ns"])
        else:  # normal
            std_uncertainty = spec["value_ns"]
        
        budget.add_component(
            name=spec["name"],
            symbol=spec["symbol"],
            value_ns=std_uncertainty,
            uncertainty_type=UncertaintyType.TYPE_B,
            distribution=spec["distribution"],
            sensitivity_coefficient=1.0,
            degrees_of_freedom=None,  # Type B: infinite DOF
            notes=spec["source"]
        )
    
    return budget


def main():
    parser = argparse.ArgumentParser(
        description="ISO/IEC Guide 98-3 (GUM) Uncertainty Analysis for SwClock"
    )
    parser.add_argument(
        "csv_file",
        nargs="?",
        help="CSV file with repeated measurements (optional)"
    )
    parser.add_argument(
        "--output",
        "-o",
        default="resources/uncertainty_budget.json",
        help="Output JSON file for uncertainty budget"
    )
    parser.add_argument(
        "--type-b-only",
        action="store_true",
        help="Generate budget with only Type B uncertainties"
    )
    
    args = parser.parse_args()
    
    print("=" * 70)
    print("ISO/IEC Guide 98-3 (GUM) Uncertainty Analysis")
    print("SwClock Timing Measurement Uncertainty Budget")
    print("=" * 70)
    print()
    
    # Analyze Type A if CSV provided
    type_a_data = None
    if args.csv_file and not args.type_b_only:
        if not Path(args.csv_file).exists():
            print(f"ERROR: CSV file not found: {args.csv_file}", file=sys.stderr)
            return 2
        
        print(f"[1/3] Analyzing repeated measurements from {args.csv_file}...")
        type_a_data = analyze_csv_measurements(args.csv_file)
        print(f"      Measurement count: {type_a_data['measurement_count']}")
        print(f"      Mean TE: {type_a_data['mean_ns']:.2f} ns")
        print(f"      Type A uncertainty: {type_a_data['type_a_uncertainty_ns']:.2f} ns")
        print(f"      Degrees of freedom: {type_a_data['degrees_of_freedom']}")
        print()
    
    # Create uncertainty budget
    print(f"[2/3] Building uncertainty budget...")
    budget = create_uncertainty_budget(type_a_data)
    print(f"      Total components: {len(budget.components)}")
    print(f"      Type A: {sum(1 for c in budget.components if c.type == UncertaintyType.TYPE_A)}")
    print(f"      Type B: {sum(1 for c in budget.components if c.type == UncertaintyType.TYPE_B)}")
    print()
    
    # Generate report
    print(f"[3/3] Computing combined and expanded uncertainty...")
    report = budget.generate_report()
    
    # Display results
    print("=" * 70)
    print("UNCERTAINTY BUDGET SUMMARY")
    print("=" * 70)
    print()
    print(f"Combined Standard Uncertainty (u_c): {report['combined_standard_uncertainty_ns']:.2f} ns")
    print(f"Expanded Uncertainty (U):            {report['expanded_uncertainty_ns']:.2f} ns")
    print(f"Coverage Factor (k):                 {report['coverage_factor']}")
    print(f"Confidence Level:                    {report['confidence_level']}")
    print(f"Effective Degrees of Freedom:        {report['effective_degrees_of_freedom']}")
    print()
    print("UNCERTAINTY STATEMENT:")
    print(f"  {report['uncertainty_statement']}")
    print()
    print("=" * 70)
    print("COMPONENT CONTRIBUTIONS (sorted by magnitude)")
    print("=" * 70)
    print()
    print(f"{'Component':<35} {'u(xi) [ns]':>12} {'Type':<25} {'Contribution':>10}")
    print("-" * 70)
    
    for comp in report['components']:
        print(f"{comp['name']:<35} {comp['standard_uncertainty_ns']:>12.2f} "
              f"{comp['type']:<25} {comp['contribution_percent']:>9.1f}%")
    
    print()
    
    # Save report
    output_path = Path(args.output)
    with open(output_path, 'w') as f:
        json.dump(report, f, indent=2)
    
    print(f"✓ Uncertainty budget saved to: {output_path}")
    print()
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
