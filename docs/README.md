# SwClock Documentation

Welcome to the SwClock performance validation framework documentation.

## Documentation Index

### User Guides

- **[PERFORMANCE_TESTING.md](PERFORMANCE_TESTING.md)** - Complete guide to running and interpreting performance tests
  - Quick start instructions
  - Test modes (quick, full, regression)
  - Interpreting results and reports
  - Troubleshooting common issues
  - CI/CD integration examples

### Technical Reference

- **[METRICS_REFERENCE.md](METRICS_REFERENCE.md)** - Detailed metrics documentation
  - Time Error (TE) statistics explained
  - MTIE (Maximum Time Interval Error)
  - TDEV (Time Deviation)
  - Allan Deviation
  - ITU-T and IEEE standards compliance
  - Mathematical formulas and interpretations

### Project Documentation

Located in project root:

- **[README.md](../README.md)** - Project overview and setup
- **[TODO.txt](../TODO.txt)** - Known limitations and future work
- **[IMPLEMENTATION_GAP_ANALYSIS.md](../IMPLEMENTATION_GAP_ANALYSIS.md)** - Implementation status vs original plan
- **[PERFORMANCE_VALIDATION_PROOF.md](../PERFORMANCE_VALIDATION_PROOF.md)** - Validation results and evidence

## Quick Links

### For Developers

- [Running your first test](PERFORMANCE_TESTING.md#quick-start)
- [Understanding test output](PERFORMANCE_TESTING.md#understanding-test-results)
- [Regression testing workflow](PERFORMANCE_TESTING.md#regression-testing)
- [Troubleshooting](PERFORMANCE_TESTING.md#troubleshooting)

### For Quality Assurance

- [Standards compliance](METRICS_REFERENCE.md#compliance-thresholds)
- [Metric targets](PERFORMANCE_TESTING.md#performance-expectations)
- [ITU-T G.8260 requirements](METRICS_REFERENCE.md#itu-t-g8260-class-c)
- [IEEE 1588 requirements](METRICS_REFERENCE.md#ieee-1588-2019-annex-j)

### For System Integrators

- [CI/CD integration](PERFORMANCE_TESTING.md#cicd-integration)
- [Exit codes and automation](PERFORMANCE_TESTING.md#exit-codes)
- [Performance expectations](PERFORMANCE_TESTING.md#performance-expectations)

## Getting Help

1. **Check the guides** - Most questions are answered in PERFORMANCE_TESTING.md
2. **Review examples** - See actual test results in `performance/` directories
3. **Check TODO.txt** - Known limitations and future enhancements
4. **Review test code** - Source in `src-gtests/tests_performance.cpp`

## Standards Covered

- **ITU-T G.810** - Synchronization network terminology
- **ITU-T G.8260** - Packet-based timing (Class C validated)
- **IEEE 1588-2019** - Precision Time Protocol (Annex J)
- **IEEE 1139** - Frequency and time metrology (Allan deviation)

## Key Features

✅ Automated IEEE-compliant metrics computation  
✅ ITU-T G.8260 Class C compliance validation  
✅ Professional markdown reports  
✅ Regression detection with baseline comparison  
✅ Real-time clock discipline testing  
✅ Python-based analysis with virtual environment support  

## Document Maintenance

- **Created:** January 13, 2026
- **Last Updated:** January 13, 2026
- **Framework Version:** v1.0
- **SwClock Version:** v2.0.0

---

**Ready to get started?** → Begin with [PERFORMANCE_TESTING.md](PERFORMANCE_TESTING.md)
