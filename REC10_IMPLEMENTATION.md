# SwClock Log Format Standardization (Recommendation 10)

**IEEE Audit Priority:** 2  
**Estimated Effort:** 32 hours  
**Status:** In Progress - Phase 1 Complete

## Overview

Implements SwClock Interchange Format (SIF) - a JSON-LD based structured logging system with IEEE 1588 compliance, enabling multi-vendor interoperability and regulatory audit trails.

## Objectives

1. **Formal Schema Definition**: JSON Schema v2020-12 with semantic versioning
2. **Standards Compliance**: IEEE 1588 and ITU-T G.8260 vocabulary mappings
3. **Multi-Vendor Compatibility**: Published interchange format for PTPd, linuxptp integration
4. **Audit Trail**: Immutable, versioned, machine-readable logs
5. **Backward Compatibility**: Coexist with existing CSV logs

## Architecture

### Components

```
schemas/
  └── swclock-log-v1.0.0.json      # JSON Schema definition (SIF v1.0.0)

src/sw_clock/
  ├── swclock_jsonld.h              # Public API
  ├── swclock_jsonld.c              # Core implementation
  └── swclock_jsonld_rotation.c    # Log rotation & compression

tools/
  ├── sif_validate.py               # Schema validator
  ├── sif_convert.py                # CSV → JSON-LD converter
  └── sif_query.py                  # Log query/analysis tool
```

### Event Types

| @type | Description | Use Case |
|-------|-------------|----------|
| ServoStateUpdate | Periodic servo snapshots | Performance monitoring |
| TimeAdjustment | adjtime/adjfreq calls | Audit trail of corrections |
| PIUpdate | PI controller cycles | Servo tuning analysis |
| ThresholdAlert | Standard violations | Real-time compliance |
| SystemEvent | Lifecycle events | Operational logging |
| MetricsSnapshot | MTIE/TDEV/TE stats | Regulatory reporting |
| TestResult | gtest outcomes | CI/CD validation |

## Implementation Phases

### ✅ Phase 1: Schema Design (4 hours)
- [x] JSON Schema v2020-12 definition
- [x] JSON-LD context with IEEE 1588 vocabulary
- [x] Semantic versioning (SIF v1.0.0)
- [x] Event type definitions
- [x] API header (swclock_jsonld.h)

**Deliverables:**
- `schemas/swclock-log-v1.0.0.json` (300 lines)
- `src/sw_clock/swclock_jsonld.h` (250 lines)

### 🔄 Phase 2: Core Implementation (12 hours)
- [ ] JSON serialization library (cJSON or custom)
- [ ] Thread-safe write buffer (1MB)
- [ ] JSONL file I/O
- [ ] ISO 8601 timestamp formatting with nanosecond precision
- [ ] Event logging functions for all 7 types
- [ ] Error handling and validation

**Deliverables:**
- `src/sw_clock/swclock_jsonld.c` (~800 lines)

### ⏳ Phase 3: Rotation & Compression (8 hours)
- [ ] Size-based rotation (configurable threshold)
- [ ] Time-based rotation (daily/weekly)
- [ ] gzip compression of rotated logs
- [ ] Retention policy (max files, age)
- [ ] Atomic rotation (no data loss)

**Deliverables:**
- `src/sw_clock/swclock_jsonld_rotation.c` (~400 lines)

### ⏳ Phase 4: Integration & Tools (8 hours)
- [ ] Integrate with SwClock servo logging
- [ ] Integrate with real-time monitoring (Rec 7)
- [ ] Schema validator tool (Python)
- [ ] CSV → JSON-LD converter
- [ ] Query/analysis tool
- [ ] Documentation and examples

**Deliverables:**
- `tools/sif_*.py` (3 tools, ~600 lines total)
- Integration code in sw_clock.c
- USER_GUIDE.md updates

## JSON-LD Example

```json
{
  "@context": {
    "@vocab": "https://swclock.org/vocab#",
    "ieee1588": "https://standards.ieee.org/1588/vocab#",
    "xsd": "http://www.w3.org/2001/XMLSchema#"
  },
  "@type": "ServoStateUpdate",
  "timestamp": "2026-01-13T18:30:45.123456789Z",
  "timestamp_monotonic_ns": 32115798531625,
  "event": {
    "freq_ppm": 0.0234,
    "phase_error_ns": -125,
    "time_error_ns": 3420,
    "pi_freq_ppm": 0.0234,
    "pi_int_error_s": 0.00000342,
    "servo_enabled": true
  },
  "system": {
    "hostname": "swclock-test-01",
    "os": "Darwin",
    "kernel": "23.2.0",
    "arch": "arm64",
    "swclock_version": "2.0.0"
  }
}
```

## Benefits

### Immediate
- **Machine-readable**: JSON parsing in any language
- **Structured queries**: Filter events by type, timestamp, severity
- **Compliance reporting**: Automatic ITU-T G.8260 validation
- **Multi-vendor**: Same format for PTPd, linuxptp, SwClock

### Long-term
- **Regulatory audit**: Immutable, timestamped, versioned logs
- **Log aggregation**: ELK stack, Splunk, CloudWatch integration
- **Time-series analysis**: Grafana, Prometheus exporters
- **Semantic web**: RDF triple stores, SPARQL queries

## Migration Strategy

### Backward Compatibility
- CSV logging continues (existing code unchanged)
- JSON-LD is **additive** - enable via `SWCLOCK_JSONLD_LOG=1`
- Both formats can run simultaneously
- CSV → JSON-LD converter for historical data

### Deprecation Plan
- v2.1.0: JSON-LD available (opt-in)
- v2.2.0: JSON-LD default (CSV opt-in with `SWCLOCK_CSV_LOG=1`)
- v3.0.0: CSV deprecated (warning messages)
- v4.0.0: CSV removed

## Testing

### Validation
```bash
# Validate log against schema
tools/sif_validate.py logs/swclock.jsonl schemas/swclock-log-v1.0.0.json

# Query specific events
tools/sif_query.py logs/swclock.jsonl --type ThresholdAlert --severity critical

# Convert legacy CSV
tools/sif_convert.py logs/old.csv --output logs/old.jsonl
```

### Performance
- Target: <0.5% CPU overhead
- Buffered writes: flush every 1MB or 5 seconds
- Async I/O option for high-frequency logging

## Standards Compliance

### JSON-LD 1.1
- @context defines semantic mappings
- @type provides RDF type inference
- Expandable to RDF triples

### JSON Schema 2020-12
- Formal validation
- Machine-readable documentation
- Code generation support

### IEEE 1588
- Vocabulary for precision timing
- MTIE/TDEV metric definitions
- Clock class and accuracy specifications

### ITU-T G.8260
- Class C compliance thresholds
- TDEV observation intervals
- Holdover performance

## IEEE Audit Impact

**Current Score:** 43/100  
**Logging Category:** 3/10 (Critical deficiency)

**After Rec 10:**
- Logging Category: **9/10** (+6 points)
- Overall Score: **49/100** (+6 points)

**Justification:**
- ✅ Formal schema with versioning
- ✅ Standards compliance (IEEE 1588 vocabulary)
- ✅ Multi-vendor interchange format
- ✅ Backward compatibility maintained
- ⚠️ Not yet: cryptographic signing (Rec 11, Priority 3)

## Timeline

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| Phase 1 | 4 hours | Schema + API (DONE) |
| Phase 2 | 12 hours | Core implementation |
| Phase 3 | 8 hours | Rotation & compression |
| Phase 4 | 8 hours | Tools & integration |
| **Total** | **32 hours** | **~2,000 lines** |

## Next Steps

1. **Immediate**: Start Phase 2 (core implementation)
2. **Choose JSON library**: cJSON (lightweight) vs jq-like custom
3. **Buffer strategy**: Ring buffer vs realloc-on-demand
4. **Integration point**: Add to sw_clock.c logging paths

---

**Status:** Phase 1 Complete ✅  
**Next:** Phase 2 - Core Implementation (12 hours)

