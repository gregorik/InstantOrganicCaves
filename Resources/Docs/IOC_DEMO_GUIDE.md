# IOC Demo Guide

> Applies to Instant Organic Caves 0.4.0 (UE 5.5 - 5.8).

> Demo/showcase console commands are development tools. They are registered as cheat commands outside Shipping and are omitted from Shipping builds.

> **Cleaning up.** The demo commands spawn actors into whichever level is open.
> `IOC.ClearShowcase` removes the showcase flythrough and restores the camera;
> `IOC.ClearAllDemos` removes everything the demos created, including the tunnel demo,
> the spectacular demo and the demo character. All of it is also under
> **Tools → Instant Organic Caves**.

## Fastest route in

**Tools → Instant Organic Caves → Open Demo Map**, then press **Play**. The shipped map at
`/InstantOrganicCaves/Maps/IOC_DemoMap` contains a showcase launcher set to auto-start; the
eight-section flythrough builds every cave live. Nothing needs setting up first.

## Comprehensive Feature List
1. Procedural Perlin tunnel generation between two points (`bGenerateTunnel`, `TunnelStart`, `TunnelEnd`, `TunnelRadius`, `WallThickness`).
2. Multi-spline branching tunnels via auto-union of spline segments.
3. Cellular Automata caves and Infinite/Tileable CA via the `IOC Voxel Core` PCG node.
4. Async generation on background threads for responsive editor workflows.
5. Presets for common styles (`LargeTunnel`, `TightCrawl`, `OpenCavern`, `AlienHive`, `CanyonStrata`).
6. Domain warp and terrace steps for “spectacular” looks.
7. Smoothing / relaxation (`SmoothIterations`) for organic surfaces.
8. Smart vertex colors for wall/floor/ceiling material blending.
9. Auto UVs (box/triplanar-style) plus `TextureTiling` control.
10. Decoration scattering with slope filters and optional Poisson separation, using the eight props that ship in `/InstantOrganicCaves/InstantOrganicCaves/Geometry/` (three rocks, two crystals, geode, stalactite, stalagmite) - no third-party art required.
11. LOD mesh generation with distance toggle and voxel-size multiplier.
12. Runtime carving API plus `IOC Carving Volume` components (Sphere/Box/Capsule with falloff blending).
13. Biome volumes for localized overrides (noise, radius, thickness, terraces).
14. Editor baking to static mesh (`BakeToStaticMesh`) into `/Game/IOC_Baked`.
15. Editor debug visualization (bounds + tunnel line).
16. Complex-as-simple collision on dynamic mesh.
17. PCG integration via `IOC Voxel Core` generation modes.
18. Live generation metrics: time, estimated voxels, primary triangles, LOD triangles, scatter instance count.
19. Demo commands `IOC.SpawnTunnelDemo`, `IOC.SpawnSpectacular`, `IOC.SpawnShowcase` and `IOC.SpawnShowcaseCapture`, with `IOC.ClearShowcase` / `IOC.ClearAllDemos` to remove them cleanly, plus `IOC.OpenSetupWizard` and `IOC.ValidateInstallation` in the editor.
20. Starter assets ship with the plugin and can be surfaced from the setup wizard (`BP_IOC_Cave` + smart material instance).
21. Capture-ready showcase map setup via the built-in **Create Starter Level** action and `AIOCShowcaseLauncher`.

## Capture-Ready Showcase

Use **Tools > Instant Organic Caves > Setup Wizard** (also on **Window**, or `IOC.OpenSetupWizard`) and press **Create Starter Level** to create `/Game/IOC_Showcase` with an `IOC_ShowcaseLauncher` actor. Press Play and the launcher starts `IOC.SpawnShowcaseCapture` automatically. Optional manual automation remains available through `Plugins/InstantOrganicCaves/Resources/IOC_CreateShowcaseMap.py`.

The capture demo is designed for a 60-90 second pass: 8 sections, polished Slate captions, no debug HUD, no external Fab asset paths, and a camera path that covers actor placement, presets, path/spline workflow, scatter props, carving, real streaming-manager chunks, live performance metrics, and the bake-ready mesh.

Manual fallback: run `IOC.SpawnShowcaseCapture` in Play-In-Editor. Add `NoCaptions` only when recording clean B-roll.

## 10-Point Demo Plan
1. Instant demo command.
2. Preset showcase.
3. Spline network.
4. Biome volumes.
5. Carving volumes and runtime dig.
6. Decoration scattering.
7. Materials and UVs.
8. LOD, live metrics, and performance.
9. PCG node integration.
10. Bake to static mesh.

## Demo Script (Suggested 10–15 Minutes)

### 1. Instant demo command
Script: “Let’s start with a full game-ready scene. I’ll run the demo command and you’ll see a generated tunnel, a lighting rig, and a playable character.”
Action: Open Output Log or console (`~`) and run `IOC.SpawnTunnelDemo`.
Callout: Async generation, playable tunnel, lighting setup.

### 2. Preset showcase
Script: “Presets give you fast, art-directed outcomes without tuning math.”
Action: Place `BP_IOC_Cave`. Cycle `CavePreset` through Large Tunnel, Tight Crawl, Open Cavern, Alien Hive, Canyon Strata.
Callout: Width, feel, and style change immediately.

### 3. Spline network
Script: “Spline mode lets you draw a cave network and IOC fuses it into one tunnel system.”
Action: Enable `bGenerateTunnel` and `bUseSpline`. Add or move spline points.
Callout: Multi-spline branching and distance-field union.

### 4. Biome volumes
Script: “Local overrides let you shape regions without duplicating actors.”
Action: Place `IOCBiomeVolume`. Override `TunnelRadius` or `NoiseFrequency`.
Callout: Localized variation inside a single cave.

### 5. Carving volumes and runtime dig
Script: “You can guarantee open space or create gameplay dig events.”
Action: Add `IOC Carving Volume` component (Sphere/Box/Capsule), adjust falloff. Trigger `CarveAtLocation` at runtime.
Callout: Controlled openings and gameplay carving.

### 6. Decoration scattering
Script: “Scatter layers give you rocks, props, and detail along the cave surface.”
Action: Add `SM_IOC_Stalactite` to `DecorationLayers` with `MinSlopeZ=-1.0`, `MaxSlopeZ=-0.5` and
`bAlignToNormal=true`, then a second layer with `SM_IOC_Rock_B` at `MinSlopeZ=0.7` for the floor.
Set density and Poisson separation.
Callout: Async scatter, slope-aware placement, and eight props shipped with the plugin so the demo
needs no external art.

### 7. Materials and UVs
Script: “IOC generates UVs and smart vertex colors for easy materials.”
Action: Toggle `bGenerateSmartColors`, adjust `TextureTiling`.
Callout: Wall/floor/ceiling blends and triplanar-style UVs.

### 8. LOD and performance
Script: “It stays performant at scale with async and LOD meshes.”
Action: Enable LOD, set `LODDistance` and `LODVoxelSizeMultiplier`. Move camera to trigger swap and show `LastGenerationTimeSeconds`, `LastEstimatedVoxelCount`, and triangle counts.
Callout: Smooth LOD transitions, async computation, and measurable generation cost.

### 9. PCG node integration
Script: “You can run IOC inside PCG graphs for larger workflows.”
Action: Add `IOC Voxel Core` in a PCG graph. Switch between Cellular Automata, Perlin Tunnel, Infinite CA.
Callout: CA vs tunnel modes and point-based integration.

### 10. Bake to static mesh
Script: “When the result is final, bake it for Nanite/Lumen workflows.”
Action: Click `BakeToStaticMesh` on the showcase bake-ready actor and show the asset under `/Game/IOC_Baked`.
Callout: Frozen asset for production pipelines.
