#!/usr/bin/env python3
"""
SwClock Log Integrity Manager
Implements SHA-256 hashing and manifest generation for tamper detection.

Part of Priority 1 implementation (Recommendation 3).
"""

import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional
import argparse


class LogIntegrityManager:
    """
    Manages log file integrity through SHA-256 hashing and manifest generation.
    Implements hash-based tamper detection for audit compliance.
    """
    
    MANIFEST_FILENAME = "manifest.json"
    ALGORITHM = "SHA-256"
    
    def __init__(self, output_dir: Path):
        """
        Initialize integrity manager for a test output directory.
        
        Args:
            output_dir: Path to directory containing test logs
        """
        self.output_dir = Path(output_dir)
        self.manifest_path = self.output_dir / self.MANIFEST_FILENAME
    
    def compute_file_hash(self, file_path: Path) -> Dict[str, any]:
        """
        Compute SHA-256 hash of a file.
        
        Args:
            file_path: Path to file to hash
            
        Returns:
            Dictionary with file metadata and hash
        """
        sha256 = hashlib.sha256()
        file_size = 0
        
        try:
            with open(file_path, 'rb') as f:
                while chunk := f.read(8192):
                    sha256.update(chunk)
                    file_size += len(chunk)
        except IOError as e:
            print(f"Error reading {file_path}: {e}", file=sys.stderr)
            return None
        
        digest = sha256.hexdigest()
        
        return {
            'file': str(file_path.relative_to(self.output_dir)),
            'size_bytes': file_size,
            'sha256': digest,
            'timestamp': datetime.now(timezone.utc).isoformat(),
            'algorithm': self.ALGORITHM
        }
    
    def seal_directory(self, patterns: Optional[List[str]] = None) -> Dict:
        """
        Seal all log files in directory by computing hashes and creating manifest.
        
        Args:
            patterns: List of glob patterns to include (default: ['*.csv', '*.log', '*.txt'])
            
        Returns:
            Manifest dictionary
        """
        if patterns is None:
            patterns = ['*.csv', '*.log', '*.txt', '*.json', '*.bin']
        
        if not self.output_dir.exists():
            raise FileNotFoundError(f"Output directory not found: {self.output_dir}")
        
        print(f"Sealing log files in: {self.output_dir}")
        
        file_manifests = []
        for pattern in patterns:
            for file_path in self.output_dir.rglob(pattern):
                # Skip the manifest itself
                if file_path.name == self.MANIFEST_FILENAME:
                    continue
                
                print(f"  Hashing: {file_path.name}")
                file_manifest = self.compute_file_hash(file_path)
                if file_manifest:
                    file_manifests.append(file_manifest)
        
        # Create overall manifest
        manifest = {
            'version': '1.0',
            'sealed_at': datetime.now(timezone.utc).isoformat(),
            'directory': str(self.output_dir.name),
            'algorithm': self.ALGORITHM,
            'file_count': len(file_manifests),
            'total_bytes': sum(f['size_bytes'] for f in file_manifests),
            'files': file_manifests
        }
        
        # Write manifest
        with open(self.manifest_path, 'w') as f:
            json.dump(manifest, f, indent=2)
        
        print(f"\nManifest created: {self.manifest_path}")
        print(f"  Files sealed: {len(file_manifests)}")
        print(f"  Total size: {manifest['total_bytes']:,} bytes")
        
        return manifest
    
    def verify_directory(self) -> bool:
        """
        Verify all files in directory match their manifest hashes.
        
        Returns:
            True if all files verified successfully, False otherwise
        """
        if not self.manifest_path.exists():
            print(f"ERROR: Manifest not found: {self.manifest_path}", file=sys.stderr)
            return False
        
        # Load manifest
        with open(self.manifest_path) as f:
            manifest = json.load(f)
        
        print(f"Verifying integrity of {manifest['file_count']} files...")
        print(f"Manifest created: {manifest['sealed_at']}")
        
        all_verified = True
        verified_count = 0
        
        for file_manifest in manifest['files']:
            file_path = self.output_dir / file_manifest['file']
            
            if not file_path.exists():
                print(f"  ❌ MISSING: {file_manifest['file']}")
                all_verified = False
                continue
            
            # Recompute hash
            current_manifest = self.compute_file_hash(file_path)
            if not current_manifest:
                print(f"  ❌ ERROR: Could not hash {file_manifest['file']}")
                all_verified = False
                continue
            
            # Compare hashes
            expected_hash = file_manifest['sha256']
            computed_hash = current_manifest['sha256']
            
            if computed_hash == expected_hash:
                print(f"  ✓ {file_manifest['file']}")
                verified_count += 1
            else:
                print(f"  ❌ TAMPERED: {file_manifest['file']}")
                print(f"     Expected: {expected_hash}")
                print(f"     Computed: {computed_hash}")
                all_verified = False
        
        print(f"\nVerification result: {verified_count}/{manifest['file_count']} files OK")
        
        if all_verified:
            print("✓ All files verified successfully - integrity intact")
        else:
            print("❌ INTEGRITY VIOLATION DETECTED", file=sys.stderr)
        
        return all_verified
    
    def get_manifest_summary(self) -> Optional[Dict]:
        """
        Get summary information from manifest without verification.
        
        Returns:
            Manifest summary or None if not found
        """
        if not self.manifest_path.exists():
            return None
        
        with open(self.manifest_path) as f:
            manifest = json.load(f)
        
        return {
            'sealed_at': manifest['sealed_at'],
            'file_count': manifest['file_count'],
            'total_bytes': manifest['total_bytes'],
            'algorithm': manifest['algorithm']
        }


def main():
    """Command-line interface for log integrity management."""
    parser = argparse.ArgumentParser(
        description='SwClock Log Integrity Manager - Seal and verify test logs',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Seal logs after test run
  %(prog)s seal performance/performance_20260113-163249
  
  # Verify log integrity before analysis
  %(prog)s verify performance/performance_20260113-163249
  
  # Show manifest summary
  %(prog)s info performance/performance_20260113-163249
"""
    )
    
    parser.add_argument(
        'command',
        choices=['seal', 'verify', 'info'],
        help='Command: seal=create manifest, verify=check integrity, info=show summary'
    )
    
    parser.add_argument(
        'directory',
        type=Path,
        help='Test output directory to seal or verify'
    )
    
    parser.add_argument(
        '--patterns',
        nargs='+',
        default=['*.csv', '*.log', '*.txt', '*.json', '*.bin'],
        help='File patterns to include (default: *.csv *.log *.txt *.json *.bin)'
    )
    
    args = parser.parse_args()
    
    manager = LogIntegrityManager(args.directory)
    
    if args.command == 'seal':
        try:
            manifest = manager.seal_directory(patterns=args.patterns)
            sys.exit(0)
        except Exception as e:
            print(f"ERROR: Failed to seal directory: {e}", file=sys.stderr)
            sys.exit(1)
    
    elif args.command == 'verify':
        verified = manager.verify_directory()
        sys.exit(0 if verified else 1)
    
    elif args.command == 'info':
        summary = manager.get_manifest_summary()
        if summary:
            print(f"Manifest Summary:")
            print(f"  Sealed: {summary['sealed_at']}")
            print(f"  Files: {summary['file_count']}")
            print(f"  Size: {summary['total_bytes']:,} bytes")
            print(f"  Algorithm: {summary['algorithm']}")
            sys.exit(0)
        else:
            print(f"ERROR: No manifest found in {args.directory}", file=sys.stderr)
            sys.exit(1)


if __name__ == '__main__':
    main()
