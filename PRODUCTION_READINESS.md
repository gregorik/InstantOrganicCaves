# Instant Organic Caves 0.4.0 — Deployment Contract

IOC 0.4.0 provides bounded asynchronous generation, replicated deterministic cave recipes, authority-owned carving, multiplayer-aware streaming, persistent streamed excavation, and production static-mesh baking for Unreal Engine 5.5 through 5.8.

## Runtime

- `MaxVoxelCount`, `MaxGeneratedTriangles`, and `MaxScatterInstances` are hard limits.
- Rejected generation preserves the last valid mesh and reports `LastGenerationError`.
- Generation is cancellable and actor/world teardown is safe while background work is active.
- World-space noise, biome lookup, and carving honor the full actor transform.
- Final dynamic-mesh and collision installation occurs on the game thread; profile target hardware.

## Multiplayer

- `AIOCProceduralActor` replicates its transform, generation recipe, material/decor settings, LOD settings, and carve history.
- Peers rebuild geometry locally; mesh buffers are not replicated.
- `CarveAtLocation` and `ClearRuntimeCarves` are authority-only.
- Host gameplay code must validate a client request in its own server RPC before calling IOC.
- `RuntimeCarves` replicates to late joiners and is marked `SaveGame`.
- Direct runtime edits to `CaveSpline` points need a project-owned replicated spline recipe; replicated tunnel endpoints can be used for simple paths.

## Streaming

- Authority tracks all possessed players and uses capped, throttled chunk loading with unload hysteresis.
- No-player cleanup and safe opt-in player coupling prevent abandoned chunks and surprise teleports.
- World-density chunks use a common seed, voxel/LOD-aligned spacing, a one-voxel neighbor halo, and fixed smoothing boundaries.
- `GetEffectiveChunkSize()` returns the aligned dimensions actually used.
- `CarveStreamedCavesAtLocation` stores a bounded `SaveGame` history and reapplies affected carves after chunk reload.
- `ClearStreamedRuntimeCarves` clears manager-owned history and loaded chunks.

`SaveGame` metadata does not create a save system. Serialize and restore the relevant cave actor or streaming manager in the host game's persistence layer.

## Baking

`Bake To Static Mesh` creates a unique asset under `/Game/IOC_Baked` with material/normal/UV/color data, lightmap UVs, tangents, optional generated LOD, optional Nanite, and selectable collision. Save the dirty package before closing or cooking.

## Modules

- `InstantOrganicCaves` (Runtime) -- generation, actors, components, the PCG element, and the showcase. Depends on no editor UI modules.
- `InstantOrganicCavesEditor` (Editor, PostEngineInit) -- setup wizard, Tools menu, installation validation, documentation, authoring capture command.
- The runtime module keeps `UnrealEd`, `AssetTools`, `AssetRegistry` and the mesh-description modules under `bBuildEditor` only, for the `Bake To Static Mesh` CallInEditor action that necessarily sits on the runtime actor.

## Engine support

- **Floor: UE 5.5.** The module compiles and links cleanly on 5.5, 5.6, 5.7 and 5.8 (`RunUAT BuildPlugin`, Win64, zero warnings).
- **All shipped content must be authored and saved on 5.5.** Package compatibility runs forward only: a `.uasset` written by 5.6 records legacy version -9 and object version 1018, and 5.5 accepts at most -8 / 1014, so it will not load there. There is no downgrade path -- an asset saved by a newer editor has to be rebuilt on the floor version.
- `InstantOrganicCaves.Packaging.ContentEngineFloor` enforces this automatically. It reads each shipped package header directly (the engine's own `FPackageFileSummary` reader zeroes the version fields for packages it considers too old, so it cannot be used for this check) and fails with the offending filenames.
- Host project targets are pinned to `BuildSettingsVersion.V5` / `EngineIncludeOrderVersion.Unreal5_5`; `V6` and `Unreal5_7` do not exist before 5.7 and would stop the project opening on the floor.
- The Nanite bake path selects `Get/SetNaniteSettings` (5.7+) or the `NaniteSettings` field (5.5/5.6) via `UE_VERSION_OLDER_THAN`.
- `EngineVersion` in the descriptor reads `5.5.0`. Fab uploads are per engine version; set it to match each packaged build.

## Shipping and platform scope

- Demo/showcase commands are non-shipping cheat commands.
- `AIOCShowcaseLauncher` is inert in Shipping.
- Automation code is excluded when development automation is disabled.
- UE 5.7 Win64 Editor, Shipping, the isolated Win64 `BuildPlugin` package, and all 10 IOC automation tests are validated.
- `PlatformAllowList` declares Win64 only, matching what has actually been built and tested. Mac, Linux, Android and iOS are buildable in principle but were never validated, so they are not advertised; re-add them to the descriptor once each has had a build, profiling and certification pass.

See `Resources/Docs/index.html` for the user manual and `Changelog.md` for release details.
