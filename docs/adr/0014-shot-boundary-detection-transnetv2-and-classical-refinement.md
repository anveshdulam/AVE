# ADR-0014: Shot Boundary and Transition Detection with TransNetV2 and Classical Refinement

## Context
Video perception for autonomous editing requires reliable detection of:
1. **Hard cuts**: Abrupt scene transitions from one camera setup or angle to another.
2. **Gradual transitions**: Cross-fades, dissolves, and fades that span multiple frames as interval ranges $[t_{\text{in}}, t_{\text{out}}]$.

Purely classical frame-to-frame approaches (e.g. naive color histogram differences) suffer from false positives during rapid object motion, camera pans, and flash frames, and struggle to accurately delimit the start and end of gradual dissolves. Conversely, standalone deep neural networks (like TransNetV2) evaluate rolling temporal context with high recall, but benefit from fine-grained edge and color gradient analysis to snap cuts to single-frame precision.

## Decision
We implemented a two-stage hybrid fusion architecture:

1. **Deep Temporal Prediction with TransNetV2 (`ave::TransNetV2Detector`)**:
   - Evaluates video using a rolling 100-frame window ($48 \times 27 \times 3$ RGB) with a 50-frame overlap stride.
   - Outputs continuous frame transition probabilities $P(\text{transition}) \in [0.0, 1.0]$.
   - Emits gradual transitions (dissolves/fades) as time ranges $[t_{\text{in}}, t_{\text{out}}]$ when transition probabilities exceed $0.35$ for 4 or more consecutive frames.
   - Selects 1–3 representative keyframes per shot (midpoint for short shots; 20%, 50%, 80% for shots $\ge 2.0$s) for subsequent visual embedding.

2. **Frame-Accurate Classical Refinement (`ave::ClassicalShotDetector`)**:
   - Computes normalized Chi-Square color histogram distance (4,096 bins) and luminance Sobel Edge Change Ratio (ECR).
   - Functions as the primary offline fallback when neural network weights are absent.
   - Refines candidate cut points within a $\pm 2$ frame window to snap cuts to the exact frame of maximum local contrast gradient.

3. **Runtime Model Abstraction (`ave::ModelRuntime`)**:
   - Decouples ML execution providers (`Cpu`, `Cuda`, `TensorRt`, `DirectML`, `WindowsML`, `Mock`) behind an abstract C++ interface.
   - DirectML is kept replaceable per ADR guidelines.
   - Fixed static input shapes (`[1, 100, 27, 48, 3]`) prevent dynamic shape allocation overhead.

4. **Integration with Canonical Perception Report**:
   - All detected shot and dissolve intervals are populated as `ShotBoundary` structs using exact 64-bit rational time (`ave::Rational`).
   - The continuous transition probability timeseries is preserved in `PerceptionReport::raw_curves` under `"transnet_transition_prob"`.

## Consequences
- Robust, license-compliant (MIT) shot and dissolve detection running 100% locally.
- Zero external cloud API calls or privacy compromises.
- Graceful degradation: Seamlessly falls back to classical histogram/ECR analysis if weights are not downloaded.
- Verified by automated test suite `ave_shot_detector_test` passing 100% across all 19 project test suites.
