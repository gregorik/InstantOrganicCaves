# Instant Organic Caves (IOC)
![image](https://img.shields.io/badge/-Unreal%20Engine-313131?style=for-the-badge&logo=unreal-engine&logoColor=blue) ![image](https://img.shields.io/badge/C%2B%2B-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=blue) ![image](https://img.shields.io/badge/Python-FFD43B?style=for-the-badge&logo=python&logoColor=blue) ![image](https://img.shields.io/badge/json-5E5C5C?style=for-the-badge&logo=json&logoColor=white) ![image](https://img.shields.io/badge/MIT-green?style=for-the-badge) ![alt text](https://img.shields.io/github/stars/gregorik/InstantOrganicCaves) ![alt text](https://img.shields.io/badge/Support-Patreon-red) [![YouTube](https://img.shields.io/badge/YouTube-Subscribe-red?style=flat&logo=youtube)](https://www.youtube.com/@agregori) [![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/C0C616ULD4)


**A high-performance, strictly programmatic voxel environment generator for Unreal Engine 5.5 through 5.8.**

[Intro video 1 (really old and basic)](https://www.youtube.com/watch?v=xURtHTS8Stk) <br>
[Update video 1](https://www.youtube.com/watch?v=-H_Fm3QvMXA) <br>
[There is an updated + supported version at Fab if interested](https://www.fab.com/listings/016b22dc-d04c-41fa-857c-d4f391a96c12) <br>
[Discord support](https://discord.gg/nqYQ5mtmHb)


## 📖 Overview

**Instant Organic Caves (IOC)** is a plugin designed to generate massive, seamless, and organic cave systems procedurally at runtime. Unlike traditional marketplace assets that rely on static meshes or Blueprint construction scripts, IOC utilizes a purist C++ approach.

It builds geometry directly using Unreal's `FDynamicMesh3` core math libraries, bypassing overhead from the Blueprint VM and intermediate wrapper libraries. The result is a system that can generate infinite, seamless, Nanite-enabled environments suitable for high-fidelity production. 

*If you have consulting and/or custom pipeline integration in mind: I offer dedicated architecture consulting for production games & projects.* 📬 Please [contact me](https://gregorigin.com/contact.html) or see my [extended portfolio](https://www.gregorigin.com/Portfolio/). 👨‍💻 

 <br><br>

| <i><b>Comparison | <i><b>GitHub version (0.4+ MIT)           | <i>[FAB edition](https://www.fab.com/listings/016b22dc-d04c-41fa-857c-d4f391a96c12) (Fab)</b></i>                |
|:---|:---|:---|
| **Version** | v0.4.0 (Full Source) | v0.4.0 (Vetted Distribution) |
| **Distribution** | Source only (Build from source) | Pre-built binaries, launcher install |
| **Engine support** | UE 5.5 – 5.8 | UE 5.5 – 5.8 |
| **Procedural Perlin caves/tunnels** | Included | Included |
| **Cellular automata caves** | Included | Included |
| **PCG Integration** | Included | Included |
| **Edge falloff / Clean shells** | Included | Included |
| **Debug visualization** | Included | Included | 
| **Multi-Spline Branching** | Included | Included |
| **Infinite/tileable cellular automata** | Included | Included |
| **Domain warp & terraces** | Included | Included | 
| **Async generation** | Included | Included |
| **Async decoration scattering** | Included | Included |
| **Original Scatter Mesh Library (8 meshes)** | Included | Included |
| **LOD mesh generation** | Included | Included |
| **Complex-as-Simple Collision** | Included | Included |
| **Streaming Manager** | Included | Included |
| **Auto UV's (Triplanar)** | Included | Included |
| **Runtime Carving API & FastArray Replication** | Included | Included |
| **Carving Volume Component** | Included | Included |
| **5 Built-in Presets** | Included | Included |
| **Smart Vertex Colors** | Included | Included |
| **Setup Wizard & Slate UI** | Included | Included |
| **Interactive Demo Map & Showcase** | Included | Included |
| **Laplacian CSR (10x faster) & Voxel Cache** | Included | Included |
| **21 Automated Verification Tests** | Included | Included |
| **Interactive HTML Tutorial & Manual** | Included | Included |
| **Updates** | GitHub Releases | Regular, vetted by Epic |
| **Support** | GitHub Issues | Discord & Fab |
<br><br>

## ✨ Key Features

*   **Pure C++ Architecture:** Operates directly on `UE::Geometry::FDynamicMesh3` for maximum performance, with zero blueprint VM overhead.
*   **Integrated Setup Wizard:** 5-step guided Slate wizard (`Window -> IOC Setup Wizard...`) with preflight voxel workload estimation, 3D preview, risk analysis, and one-click recommended fixes.
*   **Shipped Interactive Demo Map:** Open `/InstantOrganicCaves/Maps/IOC_DemoMap` and press Play for an automated 8-section feature tour (`IOCShowcaseLauncher`).
*   **Optimized Carving Engine (v0.4.0):** 1.7x faster carve latency (118 ms vs 200 ms) via compressed-sparse-row (CSR) Laplacian smoothing with `ParallelFor` and bit-packed `FIOCVoxelCache`.
*   **Replicated Multiplayer Carving:** `FIOCCarveHistory` FastArray replication ensures synchronized carve history for late-joining players.
*   **Shipped Procedural Scatter Library:** 8 original baked GeometryScripting meshes (`SM_IOC_*` rocks, stalactites, stalagmites, geodes, crystals) with LODs and simple collision—completely free of third-party art dependencies.
*   **Multi-Spline & Path Branching:** Generate complex tunnel networks following arbitrary 3D curves and branches.
*   **Domain Warping & Terracing:** Create alien hives, canyon strata, and stepped cavern topography with multi-octave fBm noise.
*   **Smart Triplanar Materials & Vertex Colors:** Pre-configured materials with runtime usage flags, two-sided rendering, and smart vertex colors (Wall, Floor, Ceiling).
*   **Multiplayer Chunk Streaming:** `AIOCStreamingManager` provides distance-based chunk loading, unload hysteresis, and safe player tracking.
*   **In-Editor Static Mesh Baking:** Convert procedural runtime caves into permanent Nanite-enabled static meshes with collision and lightmap UVs under `/Game/IOC_Baked`.
*   **Engine Floor UE 5.5–5.8:** Assets authored natively on UE 5.5 floor to guarantee compatibility across all modern Unreal Engine versions.

## 📦 Installation

1.  **Prerequisites:** Unreal Engine **5.5, 5.6, 5.7, or 5.8** and Visual Studio 2022.
2.  **Clone:** Clone this repository into your project's `Plugins` folder:
    ```bash
    YourProject/Plugins/InstantOrganicCaves
    ```
3.  **Regenerate:** Right-click your `.uproject` file and select **Generate Visual Studio Project Files**.
4.  **Build:** Build your project solution in Visual Studio or launch Unreal Editor (it will compile the plugin automatically).
5.  **Enable:** Go to **Edit > Plugins** and ensure **Instant Organic Caves** is enabled.

## 🚀 Quick Start

### 1. Interactive Demo Map (Fastest)
1. In Unreal Editor, go to **Tools > Instant Organic Caves > Open Demo Map**.
2. Press **Play (PIE)**.
3. Enjoy the automated 8-section cinematic showcase highlighting all cave generator features.

### 2. IOC Setup Wizard
1. Open **Window > IOC Setup Wizard...** (or run console command `IOC.OpenSetupWizard`).
2. Step through the 5-step guided setup: choose a preset, configure bounds, review estimated voxel counts, and spawn your first cave.

### 3. Drag-and-Drop Workflow
1. In the **Place Actors** panel, search for `IOCProceduralActor`.
2. Drag it into the viewport.
3. Select any of the 5 built-in presets in the Details panel:
   - `LargeTunnel`
   - `TightCrawl`
   - `OpenCavern`
   - `AlienHive`
   - `CanyonStrata`

### 4. Console Commands
| Command | Description |
| :--- | :--- |
| `IOC.OpenSetupWizard` | Opens the Slate Setup Wizard window |
| `IOC.SpawnShowcase` | Spawns the 8-section product showcase tour |
| `IOC.ClearShowcase` | Removes showcase actors and restores camera |
| `IOC.ClearAllDemos` | Removes all demo caves and characters cleanly |
| `IOC.SpawnTunnelDemo` | Spawns an instant path-driven tunnel demo |
| `IOC.SpawnSpectacular` | Spawns a domain-warped alien hive cavern |
| `IOC.ValidateInstallation` | Verifies plugin assets, modules, and engine floor integrity |

## 📚 Documentation & Tutorial

* **Interactive Tutorial:** Open `Resources/Docs/tutorial.html` in any web browser for an interactive preset explorer, cross-section visualizer, and voxel budget calculator.
* **Full Manual:** Open `Resources/Docs/index.html` (or via **Tools > Instant Organic Caves > Open Documentation**) for complete API and property references.
* **Demo Guide:** Check `Resources/Docs/IOC_DEMO_GUIDE.md` for a walkthrough of all showcase sections.

## 🤝 Contributing

Pull requests and issues are welcome! See `Changelog.md` for full version history and `PRODUCTION_READINESS.md` for architectural contracts.

## 📜 License

Distributed under the MIT License. See `LICENSE` for more information.
