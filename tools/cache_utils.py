#!/usr/bin/env python3
"""
cache_utils.py - Caching Utilities for Performance Analysis

Provides caching infrastructure to speed up repeated analysis by storing:
- Parsed CSV data
- Detrended signals
- Computed metrics

Cache invalidation based on file modification times ensures data freshness.
"""

import pickle
import hashlib
from pathlib import Path
from typing import Any, Optional, Tuple
import os

class AnalysisCache:
    """Manages cached analysis data with automatic invalidation"""
    
    def __init__(self, cache_dir: Optional[Path] = None, enabled: bool = True):
        """
        Initialize cache manager
        
        Args:
            cache_dir: Directory to store cache files (default: .cache/ in output dir)
            enabled: Whether caching is enabled (default: True)
        """
        self.cache_dir = cache_dir
        self.enabled = enabled
        
        if self.enabled and self.cache_dir:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
    
    @staticmethod
    def _get_file_hash(filepath: Path) -> str:
        """
        Get hash of file path and modification time for cache key
        
        Args:
            filepath: Path to file
            
        Returns:
            Hash string for cache key
        """
        # Use file path and mtime for cache key
        mtime = os.path.getmtime(filepath)
        key_str = f"{filepath}:{mtime}"
        return hashlib.md5(key_str.encode()).hexdigest()
    
    def _get_cache_path(self, source_file: Path, cache_type: str) -> Path:
        """
        Get cache file path for given source file and cache type
        
        Args:
            source_file: Original data file
            cache_type: Type of cached data (e.g., 'parsed', 'detrended', 'metrics')
            
        Returns:
            Path to cache file
        """
        file_hash = self._get_file_hash(source_file)
        cache_filename = f"{source_file.stem}_{cache_type}_{file_hash}.pkl"
        return self.cache_dir / cache_filename
    
    def is_cached(self, source_file: Path, cache_type: str) -> bool:
        """
        Check if valid cache exists for source file
        
        Args:
            source_file: Original data file
            cache_type: Type of cached data
            
        Returns:
            True if valid cache exists
        """
        if not self.enabled or not self.cache_dir:
            return False
        
        cache_path = self._get_cache_path(source_file, cache_type)
        
        if not cache_path.exists():
            return False
        
        # Check if source file has been modified since cache creation
        source_mtime = os.path.getmtime(source_file)
        cache_mtime = os.path.getmtime(cache_path)
        
        # Cache is valid if it's newer than source
        return cache_mtime >= source_mtime
    
    def load(self, source_file: Path, cache_type: str) -> Optional[Any]:
        """
        Load cached data if available and valid
        
        Args:
            source_file: Original data file
            cache_type: Type of cached data
            
        Returns:
            Cached data or None if not available
        """
        if not self.is_cached(source_file, cache_type):
            return None
        
        cache_path = self._get_cache_path(source_file, cache_type)
        
        try:
            with open(cache_path, 'rb') as f:
                return pickle.load(f)
        except (pickle.PickleError, EOFError, OSError) as e:
            # Cache corrupted, remove it
            print(f"Warning: Cache corrupted, removing: {cache_path}")
            cache_path.unlink(missing_ok=True)
            return None
    
    def save(self, source_file: Path, cache_type: str, data: Any) -> bool:
        """
        Save data to cache
        
        Args:
            source_file: Original data file
            cache_type: Type of cached data
            data: Data to cache
            
        Returns:
            True if successfully saved
        """
        if not self.enabled or not self.cache_dir:
            return False
        
        cache_path = self._get_cache_path(source_file, cache_type)
        
        try:
            with open(cache_path, 'wb') as f:
                pickle.dump(data, f, protocol=pickle.HIGHEST_PROTOCOL)
            return True
        except (pickle.PickleError, OSError) as e:
            print(f"Warning: Failed to save cache: {e}")
            return False
    
    def invalidate(self, source_file: Path, cache_type: Optional[str] = None):
        """
        Invalidate cache for source file
        
        Args:
            source_file: Original data file
            cache_type: Specific cache type to invalidate, or None for all
        """
        if not self.enabled or not self.cache_dir:
            return
        
        if cache_type:
            # Invalidate specific cache type
            cache_path = self._get_cache_path(source_file, cache_type)
            cache_path.unlink(missing_ok=True)
        else:
            # Invalidate all cache types for this file
            pattern = f"{source_file.stem}_*_{self._get_file_hash(source_file)[:8]}*.pkl"
            for cache_file in self.cache_dir.glob(pattern):
                cache_file.unlink(missing_ok=True)
    
    def clear_all(self):
        """Clear all cache files"""
        if not self.enabled or not self.cache_dir:
            return
        
        for cache_file in self.cache_dir.glob("*.pkl"):
            cache_file.unlink(missing_ok=True)
    
    def get_stats(self) -> dict:
        """
        Get cache statistics
        
        Returns:
            Dictionary with cache stats
        """
        if not self.enabled or not self.cache_dir:
            return {'enabled': False, 'total_files': 0, 'total_size_mb': 0}
        
        cache_files = list(self.cache_dir.glob("*.pkl"))
        total_size = sum(f.stat().st_size for f in cache_files)
        
        return {
            'enabled': True,
            'cache_dir': str(self.cache_dir),
            'total_files': len(cache_files),
            'total_size_mb': total_size / (1024 * 1024)
        }


def load_csv_with_cache(csv_path: Path, cache: AnalysisCache, 
                        load_func) -> Tuple[Any, bool]:
    """
    Load CSV data with caching support
    
    Args:
        csv_path: Path to CSV file
        cache: Cache manager
        load_func: Function to load CSV if not cached (takes csv_path)
        
    Returns:
        (data, from_cache) tuple
    """
    # Try to load from cache
    cached_data = cache.load(csv_path, 'parsed')
    if cached_data is not None:
        return cached_data, True
    
    # Load fresh data
    data = load_func(csv_path)
    
    # Save to cache
    cache.save(csv_path, 'parsed', data)
    
    return data, False


def compute_with_cache(source_file: Path, cache_type: str, cache: AnalysisCache,
                      compute_func, *args, **kwargs) -> Tuple[Any, bool]:
    """
    Compute result with caching support
    
    Args:
        source_file: Source file for cache key
        cache_type: Type of cached computation
        cache: Cache manager
        compute_func: Function to compute result if not cached
        *args, **kwargs: Arguments for compute_func
        
    Returns:
        (result, from_cache) tuple
    """
    # Try to load from cache
    cached_result = cache.load(source_file, cache_type)
    if cached_result is not None:
        return cached_result, True
    
    # Compute fresh result
    result = compute_func(*args, **kwargs)
    
    # Save to cache
    cache.save(source_file, cache_type, result)
    
    return result, False


if __name__ == '__main__':
    """Test caching functionality"""
    import tempfile
    import numpy as np
    import time
    
    print("Testing AnalysisCache...")
    
    with tempfile.TemporaryDirectory() as tmpdir:
        cache_dir = Path(tmpdir) / '.cache'
        test_file = Path(tmpdir) / 'test.csv'
        
        # Create test file
        test_file.write_text("timestamp,value\n1,2\n3,4\n")
        
        # Initialize cache
        cache = AnalysisCache(cache_dir, enabled=True)
        
        # Test data
        test_data = {'array': np.array([1, 2, 3, 4, 5]), 'value': 42}
        
        # Test save
        print("Saving to cache...")
        assert cache.save(test_file, 'test_data', test_data)
        
        # Test load
        print("Loading from cache...")
        loaded = cache.load(test_file, 'test_data')
        assert loaded is not None
        assert np.array_equal(loaded['array'], test_data['array'])
        assert loaded['value'] == test_data['value']
        
        # Test is_cached
        assert cache.is_cached(test_file, 'test_data')
        
        # Test invalidation on file modification
        print("Modifying source file...")
        time.sleep(0.1)  # Ensure mtime changes
        test_file.write_text("timestamp,value\n1,2\n3,4\n5,6\n")
        
        # Cache should be invalid now
        assert not cache.is_cached(test_file, 'test_data')
        
        # Test stats
        stats = cache.get_stats()
        print(f"Cache stats: {stats}")
        assert stats['enabled']
        
        # Test clear
        cache.clear_all()
        assert cache.get_stats()['total_files'] == 0
        
        print("\n✓ All cache tests passed!")
