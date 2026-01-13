# SwClock Documentation

Welcome to the SwClock performance validation framework documentation.

## Documentation Index

### User Guides

- **[USER_GUIDE.md](USER_GUIDE.md)** - Comprehensive user guide for SwClock testing
  - Getting started with your first test
  - Interpreting test results (TE, MTIE, TDEV, ADEV)
  - Common performance issues and diagnosis
  - Troubleshooting guide (build, Python, test execution)
  - Best practices (quick vs full mode, thresholds, baselines)
  - Working with CSV data and analysis
  - Understanding compliance levels

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

- **[LINUX_COMPATIBILITY.md](LINUX_COMPATIBILITY.md)** - Linux compatibility reference
  - API compatibility matrix (adjtimex modes, status, fields)
  - Performance comparison: macOS vs Linux
  - Cross-platform code guidelines
  - Known differences and limitations
  - Testing on Linux recommendations

- **[STANDARDS_REFERENCE.md](STANDARDS_REFERENCE.md)** - Standards and compliance reference
  - IEEE 1588-2019 (PTPv2) requirements
  - ITU-T G.810 synchronization networks
  - ITU-T G.8260 packet timing (Classes A/B/C)
  - ITU-T G.8271.1 (5G timing)
  - IEEE 802.1AS (gPTP/TSN)
  - SwClock compliance matrix
  - Granular compliance level reporting

### Project Documentation

Located in project root:

- **[README.md](../README.md)** - Project overview and setup
- **[TODO.txt](../TODO.txt)** - Known limitations and future work

## Quick Links

### For Developers

- [Getting started - First test run](USER_GUIDE.md#getting-started)
- [Interpreting test results](USER_GUIDE.md#interpreting-test-results)
- [Common performance issues](USER_GUIDE.md#common-performance-issues)
- [Troubleshooting guide](USER_GUIDE.md#troubleshooting)
- [Running your first test](PERFORMANCE_TESTING.md#quick-start)
- [Understanding test output](PERFORMANCE_TESTING.md#understanding-test-results)
- [Regression testing workflow](PERFORMANCE_TESTING.md#regression-testing)

### For Quality Assurance

- [Understanding compliance levels](USER_GUIDE.md#understanding-compliance-levels)
- [Best practices for testing](USER_GUIDE.md#best-practices)
- [Standards compliance matrix](STANDARDS_REFERENCE.md#swclock-compliance-matrix)
- [Standards compliance](METRICS_REFERENCE.md#compliance-thresholds)
- [Metric targets](PERFORMANCE_TESTING.md#performance-expectations)
- [ITU-T G.8260 requirements](METRICS_REFERENCE.md#itu-t-g8260-class-c)
- [IEEE 1588 requirements](METRICS_REFERENCE.md#ieee-1588-2019-annex-j)

### For System Integrators

- [Standards and compliance overview](STANDARDS_REFERENCE.md)
- [Application-specific requirements](STANDARDS_REFERENCE.md#granular-compliance-levels)
- [CI/CD integration](PERFORMANCE_TESTING.md#cicd-integration)
- [Exit codes and automation](PERFORMANCE_TESTING.md#exit-codes)
- [Performance expectations](PERFORMANCE_TESTING.md#performance-expectations)
- [Linux compatibility](LINUX_COMPATIBILITY.md)

## Getting Help

1. **Start here** - [USER_GUIDE.md](USER_GUIDE.md) for comprehensive getting started guide
2. **Check the guides** - Most questions are answered in PERFORMANCE_TESTING.md
3. **Review examples** - See actual test results in `performance/` directories
4. **Check TODO.txt** - Known limitations and future enhancements
5. **Review test code** - Source in `src-gtests/tests_performance.cpp`

## Standards Covered

- **IEEE 1588-2019** - Precision Time Protocol (PTPv2)
- **ITU-T G.810** - Synchronization network terminology and equipment classes
- **ITU-T G.8260** - Packet-based timing (Classes A/B/C validated)
- **ITU-T G.8271.1** - 5G timing requirements
- **ITU-T G.8273.2** - Packet slave clock specifications
- **IEEE 802.1AS** - gPTP for Time-Sensitive Networking
- **IEEE 1139** - Frequency and time metrology (Allan deviation)

See [STANDARDS_REFERENCE.md](STANDARDS_REFERENCE.md) for detailed requirements and SwClock compliance status.

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
