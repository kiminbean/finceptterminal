"""
Shared HTTP utilities for FinceptTerminal Python scripts.

Provides a reusable requests.Session with connection pooling, gzip support,
and response caching to reduce redundant API calls across all data scripts.
"""

import time
import requests
from typing import Any, Dict, Optional
from functools import lru_cache

# Shared session with connection pooling (up to 10 concurrent connections)
_session = requests.Session()
_session.headers.update({
    "Accept": "application/json",
    "Accept-Encoding": "gzip, deflate",
    "User-Agent": "FinceptTerminal/4.0",
})
_adapter = requests.adapters.HTTPAdapter(pool_connections=10, pool_maxsize=10, max_retries=2)
_session.mount("https://", _adapter)
_session.mount("http://", _adapter)


def get_session() -> requests.Session:
    """Get the shared requests.Session with connection pooling."""
    return _session


class ResponseCache:
    """Simple TTL-based response cache for deduplicating rapid API calls."""

    def __init__(self, default_ttl: int = 30):
        self._cache: Dict[str, tuple] = {}
        self._default_ttl = default_ttl

    def get(self, key: str) -> Optional[Any]:
        entry = self._cache.get(key)
        if entry is None:
            return None
        timestamp, data = entry
        if time.time() - timestamp > self._default_ttl:
            del _cache[key]  # noqa: F821
            return None
        return data

    def set(self, key: str, data: Any) -> None:
        self._cache[key] = (time.time(), data)

    def cached_get(self, url: str, params: Optional[Dict] = None,
                   timeout: int = 30, ttl: Optional[int] = None) -> Any:
        """
        Cached GET request. Returns cached response if within TTL,
        otherwise makes a fresh request and caches it.
        """
        cache_key = f"{url}:{sorted(params.items()) if params else ''}"
        now = time.time()
        effective_ttl = ttl if ttl is not None else self._default_ttl

        entry = self._cache.get(cache_key)
        if entry is not None and (now - entry[0]) < effective_ttl:
            return entry[1]

        response = _session.get(url, params=params, timeout=timeout)
        response.raise_for_status()
        data = response.json()
        self._cache[cache_key] = (now, data)
        return data


# Module-level shared cache instance
cache = ResponseCache(default_ttl=30)
