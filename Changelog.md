# Changelog

## [1.3.0] - 2026-04-02
### Added
- **Automated Showcase Demo:** New `IOC.SpawnShowcase` console command spawns a cinematic flythrough across 7 cave sections, each highlighting a different plugin feature: Cellular Automata, Perlin Tunnels, Domain Warp, Terrace Steps, Decoration Scattering, Carving Volumes, and Streaming.
- **Showcase Cleanup:** New `IOC.ClearShowcase` console command destroys all showcase actors and restores camera.

## [1.2.2] - 2026-02-24
### Fixed
- **Mesh Tearing (Shredded Mesh):** Implemented a strict **Topological Gradient Limit** (`Frequency * Radius < 1.0`) to prevent surface self-intersection and "shredded" artifacts in large tunnels. This ensures the noise displacement never exceeds the expansion rate of the tunnel radius.
- **Preset Safety:** Updated `LargeTunnel`, `TightCrawl`, and `OpenCavern` presets to use safer default frequencies that guarantee mesh integrity out-of-the-box.

## [1.2.1] - 2026-02-24
### Fixed
- **Critical Mesh Artifacts:** Fixed "Shredded Mesh" / Checkerboard holes by moving the Nyquist Frequency Sanitation logic to a pre-calculation phase. This prevents discontinuous noise sampling that occurred when frequency was dynamically clamped per-voxel.
- **Biome Stability:** Fixed potential frequency mismatches in Biome Overrides where a large Tunnel Radius override without a corresponding Frequency override could result in unsafe noise sampling. The system now force-clamps the frequency for that specific biome's radius.

## [1.2] - 2026-02-22
### Fixed
- **Mesh Fragmentation:** Fixed critical "shredded mesh" issue caused by high-frequency noise aliasing. Implemented automatic frequency clamping based on Voxel Size (Nyquist limit) and Tunnel Radius (Gradient Stability).
- **Detail Resolution:** Lowered minimum `SafeVoxel` size from 20.0 to 10.0 to allow for higher-fidelity cave generation when explicitly requested.
- **Safety:** Added dynamic frequency capping to Biome overrides to prevent localized mesh explosions.

## [1.1] - 2026-02-17
### Added
- **Multi-Spline Branching:** `IOCProceduralActor` now automatically discovers and fuses all attached `USplineComponent` children into a unified tunnel network using Distance Field unions ("Segment Soup").
- **Edge Falloff:** Standard Mode now respects boundary falloff to prevent "sliced cube" artifacts at generation bounds.
- **Clean Shells:** Tunnel Mode generation now enforces minimum wall thickness relative to voxel size to prevent holes or fragmented geometry.
- **Async Scattering:** Decoration placement is now fully async and thread-safe.

### Fixed
- **Race Condition:** Fixed crash in `DecorationLayers` access during Async execution.
- **Memory Leak:** Fixed console command delegate leak in `ShutdownModule`.

## [1.0] - 2026-02-05
- Initial Release
- Features: Perlin Tunnels, Cellular Automata, Async Generation, Smart Materials, Decoration Scattering.
