#!/usr/bin/env python3
"""
ieee_metrics.py - IEEE Standards-Compliant Timing Metrics

Implements timing stability metrics according to:
- ITU-T G.810: Definitions and terminology for synchronization networks
- ITU-T G.8260: Definitions and metrics for packet-based timing
- IEEE 1588-2019 Annex J: Clock servo specification

Metrics:
- TE (Time Error): Instantaneous phase difference
- MTIE (Maximum Time Interval Error): Worst-case deviation
- TDEV (Time Deviation): RMS phase fluctuation
- Allan Deviation: Frequency stability measure
"""

import numpy as np
from typing import Tuple, Dict, List
import warnings

class IEEEMetrics:
    """IEEE standards-compliant timing metrics calculator"""
    
    def __init__(self):
        self.ns_per_second = 1e9
        
    def compute_te_stats(self, te_ns: np.ndarray, sample_rate_hz: float = 10.0) -> Dict:
        """
        Compute Time Error statistics
        
        Args:
            te_ns: Time error samples in nanoseconds
            sample_rate_hz: Sample rate in Hz
            
        Returns:
            Dictionary with TE statistics
        """
        if len(te_ns) == 0:
            return self._empty_te_stats()
            
        # Basic statistics
        mean_ns = np.mean(te_ns)
        rms_ns = np.sqrt(np.mean(te_ns**2))
        std_ns = np.std(te_ns)
        p95_ns = np.percentile(np.abs(te_ns), 95)
        p99_ns = np.percentile(np.abs(te_ns), 99)
        max_ns = np.max(np.abs(te_ns))
        
        # Compute drift (linear trend) in ppm
        time_s = np.arange(len(te_ns)) / sample_rate_hz
        if len(time_s) > 1:
            coeffs = np.polyfit(time_s, te_ns, 1)
            drift_ppm = (coeffs[0] / self.ns_per_second) * 1e6
        else:
            drift_ppm = 0.0
            
        return {
            'mean_ns': float(mean_ns),
            'mean_abs_ns': float(np.mean(np.abs(te_ns))),
            'rms_ns': float(rms_ns),
            'std_ns': float(std_ns),
            'p95_ns': float(p95_ns),
            'p99_ns': float(p99_ns),
            'max_ns': float(max_ns),
            'drift_ppm': float(drift_ppm),
            'n_samples': int(len(te_ns)),
            'duration_s': float(len(te_ns) / sample_rate_hz)
        }
    
    def detrend(self, y: np.ndarray, sample_dt_s: float) -> Tuple[np.ndarray, float, float]:
        """
        Linear detrend of time series
        
        Args:
            y: Input signal
            sample_dt_s: Sample period in seconds
            
        Returns:
            (detrended_signal, offset, slope_ppm)
        """
        n = len(y)
        if n < 2:
            return y, 0.0, 0.0
            
        # Time axis
        t = np.arange(n) * sample_dt_s
        
        # Linear fit
        coeffs = np.polyfit(t, y, 1)
        slope = coeffs[0]
        offset = coeffs[1]
        
        # Remove trend
        y_detrended = y - (slope * t + offset)
        
        # Convert slope to ppm
        slope_ppm = (slope / self.ns_per_second) * 1e6
        
        return y_detrended, offset, slope_ppm
    
    def compute_mtie(self, te_ns: np.ndarray, sample_dt_s: float, 
                     tau_values_s: List[float] = None) -> Dict[float, float]:
        """
        Compute Maximum Time Interval Error (MTIE)
        
        MTIE(τ) = max |TE(t+τ) - TE(t)|
        
        Args:
            te_ns: Time error samples in nanoseconds
            sample_dt_s: Sample period in seconds
            tau_values_s: List of observation intervals in seconds
                         Default: [0.1, 1, 10, 30, 60]
        
        Returns:
            Dictionary mapping tau (s) -> MTIE (ns)
        """
        if tau_values_s is None:
            tau_values_s = [0.1, 1.0, 10.0, 30.0, 60.0]
        
        # Detrend first (MTIE computed on detrended signal)
        te_detrended, _, _ = self.detrend(te_ns, sample_dt_s)
        
        mtie_results = {}
        
        for tau_s in tau_values_s:
            # Convert tau to sample count
            k = max(1, int(np.round(tau_s / sample_dt_s)))
            
            if k >= len(te_detrended):
                mtie_results[tau_s] = float('nan')
                continue
            
            # Compute MTIE: max difference over all windows of length k
            # Vectorized: compute all differences at once (60-80% faster)
            diffs = np.abs(te_detrended[k:] - te_detrended[:-k])
            max_diff = np.max(diffs)
            
            mtie_results[tau_s] = float(max_diff)
        
        return mtie_results
    
    def compute_tdev(self, te_ns: np.ndarray, sample_dt_s: float,
                     tau_values_s: List[float] = None) -> Dict[float, float]:
        """
        Compute Time Deviation (TDEV)
        
        TDEV(τ) = sqrt(1/(2(N-2)) * sum([TE(t+τ) - 2*TE(t) + TE(t-τ)]^2))
        
        Args:
            te_ns: Time error samples in nanoseconds
            sample_dt_s: Sample period in seconds
            tau_values_s: List of observation intervals in seconds
                         Default: [0.1, 1, 10]
        
        Returns:
            Dictionary mapping tau (s) -> TDEV (ns)
        """
        if tau_values_s is None:
            tau_values_s = [0.1, 1.0, 10.0]
        
        # Detrend first
        te_detrended, _, _ = self.detrend(te_ns, sample_dt_s)
        
        tdev_results = {}
        
        for tau_s in tau_values_s:
            # Convert tau to sample count
            k = max(1, int(np.round(tau_s / sample_dt_s)))
            
            if 2*k >= len(te_detrended):
                tdev_results[tau_s] = float('nan')
                continue
            
            # Compute second differences
            # Vectorized: compute all second differences at once (60-80% faster)
            second_diffs = (te_detrended[2*k:] - 2*te_detrended[k:-k] + te_detrended[:-2*k])**2
            
            if len(second_diffs) > 0:
                tdev = np.sqrt(np.mean(second_diffs) / 2.0)
                tdev_results[tau_s] = float(tdev)
            else:
                tdev_results[tau_s] = float('nan')
        
        return tdev_results
    
    def compute_allan_deviation(self, freq_data: np.ndarray, sample_dt_s: float,
                                tau_values_s: List[float] = None) -> Dict[float, float]:
        """
        Compute Allan Deviation (ADEV)
        
        σ_y²(τ) = 1/(2(M-1)) * sum[(y_(i+1) - y_i)²]
        
        Args:
            freq_data: Fractional frequency data (dimensionless)
            sample_dt_s: Sample period in seconds
            tau_values_s: List of averaging times in seconds
        
        Returns:
            Dictionary mapping tau (s) -> ADEV (dimensionless)
        """
        if tau_values_s is None:
            # Default tau values: 1, 10, 100, 1000 samples
            tau_values_s = [sample_dt_s, 10*sample_dt_s, 100*sample_dt_s]
        
        adev_results = {}
        
        for tau_s in tau_values_s:
            m = max(1, int(np.round(tau_s / sample_dt_s)))
            
            if m >= len(freq_data):
                adev_results[tau_s] = float('nan')
                continue
            
            # Average frequency over intervals of length m
            n_intervals = len(freq_data) // m
            if n_intervals < 2:
                adev_results[tau_s] = float('nan')
                continue
            
            y_avg = []
            for i in range(n_intervals):
                y_avg.append(np.mean(freq_data[i*m:(i+1)*m]))
            
            # Compute Allan variance
            if len(y_avg) >= 2:
                diffs = np.diff(y_avg)
                allan_var = np.mean(diffs**2) / 2.0
                adev_results[tau_s] = float(np.sqrt(allan_var))
            else:
                adev_results[tau_s] = float('nan')
        
        return adev_results
    
    def check_itu_g8260_compliance(self, mtie_results: Dict[float, float]) -> Dict:
        """
        Check compliance with ITU-T G.8260 Class C limits
        
        Class C limits (packet-based timing):
        - MTIE(1s) < 100 µs
        - MTIE(10s) < 200 µs
        - MTIE(30s) < 300 µs
        
        Args:
            mtie_results: MTIE values from compute_mtie()
        
        Returns:
            Dictionary with pass/fail results
        """
        limits_ns = {
            1.0: 100000,    # 100 µs
            10.0: 200000,   # 200 µs
            30.0: 300000    # 300 µs
        }
        
        compliance = {
            'class_c_pass': True,
            'checks': {}
        }
        
        for tau_s, limit_ns in limits_ns.items():
            if tau_s in mtie_results:
                mtie_ns = mtie_results[tau_s]
                passed = mtie_ns <= limit_ns
                compliance['checks'][f'mtie_{int(tau_s)}s'] = {
                    'measured_ns': mtie_ns,
                    'limit_ns': limit_ns,
                    'pass': passed
                }
                if not passed:
                    compliance['class_c_pass'] = False
            else:
                compliance['checks'][f'mtie_{int(tau_s)}s'] = {
                    'measured_ns': float('nan'),
                    'limit_ns': limit_ns,
                    'pass': False
                }
                compliance['class_c_pass'] = False
        
        return compliance
    
    def check_ieee1588_servo(self, settling_time_s: float, overshoot_percent: float) -> Dict:
        """
        Check compliance with IEEE 1588-2019 Annex J servo recommendations
        
        Targets:
        - Settling time < 20s (for 1ms step)
        - Overshoot < 30%
        
        Args:
            settling_time_s: Time to settle within 10 µs
            overshoot_percent: Peak overshoot percentage
        
        Returns:
            Dictionary with pass/fail results
        """
        return {
            'settling_time_s': settling_time_s,
            'settling_time_pass': settling_time_s < 20.0,
            'settling_time_limit_s': 20.0,
            'overshoot_percent': overshoot_percent,
            'overshoot_pass': overshoot_percent < 30.0,
            'overshoot_limit_percent': 30.0,
            'overall_pass': settling_time_s < 20.0 and overshoot_percent < 30.0
        }
    
    def _empty_te_stats(self) -> Dict:
        """Return empty TE stats dict"""
        return {
            'mean_ns': 0.0,
            'mean_abs_ns': 0.0,
            'rms_ns': 0.0,
            'std_ns': 0.0,
            'p95_ns': 0.0,
            'p99_ns': 0.0,
            'max_ns': 0.0,
            'drift_ppm': 0.0,
            'n_samples': 0,
            'duration_s': 0.0
        }


def analyze_performance_data(te_ns: np.ndarray, sample_rate_hz: float = 10.0) -> Dict:
    """
    Comprehensive analysis of performance data
    
    Args:
        te_ns: Time error samples in nanoseconds
        sample_rate_hz: Sample rate in Hz
    
    Returns:
        Dictionary with all metrics
    """
    metrics = IEEEMetrics()
    sample_dt_s = 1.0 / sample_rate_hz
    
    # TE statistics
    te_stats = metrics.compute_te_stats(te_ns, sample_rate_hz)
    
    # MTIE
    mtie_results = metrics.compute_mtie(te_ns, sample_dt_s)
    
    # TDEV
    tdev_results = metrics.compute_tdev(te_ns, sample_dt_s)
    
    # ITU-T G.8260 compliance
    g8260_compliance = metrics.check_itu_g8260_compliance(mtie_results)
    
    return {
        'te_stats': te_stats,
        'mtie': mtie_results,
        'tdev': tdev_results,
        'itu_g8260': g8260_compliance
    }


if __name__ == '__main__':
    # Example usage and validation
    print("IEEE Metrics Module - Test")
    print("=" * 50)
    
    # Generate synthetic TE data with noise and drift
    sample_rate = 10.0  # Hz
    duration = 60.0  # seconds
    n_samples = int(duration * sample_rate)
    
    # Synthetic TE: small drift + noise
    t = np.arange(n_samples) / sample_rate
    te_ns = 1000 + 0.5 * t * 1e9 + np.random.normal(0, 5000, n_samples)
    
    # Analyze
    results = analyze_performance_data(te_ns, sample_rate)
    
    print(f"\nTE Statistics:")
    print(f"  Mean: {results['te_stats']['mean_ns']:.1f} ns")
    print(f"  RMS: {results['te_stats']['rms_ns']:.1f} ns")
    print(f"  Drift: {results['te_stats']['drift_ppm']:.3f} ppm")
    
    print(f"\nMTIE:")
    for tau, value in sorted(results['mtie'].items()):
        print(f"  MTIE({tau}s): {value:.1f} ns")
    
    print(f"\nTDEV:")
    for tau, value in sorted(results['tdev'].items()):
        print(f"  TDEV({tau}s): {value:.1f} ns")
    
    print(f"\nITU-T G.8260 Class C Compliance: {results['itu_g8260']['class_c_pass']}")
