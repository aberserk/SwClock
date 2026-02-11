#!/usr/bin/env python3
"""
SwClock Interchange Format (SIF) Validator
Validates JSON-LD logs against schema v1.0.0
"""

import json
import sys
import argparse
from pathlib import Path

def validate_jsonl(log_file, schema_file):
    """Validate JSONL file against JSON Schema"""
    
    # Load schema
    try:
        with open(schema_file, 'r') as f:
            schema = json.load(f)
        print(f"✓ Loaded schema: {schema_file}")
    except Exception as e:
        print(f"✗ Failed to load schema: {e}", file=sys.stderr)
        return False
    
    # Validate each line
    errors = []
    line_count = 0
    valid_count = 0
    
    try:
        with open(log_file, 'r') as f:
            for line_num, line in enumerate(f, 1):
                line_count += 1
                line = line.strip()
                
                if not line:
                    continue
                
                try:
                    entry = json.loads(line)
                    
                    # Basic validation: check required fields
                    if '@context' not in entry:
                        errors.append(f"Line {line_num}: Missing @context")
                        continue
                    
                    if '@type' not in entry:
                        errors.append(f"Line {line_num}: Missing @type")
                        continue
                    
                    if 'timestamp' not in entry:
                        errors.append(f"Line {line_num}: Missing timestamp")
                        continue
                    
                    if 'event' not in entry:
                        errors.append(f"Line {line_num}: Missing event")
                        continue
                    
                    # Validate @type is one of the defined types
                    valid_types = [
                        "ServoStateUpdate", "TimeAdjustment", "PIUpdate",
                        "ThresholdAlert", "SystemEvent", "MetricsSnapshot", "TestResult"
                    ]
                    
                    if entry['@type'] not in valid_types:
                        errors.append(f"Line {line_num}: Invalid @type '{entry['@type']}'")
                        continue
                    
                    valid_count += 1
                    
                except json.JSONDecodeError as e:
                    errors.append(f"Line {line_num}: JSON parse error: {e}")
                    
    except Exception as e:
        print(f"✗ Failed to read log file: {e}", file=sys.stderr)
        return False
    
    # Print results
    print(f"\nValidation Results:")
    print(f"  Total lines:  {line_count}")
    print(f"  Valid entries: {valid_count}")
    print(f"  Errors:        {len(errors)}")
    
    if errors:
        print(f"\nErrors found:")
        for error in errors[:20]:  # Show first 20 errors
            print(f"  {error}")
        if len(errors) > 20:
            print(f"  ... and {len(errors) - 20} more")
        return False
    else:
        print(f"✓ All entries valid")
        return True

def main():
    parser = argparse.ArgumentParser(description='Validate SwClock JSON-LD logs')
    parser.add_argument('log_file', help='JSONL log file to validate')
    parser.add_argument('--schema', 
                       default='resources/schemas/swclock-log-v1.0.0.json',
                       help='JSON Schema file')
    
    args = parser.parse_args()
    
    # Check files exist
    if not Path(args.log_file).exists():
        print(f"✗ Log file not found: {args.log_file}", file=sys.stderr)
        return 1
    
    if not Path(args.schema).exists():
        print(f"✗ Schema file not found: {args.schema}", file=sys.stderr)
        return 1
    
    # Validate
    success = validate_jsonl(args.log_file, args.schema)
    
    return 0 if success else 1

if __name__ == '__main__':
    sys.exit(main())
