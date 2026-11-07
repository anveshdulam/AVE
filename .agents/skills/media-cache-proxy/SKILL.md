---
name: media-cache-proxy
description: Designs the media pool, asset indexing, thumbnail and waveform caches, proxy generation, cache invalidation, storage management and offline-media relinking.
---

# Media Pool, Cache and Proxy

## Asset model
Each imported asset has a stable ID, canonical path/URI, content fingerprint where practical, stream metadata, import status, proxy references and generated-analysis references.

## Requirements
- Separate original media from generated proxies and caches.
- Never overwrite source media.
- Generate thumbnails, waveforms and proxies asynchronously.
- Use cache keys that include source identity, settings and tool/model version.
- Support cache eviction and user-configurable storage location.
- Handle offline, moved and relinked media.
- Avoid hashing huge files synchronously on the UI thread; support staged fingerprinting.
- Keep cache operations cancellable and crash-safe.

## Acceptance
Import/relink/offline tests, cache invalidation, proxy-to-original mapping, cleanup safety and storage usage reporting.
