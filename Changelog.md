# Changelog

## [0.4.0] - 2026-09-08
Carving, content and onboarding. Every shipped asset was re-authored on the 5.5 floor, the
plugin gained its own scatter library, the Setup Wizard was rebuilt, and a demo map and
tutorial now give two routes in that need no setup.

**Verified on UE 5.5, 5.6, 5.7 and 5.8** — `BUILD SUCCESSFUL` on all four with zero compile or
link errors. Automation suite **21/21 green on the 5.5 floor**, up from 14 tests, and every
assertion added this cycle was negative-tested: deliberately broken, observed to fail, then
restored.

### ⚡ Performance — carve latency 200 ms to 118 ms (1.7x)
Measured on an 80^3 cave (3200 uu bounds, 40 uu voxels, non-tunnel, one sphere carve).
New per-stage instrumentation, logged when `bLogPresetDebug` is set, drove all of it:

| stage | before | after |
|---|---|---|
| noise fill | 23 ms | 0 ms (replayed) |
| carve | 0.3 ms | 0.3 ms |
| face extract | 10.5 ms | 10 ms |
| **Laplacian smooth** | **73 ms** | **7.2 ms** |
| mesh build | 58 ms | 64 ms |
| overlays | 35 ms | 35 ms |
| **worker total** | **200 ms** | **118 ms** |

- **Laplacian smoothing was allocating a `TSet<int32>` per vertex** to hold adjacency -- one
  hash set per mesh vertex, which made allocator traffic the single largest cost in the whole
  generator (36% of generation time). Replaced with compressed-sparse-row adjacency built in
  two linear passes, with per-slice sort-and-unique reproducing `TSet`'s unique-neighbour
  semantics. The per-vertex update has no cross-vertex dependency, so it now also runs through
  `ParallelFor`. **10x faster on that stage.**
- **Added `FIOCVoxelCache`**: the post-noise, pre-carve voxel field is cached and replayed, so
  a carve no longer re-evaluates the noise. Keyed by a signature covering every input to the
  fill (seed, noise parameters, tunnel geometry, biome overrides, spline segments, actor
  transform, bounds) plus voxel size and grid dimensions -- change any generation parameter and
  the signature stops matching, so a stale field cannot be served and there is no invalidation
  call to forget. Stored as one bit per voxel (a 2M-voxel streamed chunk costs ~250 KB); only
  caves that actually carry carves populate a cache. Toggleable in Project Settings.
- **The carve pass is restricted to the union bounding box of the carves' influence.** Outside
  it the field is identical to the filled or replayed result.

Honest scope note: the mesh is still fully re-extracted and rebuilt on every carve. True
localized re-meshing -- splicing a region of the existing `FDynamicMesh3` -- is *not* done, and
the remaining ~110 ms is entirely mesh build (64 ms) and attribute overlays (35 ms), both bound
by single-threaded `FDynamicMesh3` append APIs.

### 💥 Breaking
- **`AIOCProceduralActor::RuntimeCarves` changed type** from `TArray<FIOCCarvingCapture>` to
  `FIOCCarveHistory`. C++ that iterated it should use `RuntimeCarves.Items` or the new
  `GetRuntimeCarves()`; Blueprint should use `GetRuntimeCarves()`. **The SaveGame layout for
  carve history changed** -- previously saved carve histories will not load.
- `OnRep_RuntimeCarves` is gone; the FastArray notifies through its own hook.

### 🗿 Content — everything re-authored on the 5.5 floor
- **All 13 shipped assets were UE 5.6 packages and could not load on the declared 5.5 floor.**
  A package records the object version that wrote it and an older engine refuses anything above
  its own ceiling, so forward compatibility runs one way only — there is no downgrade path and
  no descriptor setting that helps. Every asset was rebuilt natively on 5.5 by dumping its
  *semantic* content on a newer engine and replaying it through the editor API, then re-dumping
  the rebuild on 5.5 and diffing: 279 values compared, one difference, and that one is an
  engine limitation carrying no data.
- **`Packaging.ContentEngineFloor`** now scans every shipped `.uasset` and `.umap` and fails on
  anything saved above the floor, so this cannot recur silently.

### 🩹 Fixed — cave materials
- **Tunnel interiors rendered transparent from inside the cave.** The three base materials
  were one-sided. A cave is a shell -- `TunnelRadius` of air inside `WallThickness` of rock --
  and the player stands inside it, so the bore's surfaces are backfaces and were being culled:
  you saw through the wall, through the outer shell behind it, and out of the level. All three
  are now two-sided, which also fixes shading, since Unreal flips the vertex normal on
  backfaces for two-sided materials.
- **Every scatter prop rendered as grey default material in packaged builds.** The same
  materials lacked `bUsedWithInstancedStaticMeshes`, and props are placed on HISM components,
  so the engine silently substituted `WorldGridMaterial`. This was invisible in the editor:
  `CheckMaterialUsage` sets the flag at runtime and dirties the package, so the flag was
  recomputed and discarded every session while the shipped product stayed broken.
- Both flags are set by `Resources/SetMaterialRenderFlags.py` and guarded by the
  `Content.MaterialRenderFlags` automation test.

### 🩹 Fixed — six defects in the shipped content
All six predate this release; none was a regression. Four were authoring calls that failed
silently and were never checked.
- 🎨 **`M_IOC_SmartCave` ignored vertex colours entirely.** `VertexColor` was wired to nothing
  and both `LinearInterpolate` alpha pins sat at `const_alpha = 0.5`, so the material rendered a
  fixed 50/50 blend. The authoring script wired outputs named `"Green"`/`"Blue"`; the outputs
  are `R`/`G`/`B`/`A`, so both calls returned false and were ignored.
- 🎨 **`M_IOC_MathRock` ran its noise at default world scale.** The `Noise` position input was
  unconnected and `WorldPosition → Multiply(x0.002)` dangled — the script targeted a pin named
  `"Position"`, which is actually `"World Position"`.
- 🧩 **Both `Subgraph` nodes in `IOC_Graph_Master` had `graph == null`**, so
  `IOC_Fn_OrganicNoise` and `IOC_Fn_ScatterCrystals` were never called by anything.
- 🧩 **Both `StaticMeshSpawner` nodes had meshless entries**, and `NewPCGGraphInstance` was an
  empty stub pointing at no graph.
- 🧩 **`IOC_Fn_ScatterCrystals` computed a density it discarded** — `NormalToDensity_0.Out` went
  nowhere. It now feeds `DensityFilter_0.In`.
- **Root cause fixed too.** `ConnectMaterialExpressions` returns false on an unknown pin or
  output name and changes nothing; ignoring that return is how these shipped. Every authoring
  script now routes through a helper that raises on rejection.

### 🪨 Added — an original scatter library
- **Eight meshes generated with GeometryScripting** and shipped in
  `/InstantOrganicCaves/InstantOrganicCaves/Geometry/`: three rocks from boulder to gravel, two
  crystals, a geode, a stalactite and a stalagmite. Each has two LODs, simple collision, and a
  pivot placed for how it is used — the stalactite hangs from its top, the rest sit on their
  base — so instances neither float nor sink when aligned to a surface.
- **All 19 references to third-party `/Game/Fab/...` meshes are gone from shipped code.** They
  were project assets a customer would not have, so they shipped as dangling references.
- **`Content.ScatterMeshLibrary`** checks all eight for material slots, LOD count and reduction,
  triangle budget, collision, real-world scale and pivot placement. All eight gates have now
  been proven able to fail.

### 🧭 Added — two routes in that need no setup
- 🗺️ **A demo map ships**: `/InstantOrganicCaves/Maps/IOC_DemoMap`, reachable from
  **Tools → Instant Organic Caves → Open Demo Map**. It holds one launcher and a PlayerStart
  and nothing else — every cave is built at runtime on Play, so there is no baked lighting to
  rebuild, nothing to rot, and no content references beyond a native class.
- 📖 **A tutorial ships**: `Resources/Docs/tutorial.html`. The manual documents every property
  and deliberately teaches nothing, which left no guided path for a new customer. The tutorial
  covers demo, wizard, presets, sizing, the six parameters that matter, scatter and
  troubleshooting, with a preset explorer that draws each cave's cross-section to scale from
  the real preset values and a voxel budget calculator running the wizard's own arithmetic.
- 🧹 **`IOC.ClearAllDemos`** removes everything any demo command added, including the playtest
  character, so the commands leave no residue in a customer's level.

### 🪄 Changed — the Setup Wizard
- **Every button was inert.** All 39 used `FCoreStyle "NoBorder"` with a flat border painted
  behind the label, so no hover or press state existed anywhere in the wizard. They now use
  real `FButtonStyle`s with distinct normal/hover/press brushes and a press offset that
  physically depresses the label.
- **Every `SWrapBox` had always been wrapping at 100px.** `SWrapBox` defaults to
  `PreferredSize = 100` with `UseAllottedSize = false`, and all twelve instances set neither —
  so button rows stacked vertically regardless of available width.
- **The hero banner was stretched**: an `SImage` in a fixed-height slot is scaled to the slot's
  shape, not its own. It is now inside an `SScaleBox`.
- The wizard is branded from the product's own key art, its palette sampled from that art
  rather than chosen independently, and it opens on a first run with a working route forward
  instead of a dead end. `IOC.OpenSetupWizard` opens it without the menu.

### 🌐 Network
- **`RuntimeCarves` now delta-replicates.** It was a plain `TArray<FIOCCarvingCapture>`, so
  every carve resent the whole array -- roughly 25 KB to each relevant client against a
  256-entry history. It is now `FIOCCarveHistory`, a `FFastArraySerializer`. The common case
  (append) marks one item and sends one small delta; only FIFO eviction degrades to a full
  array update, because a removal invalidates the replication keys after it.
- Client-side rebuilds are driven by `PostReplicatedReceive`, which fires once per received
  update, rather than by the per-element add/change/remove hooks.

### ✨ Added — carving and instrumentation
- `InstantOrganicCaves.Cave.CarveFieldCacheParity` -- proves a replayed field produces geometry
  identical to a recomputed one, that two consecutive cached generations agree, and that
  changing the seed invalidates the cache rather than serving the stale field.
- `Production.RuntimeContract` now asserts the carve history carries
  `STRUCT_NetDeltaSerializeNative`. Omitting the type trait still compiles and still
  replicates, just as a whole array -- this is the only check that distinguishes the two.
- Per-stage generation timings behind `bLogPresetDebug`.

### 🧪 Verification
- Suite grown from 14 tests to **21**. Seven are new: `Content.ScatterMeshLibrary`,
  `Content.MaterialRenderFlags`, `Content.WizardStyleAssets`, `Demo.DemoMap`,
  `Demo.ShowcaseLifecycle`, `SetupWizard.ButtonSweep` and `SetupWizard.Layout`. The
  fourteenth, `Packaging.ContentEngineFloor`, existed and was the one failure — it passes now
  that the content is authored on the floor.
- **Every new assertion was negative-tested.** Two proved worth the effort by exposing tests
  that could not have failed: a mesh-breaking helper reported success while doing nothing,
  because the editor library it called is a silent no-op outside a real editor session; and an
  early layout test emitted four confident failures that were artefacts of measuring a widget
  Slate had never laid out.

### ⚠️ Known
- `SetupWizard.Layout` checks that every wrap box was built through the idiom that sets
  `UseAllottedSize`, not that the rows actually lay out horizontally. `SWrapBox` reads its
  allotted width from the last real Slate pass, and a widget that has never been ticked in a
  shown window reports zero width and wraps after every child — indistinguishable from the bug.
  A true pixel check needs an interactive editor.
- Runtime carving still re-meshes the whole actor per carve; see the performance note above.

## [0.3.7] - 2026-09-04
Modernization pass. Verified building with zero warnings on UE 5.5, 5.6, 5.7 and 5.8, and the
automation suite runs on the 5.5 floor.

### 🧱 Structure
- **Split out an `InstantOrganicCavesEditor` module (Type: Editor).** The setup wizard (~6.5k lines of Slate), the Tools menu, installation validation, the documentation opener and the authoring capture command all lived in the Runtime module behind `WITH_EDITOR`, which dragged `UnrealEd`, `LevelEditor`, `ToolMenus`, `AssetTools` and `ApplicationCore` into every editor build of a runtime module. They now live in an Editor module, and the wizard's automation tests moved with them.
- The showcase still runs in PIE and packaged builds, so it stayed in the Runtime module; its level-viewport control is inverted through `FIOCShowcaseViewportHooks`, which the editor module binds on startup. The Runtime module no longer depends on `LevelEditor`, `ToolMenus` or `ApplicationCore` at all.
- `FInstantOrganicCavesModule::ValidateInstallation` / `OpenDocumentation` moved to `FInstantOrganicCavesEditorModule`. **API break** for any C++ that called them.
- Editor logging goes to a separate `LogIOCEditor` category.

### ✨ Added
- **Project Settings page** (`UIOCSettings`, Project Settings > Plugins > Instant Organic Caves): fallback cave material as a `FSoftObjectPath` (previously a hardcoded string literal into the plugin's own content, which a project forking the materials could not retarget), max grid cells per axis, max spline samples, and the vertex welding toggle.
- **Blueprint-assignable generation events.** `OnGenerationStartedEvent` and `OnGenerationFinishedEvent` are `BlueprintAssignable`; generation is asynchronous and can take seconds, and Blueprints previously had to poll `bIsGeneratingDisplay` on tick to know when a cave was ready. The native C++ delegates are unchanged and still broadcast.
- `GenerateCave` and `RequestRegeneration` are now `BlueprintCallable`.
- **Vertical chunk streaming.** `AIOCStreamingManager` keys chunks by `FIntVector` and gained `VerticalStreamRadius`. It defaults to 0, which reproduces the previous single-layer behaviour exactly; raising it streams cave systems deeper than one chunk. Note the chunk count scales by `(2 * VerticalStreamRadius + 1)`, so `MaxLoadedChunks` needs raising to match.
- `InstantOrganicCaves.Cave.VertexWelding` regression test: asserts welding preserves every triangle while sharing vertices, and logs the measured saving.

### 🔧 Changed
- **Generated meshes weld their vertices.** The generator emitted four unshared vertices per quad because `FDynamicMesh3` rejects triangles that would create a non-manifold edge -- something diagonal-touching voxels produce constantly. It now shares a vertex per grid corner and falls back to duplicated vertices for only the individual triangles that actually fail, so the manifold guarantee holds and no triangle is lost. Measured **2.93x fewer vertices** (5504 -> 1881 for 2752 triangles) on the test cave, with the same reduction carried into cooked collision. Revertible via the project setting.
- **The PCG node has a version-gated output path.** `UPCGBasePointData` (structure-of-arrays) does not exist on 5.5, so 5.5 keeps `UPCGPointData` + `TArray<FPCGPoint>` while 5.6+ uses `FPCGContext::NewPointData_AnyThread` with the SoA value ranges -- which also picks up `UPCGPointArrayData` where a project has enabled it. The solid-voxel scan was restructured into a count-then-fill pass so both back-ends write output in parallel with no per-point reallocation.
- `TObjectPtr` for all `UPROPERTY` object references on the cave actor, streaming manager and character.
- Editor UI strings converted from `INVTEXT` to `LOCTEXT` in the menus and validation notifications.

### ⚠️ Known
- `InstantOrganicCaves.Packaging.ContentEngineFloor` still fails: the 13 shipped assets were saved by UE 5.6 and cannot load on the 5.5 floor. They have to be re-authored on 5.5; there is no downgrade path.
- The setup wizard's own ~200 UI strings are still `INVTEXT` and remain unlocalizable.
- Runtime carving still revoxelises the whole actor per carve (bursts within a frame are coalesced), and `RuntimeCarves` still replicates as a whole array rather than a `FastArraySerializer` delta.

## [0.3.6] - 2026-09-04
### 🩹 Fixed
- **PCG element allocated UObjects off the game thread.** `FIOCVoxelCoreElement::ExecuteInternal` called `NewObject<UPCGPointData>()` directly; PCG elements run on worker threads (`IPCGElement::CanExecuteOnlyOnMainThread` defaults to false), so this raced the garbage collector. Now uses `FPCGContext::NewObject_AnyThread`.
- **Cellular Automata seeding produced a lattice, not noise.** The initial fill used a single LCG step, which leaves consecutive cells a fixed stride apart and yields regular banding. Replaced with a MurmurHash3-finalised, coordinate-keyed hash that is also stable across grid resizes.
- **Streaming manager held raw `UObject*` chunk pointers** in a non-`UPROPERTY` map, invisible to the GC and left dangling when a chunk was destroyed outside `UnloadChunk`. Now `TWeakObjectPtr`, with an explicit prune pass.
- **Clients overwrote replicated generation settings.** `BeginPlay` re-expanded the cave preset on every peer, clobbering the replicated recipe and producing client geometry (and collision) the server did not have. Preset expansion is now authority-only.
- **The legacy preset migration could hijack runtime actors.** `ShouldAutoPreset()` is a value-match heuristic over user-editable fields; it no longer runs at `BeginPlay`, only in a genuine editor world, so a streamed chunk whose settings coincide with the defaults is not silently converted into a tunnel.
- **Generation bounds were silently truncated.** The working grid is capped per axis; exceeding it shrank the cave with no diagnostic. The cap is raised to 2048 cells per axis and clamping now logs a warning.
- **Splines were chorded, not followed.** Tunnel generation read only `GetLocationAtSplinePoint`, so a two-point spline with tangents produced a straight tunnel and curves were clipped by bounds derived from the same chords. Splines are now sampled by arc length, backed by a new uniform segment grid so the extra segments do not turn the nearest-segment query into an O(voxels x segments) scan.
- **World-space noise tiled.** `FMath::PerlinNoise3D` masks its lattice with `& 255`, repeating roughly every 512 m at the default frequency and folding large seed offsets back onto the same lattice. Replaced with a hash-lattice gradient noise of matching range and distribution, so existing thresholds still apply. Regenerating an existing cave will change its shape.
- **Scatter decorations did not survive a level save.** Decoration HISMs were registered but never added to `InstanceComponents`, the only serialised component list, so props vanished on reload while the cave mesh persisted.
- **Collision was re-cooked on every mesh edit.** Complex-as-simple collision updates are now deferred and cooked once per completed generation.
- **The demo and showcase commands vandalised the user's level.** They took the first DirectionalLight / SkyLight / Fog / PostProcessVolume found in the level, overwrote its settings and renamed it. Environment dressing is now plugin-owned, tagged, transient in editor worlds, and removed by `IOC.ClearShowcase`.
- **`IOC.CaptureWizardPreset`** — an authoring screenshot tool that spawns actors into the open level, moves the viewport and can quit the editor — is now registered only when the editor is launched with `-IOCDevTools`, and its retry ticker is removed on module shutdown.
- **Setup wizard rollback snapshot** held bare `UObject*` references on a Slate widget. `SIOCSetupWizard` is now an `FGCObject` and reports them; the referenced fields use `TObjectPtr`.
- **Authoring Python scripts shipped with absolute paths from the author's machine** (including a user-profile path). Paths are now relative or environment-driven, logging goes to the Output Log, and the four authoring scripts are excluded from packaging in `FilterPlugin.ini`.
- `AIOCCharacter` no longer teleports to an arbitrary cave on `BeginPlay`: the snap is an exposed, documented option, picks the nearest cave, honours spline mode, and runs on the authority only. Input modifiers use `CreateDefaultSubobject` instead of `NewObject` during construction.
- Property edits that cannot affect geometry (bake options, debug toggles, LOD switch distance, runtime-carve limits) no longer trigger a full regeneration.
- Runtime carves within a frame are coalesced into a single rebuild instead of one full revoxelisation each.
- `bShowDebugViz` gizmos persist while the box is ticked. They were issued once from `OnConstruction` into the transient line batcher, which expires them after about a second.

### 🎯 Engine support
- **Declared floor is now UE 5.5, verified on 5.5 / 5.6 / 5.7 / 5.8.** `RunUAT BuildPlugin` succeeds with zero warnings on all four, and the automation suite runs green on the 5.5 floor.
- Nanite bake settings now switch on `UE_VERSION_OLDER_THAN(5, 7, 0)` instead of a bare `ENGINE_MINOR_VERSION >= 7`, which ignored the major version entirely.
- Host project targets pinned to `BuildSettingsVersion.V5` / `EngineIncludeOrderVersion.Unreal5_5`; the previous `V6` / `Unreal5_7` values do not exist before 5.7, so the project could not be opened on the version its content has to be authored on.
- `EngineVersion` in the descriptor set to `5.5.0`.
- New `InstantOrganicCaves.Packaging.ContentEngineFloor` test fails if any shipped package was saved by an engine newer than the floor. **This test currently fails: all 13 shipped assets were saved by UE 5.6 and cannot load on 5.5.** They must be re-authored and saved on 5.5.

### 🔧 Changed
- All plugin logging moved from `LogTemp` to a dedicated `LogIOC` category.
- `PlatformAllowList` narrowed to Win64, matching what has actually been built and validated.
- Descriptor version corrected: it disagreed with the changelog and the readiness doc.
- `UIOCVoxelCoreSettings` exposes `MaxVoxelCount` instead of a hard-coded 15M ceiling.

## [0.3.5] - 2026-07-12
### ✨ Added
- **Authoritative runtime carving:** Reflected carve records now replicate to late joiners, serialize through `SaveGame`, validate radius/input, and obey a bounded FIFO history.
- **Persistent streamed carving:** `AIOCStreamingManager` owns a saveable carve history, routes excavation to affected chunks, and restores it after unload/reload.
- **Production resource budgets:** Hard voxel, triangle, and scatter caps reject unsafe work while preserving the last valid generated mesh.
- **Multiplayer streaming controls:** Authority tracks all player pawns, unions their desired chunks, caps retention, throttles loads, and releases chunks after the last player leaves.
- **Seam-safe streamed density:** Voxel/LOD-aligned chunk dimensions, a one-voxel neighbor halo, stable global seeding, and locked smoothing boundaries prevent artificial interface caps.
- **World-aligned generated UVs:** World-space chunks now project generated UVs from world coordinates so UV-driven materials do not restart at every chunk origin.
- **Production bake controls:** Stable unique naming, material override, lightmap UV/tangent generation, optional generated LOD, optional Nanite, and selectable collision mode.
- **Runtime status:** Blueprint-visible success, failure reason, timing, voxel, triangle, LOD, and scatter metrics.
- **Regression coverage:** Added runtime-contract, generation-budget, streamed-seam, and streamed-carve-persistence automation tests.

### 🔧 Changed
- Cave transforms now participate correctly in world-space noise, biome, and carve sampling.
- Generation cancellation is lifecycle-safe and checks cancellation during expensive parallel voxel/carve work.
- LOD distance uses generated mesh bounds and never switches to an invalid LOD.
- Player coupling is opt-in and disabled for network games unless explicitly allowed.
- Demo/showcase commands are non-shipping cheat commands; the showcase launcher is inert in Shipping.
- Plugin version advanced; verified platform declarations remain limited to platforms recognized by stock UE 5.7.

### 🩹 Fixed
- Removed global debug-line flushing that could erase unrelated project visualization.
- Removed module delegate leakage during shutdown.
- Removed plugin-owned UE 5.7 deprecation warnings for networking and Nanite access.
- Fixed Shipping compilation of the showcase launcher.
- Fixed world-space generation seeds being ignored and per-chunk seeds breaking density continuity.

## [0.3.4] - 2026-06-19
### ✨ Added
- 🎨 **Pure Math Material:** Developed `M_IOC_MathRock`, a zero-footprint master material that uses World Position and 3D Simplex Noise to generate organic rock strata without relying on texture memory.
- 🪨 **Hybrid PBR Pipeline:** Upgraded the Master Material with a `Use PBR Texture` switch, instantly swapping between Pure Math and traditional Textures.
- 💎 **Seamless Biome Textures:** Packaged three new 4K tileable rock textures directly into the plugin: Obsidian Flow, Limestone Crawl, and Alien Hive.
- 🗺️ **Triplanar Projection:** Implemented automatic Box Mapping to prevent texture stretching on generated vertical cave walls.
- 📚 **Vector Documentation:** Overhauled the shipped `index.html` manual. Replaced static screenshots with clean, responsive SVG vector illustrations and added comprehensive guides for the new Hybrid Texture workflow.
- 🚀 **Fab-Ready Distribution:** Executed a clean Unreal Automation Tool build, securely baking all new Python scripts, materials, and textures into the final `C:\PACK57` release.

## [0.3.3] - 2026-06-19
### ✨ Added
- **Readiness Guard:** Showcase flythrough camera now intelligently defers its start until all asynchronous procedural caves have completed generating (or up to a 30s fallback timeout).
- **Flythrough Looping:** Added `bLoopShowcase` property to `AIOCShowcaseLauncher`, giving users control over whether the showcase flythrough loops continuously or halts after one pass.

### 🩹 Fixed
- **Showcase Robustness:** Refactored the showcase spawn pipeline into resilient conditional blocks. If a specific section's actor fails to spawn, the system gracefully skips it without crashing or desyncing the camera.
- **Concurrency Safety:** Added a strict `bIsActive` lock to `ShowcaseState` with hardened reset logic to prevent overlapping showcases, memory pollution, and hot-reload errors.
- **Python Drift Prevention:** Fully deprecated `IOC_CreateShowcaseMap.py` via runtime warnings and comments, establishing the C++ Setup Wizard as the single source of truth for starter map configuration.
- **Shipping Build Compatibility:** Wrapped editor-only setup wizard capture functionality inside `#if WITH_EDITOR` blocks, completely resolving compilation errors in Game/Runtime/Shipping targets.

## [0.3.2] - 2026-06-19
### ✨ Added
- **Dedicated Tools Menu:** Added `Tools > Instant Organic Caves` with one-click access to the setup wizard, validation, documentation, demo spawns, showcase launch, and showcase cleanup.
- **Launcher Quick Actions:** `AIOCShowcaseLauncher` now exposes clearer Call In Editor actions for standard showcase start, capture showcase start, and showcase cleanup, with friendlier property labels and tooltips.
- **Wizard Quick Start UX:** The setup wizard now exposes a welcome-page project snapshot, quick-start actions, clickable step navigation, and preset-to-custom workflow shortcuts.

### 🔧 Changed
- **Typed Showcase/Demo API:** Wizard and launcher flows now call a shared native module API instead of relying on string-built `GEngine->Exec` commands.
- **Validation Feedback:** Installation validation now also shows editor notifications while still writing the full report to the Output Log.
- **Documentation Access:** Wizard and editor menu actions now share a single documentation opener that prefers the local shipped docs and falls back to the website.
- **Wizard Practicality:** Review now links directly to starter assets, preview-side workload visibility is stronger, and safer-first-pass actions surface earlier in the flow.

## [0.3.1] - 2026-05-16
### 🩹 Fixed
- **Standalone Packaging:** Fixed Win64 game-target packaging by restoring the runtime material-domain include used by fallback cave materials.
- **Native Setup Flow:** The setup wizard no longer depends on blind Python execution for starter assets or showcase-map creation; both workflows now use built-in editor APIs with explicit success/failure handling.
- **Validation Messaging:** Installation checks now treat Python automation as optional and correctly describe starter assets as shipped plugin content.
- **Release Metadata:** Added creator/support URLs and narrowed the declared verified platform list to Win64.

## [0.3.0] - 2026-04-02
### ✨ Added
- **Automated Showcase Demo:** `IOC.SpawnShowcase` now spawns a cinematic flythrough across 8 sections, covering drop-in actor workflow, path-driven tunnels, presets, decoration scattering, carving, real streaming-manager chunks, live metrics, and bake-ready output.
- **Capture-Ready Showcase:** New `IOC.SpawnShowcaseCapture` command and `AIOCShowcaseLauncher` actor start a 60-90 second showcase with polished captions and no debug HUD.
- **Showcase Map Setup:** New `Resources/IOC_CreateShowcaseMap.py` creates `/Game/IOC_Showcase` with an auto-start launcher.
- **Showcase Cleanup:** New `IOC.ClearShowcase` console command destroys all showcase actors and restores camera.

## [0.2.0] - 2026-02-05
- Initial Release
- Features: Perlin Tunnels, Cellular Automata, Async Generation, Smart Materials, Decoration Scattering.
