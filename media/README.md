# Layer 4 — Media and Data

## Responsibilities
- Demuxing, decoding, encoding, and metadata probing via FFmpeg libraries (`libavformat`, `libavcodec`, `libswresample`).
- Still image sequences and HDR metadata handling via OpenImageIO.
- Proxy generation, thumbnail caches, and audio waveform peak generation.
- Immutable source asset indexing and media pool registration.
- Native project database (SQLite with WAL mode).

## Boundary Rules
- Media files are immutable source assets; editing operations never overwrite source media.
- UI never decodes media directly. Decoding is deadline-scheduled on background worker threads.

