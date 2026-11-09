# ADR-0017: Reference Fingerprint v2 and Mathematical Distance Metric

## Context
Autonomous video editing requires capturing the stylistic essence of a reference edit (e.g. music video, trailer, montage, interview) and transferring that style to target raw footage.

Simple scalar averages (such as Average Shot Length - ASL - or Cuts Per Minute - CPM) fail to capture:
1. **Pacing Dynamics**: Whether an edit features uniform metronomic pacing versus bursty rhythms (montages alternating with long takes).
2. **Musical Sync & Groove**: The percentage of cuts landing on beats versus downbeats versus off-beats, and micro-timing offsets (cutting 20ms early vs on-beat).
3. **Transition Grammar**: The mixture of hard cuts, dissolves, and fades.
4. **Visual & Energy Curves**: Motion energy distribution, color cast (CIE $L^*a^*b^*$), contrast, and speech density.
5. **Objective Evaluation**: The planner needs an objective function to minimize when generating edit proposals. Without a mathematically rigorous distance metric, candidate edits cannot be quantitatively compared against reference targets.

## Decision
We implemented **Reference Fingerprint v2** (`ave::ReferenceFingerprint`) and a multi-dimensional normalized distance function (`ave::FingerprintEngine`):

1. **Multi-Dimensional Style Representation**:
   - **Pacing Distribution (`ShotLengthDistribution`)**: Computes median shot duration, 25th percentile (Q1), 75th percentile (Q3), interquartile range (IQR), and normalized burstiness parameter $B = \frac{\sigma - \mu}{\sigma + \mu} \in [-1.0, 1.0]$.
   - **Cut-to-Beat Phase (`CutToBeatDistribution`)**: Measures beat sync ratio ($\pm 70\text{ ms}$ window), downbeat sync ratio, off-beat ratio, and mean signed temporal offset in milliseconds.
   - **Transition Mix (`TransitionMix`)**: Hard cut, dissolve, and fade ratios ($H + D + F = 1.0$).
   - **Motion Profile (`MotionProfile`)**: Mean inter-frame motion energy and variance.
   - **Color Look & Audio Profile**: Mean CIE $L^*, a^*, b^*$, contrast, EBU R128 integrated loudness (LUFS), tempo (BPM), and dialogue speech density.

2. **Universal Dual Extractor**:
   - `from_perception_report`: Extracts fingerprint from reference video analysis (`ave::PerceptionReport`).
   - `from_timeline`: Extracts fingerprint from an actual native editorial timeline (`ave::Timeline`) and audio rhythm data.

3. **Normalized Distance Function $D(F_1, F_2) \in [0.0, 1.0]$**:
   - Computes weighted dimensional distances across pacing (0.30), rhythm (0.25), transitions (0.15), motion (0.15), and color look (0.15).
   - Satisfies mathematical metric properties:
     - Identity: $D(F, F) = 0.0$.
     - Symmetry: $D(F_1, F_2) = D(F_2, F_1)$.
     - Strict bounds: $0.0 \le D(F_1, F_2) \le 1.0$.
   - Provides `compute_distance_detailed` returning exact dimensional distance breakdowns for debugging and planner cost scoring.

4. **Zero Float Drift & Serialization**:
   - JSON roundtrip serialization and parsing verified.

## Consequences
- The Phase 7F deterministic planner now has an explicit, mathematically sound objective function to optimize when constructing beat-grid cut proposals.
- Automated tests can score and assert editing style fidelity with mathematical precision.
- 100% offline, local-first execution with zero cloud calls.
- Verified by automated test suite `ave_fingerprint_test` passing 100% across all 22 project test suites.
