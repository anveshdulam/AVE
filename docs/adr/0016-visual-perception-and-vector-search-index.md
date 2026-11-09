# ADR-0016: Visual Perception, Keyframe Embeddings, and Vector Search Index

## Context
Autonomous video editing requires visual understanding across media assets:
1. **Color Look & Exposure**: Measuring average scene luminance ($L^*$), color cast ($a^*, b^*$), and contrast to maintain visual continuity and grade consistency across edits.
2. **Motion Energy**: Distinguishing static talking-head clips from rapid action, camera pans, and handheld footage to regulate pacing.
3. **Semantic Shot Similarity**: Enabling text-to-clip search, shot clustering, duplicate elimination, and pairing reference video shots with user footage without inspecting every individual frame.
4. **Computational Efficiency & Local-First**: Extracting high-dimensional embeddings on every single frame is computationally prohibitive for a desktop NLE. We need sparse keyframe sampling and an in-memory vector index with permissive licensing (Apache 2.0).

## Decision
We implemented a three-tier visual perception subsystem:

1. **Color Science Metrics (`ave::ColorLookAnalyzer`)**:
   - Converts RGBA8 frames from sRGB through D65 XYZ space into CIE $L^*a^*b^*$.
   - Calculates mean lightness ($L^* \in [0, 100]$), chromatic green-red balance ($a^* \in [-128, 127]$), and blue-yellow balance ($b^* \in [-128, 127]$).
   - Measures contrast as the standard deviation of lightness $L^*$.

2. **Inter-Frame Motion Analysis (`ave::MotionAnalyzer`)**:
   - Calculates average normalized luminance displacement between successive frames.
   - Generates the continuous `"visual_motion_energy"` curve stored in `PerceptionReport::raw_curves` and populates `VisualReport::average_motion_energy`.

3. **Keyframe Semantics with SigLIP 2 (`ave::SigLIPEmbeddingExtractor`)**:
   - Evaluates only the representative keyframes per shot (selected during shot boundary detection in Phase 7B).
   - Generates 1152-dimensional $L_2$-normalized embedding vectors ($\|v\|_2 = 1.0$).
   - Plugs into `ave::IModelRunner` for ONNX Runtime acceleration on GPU/CPU with a deterministic offline fallback when weights are uninitialized.

4. **In-Memory Cosine Vector Search (`ave::VectorSearchIndex`)**:
   - Stores keyframe embeddings associated with exact `ave::Rational` PTS and shot indices.
   - Computes inner-product cosine similarity: $S_C(u, v) = u \cdot v$.
   - Implements `search_nearest(query, top_k)` to instantly identify visually and semantically related shots.

## Consequences
- Fast, license-compliant (Apache 2.0) semantic search and reference shot matching.
- Visual continuity (motion, color, contrast) is quantitatively measured and accessible to the planner.
- Zero network or cloud API dependencies.
- Verified by automated test suite `ave_visual_perception_test` passing 100% across all 21 project test suites.
