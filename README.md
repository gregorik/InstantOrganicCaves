# Instant Organic Caves (IOC)

**A high-performance, strictly programmatic voxel environment generator for Unreal Engine 5.7.**

![Unreal Engine 5.7](https://img.shields.io/badge/Unreal%20Engine-5.7-black?style=flat&logo=unrealengine)
![Language](https://img.shields.io/badge/Language-C%2B%2B-blue)
![License](https://img.shields.io/badge/License-MIT-green)

## 📖 Overview

**Instant Organic Caves (IOC)** is a plugin designed to generate massive, seamless, and organic cave systems procedurally at runtime. Unlike traditional marketplace assets that rely on static meshes or Blueprint construction scripts, IOC utilizes a **"Metal C++"** approach.

It builds geometry directly using Unreal's `FDynamicMesh3` core math libraries, bypassing overhead from the Blueprint VM and intermediate wrapper libraries. The result is a system that can generate infinite, seamless, Nanite-enabled environments suitable for high-fidelity production.

## ✨ Key Features

*   **Pure C++ Architecture:** No dependency on Blueprint logic, PCG Graphs, or external assets. The geometry is calculated mathematically from scratch.
*   **Direct Dynamic Mesh Manipulation:** Operates directly on `UE::Geometry::FDynamicMesh3` for maximum performance.
*   **Infinite World Generation:** Supports seamless chunk tiling using world-space Perlin Noise.
*   **Organic Smoothing:** Implements custom, stable Laplacian Smoothing to convert blocky voxel grids into smooth, eroded cavern walls.
*   **Welded Topology:** Custom meshing algorithm ensures watertight geometry with no internal faces, preventing shading artifacts and mesh tearing.
*   **Nanite & Lumen Ready:** programmatically enables Nanite support on generated meshes.

## 📦 Installation

1.  **Prerequisites:** Unreal Engine **5.7** (Source or Launcher) and Visual Studio 2022.
2.  **Clone:** Clone this repository into your project's `Plugins` folder:
    ```bash
    YourProject/Plugins/InstantOrganicCaves
    ```
3.  **Regenerate:** Right-click your `.uproject` file and select **Generate Visual Studio Project Files**.
4.  **Build:** Open the solution in Visual Studio and build the project. The plugin handles dependencies on `GeometryCore`, `DynamicMesh`, and `PhysicsCore` automatically.
5.  **Enable:** Launch the Editor, go to **Edit > Plugins**, and ensure **Instant Organic Caves** is enabled.

## 🚀 Quick Start

### 1. Single Chunk Generation
1.  In a New Scene, open the **Place Actors** panel.
2.  Search for `IOCProceduralActor`.
3.  Drag it into the viewport.
4.  A cave chunk will generate immediately. Use the **Details Panel** to adjust settings like `Seed`, `Roughness`, and `Cave Size`.

### 2. Infinite World Generation
1.  Search for `IOCWorldGenerator` in the Class list.
2.  Drag it into the level.
3.  In the Details Panel:
    *   Set **Chunk Class** to `IOCProceduralActor`.
    *   Set **Render Distance** (e.g., `2` for a 5x5 grid).
    *   Set **Update Interval** (e.g., `0.5` seconds).
4.  Press **Play**. As the camera moves, new cave chunks will form ahead of you seamlessly.

## ⚙️ Configuration Guide

The `IOCProceduralActor` exposes several critical parameters to control the algorithm:

### Generation Settings
| Parameter | Default | Description |
| :--- | :--- | :--- |
| **Grid Size** | `60` | The resolution of the chunk (X, Y, Z). Higher values = more detail but higher RAM usage. |
| **Voxel Size** | `100.0` | The size of a single logic unit in World Space (cm). |
| **Dimensions** | `60,60,60` | Allows for non-cubic chunks (e.g., creating long corridors). |

### Noise & Shape
| Parameter | Default | Description |
| :--- | :--- | :--- |
| **Noise Scale** | `2500.0` | **Critical.** Controls the size of the tunnels. <br>• **2500+**: Large, open caverns.<br>• **1000**: Tight, winding tunnels.<br>• **<100**: Will cause white noise/fragmentation. |
| **Noise Threshold** | `0.5` | The "Air vs. Solid" cutoff. Lower values make thicker walls. |
| **Fill Probability** | `0.45` | Impact of the initial cellular automata seeding pass. |

### Organic Processing
| Parameter | Default | Description |
| :--- | :--- | :--- |
| **Smooth Iterations** | `5` | How many times the Laplacian smoothing kernel runs. <br>• **0**: Minecraft/Blocky look.<br>• **5**: Organic/Melted rock look. |
| **Surface Roughness**| `1.0` | Adds jitter to vertices before smoothing to simulate natural erosion. |

## 🧠 Technical Architecture

Calculations differ from standard Marching Cubes to allow for specific artistic styles:

1.  **Noise Sampling:** We sample 3D Perlin Noise in World Space. To prevent "Integer Lattice" artifacts (zebra striping), coordinate sampling is jittered by a prime-number vector offset.
2.  **Cellular Automata:** A pass of 3D Cellular Automata runs on the voxel data to clean up noise and form coherent structures.
3.  **Face Culling & Welding:**
    *   The mesher iterates over the grid.
    *   Faces are only generated if a solid voxel neighbors an air voxel.
    *   **Crucial:** A `VertexMap` is employed to reuse vertex indices. This "Welds" the mesh topology, ensuring that adjacent cubes share vertices. This allows the smoothing algorithm to act on the mesh as a continuous skin rather than separate blocks.
4.  **Laplacian Smoothing:** A custom, dependency-free smoothing algorithm iterates over the vertex connectivity graph, relaxing vertex positions toward the average of their neighbors to create organic curves.

## ⚠️ Troubleshooting / FAQ

**Q: The mesh looks shattered or like floating confetti.**
*   **A:** Your `Noise Scale` is likely too low relative to your `Voxel Size`. If the noise frequency is too high, the algorithm generates isolated, single-voxel islands that collapse during smoothing. **Increase Noise Scale to 2500.0 or higher.**

**Q: I see "Zebra Stripes" or repeating patterns.**
*   **A:** This is Perlin harmonic alignment. Ensure you are using the latest version of the code which includes the `Origin` coordinate jitter fix in `GenerateCave()`.

**Q: Collision isn't working.**
*   **A:** IOC uses `ComplexAsSimple` for collision. Ensure your Player Pawn has a valid collision capsule and physics are enabled.

## 🤝 Contributing

This is an open-source project. Pull requests are welcome, particularly for:
*   Async background thread generation (TaskGraph integration).
*   LOD handling for the Infinite World Generator.
*   Biomes support (varying noise parameters based on world position).

## 📜 License

Distributed under the GPL 3.0 License. See `LICENSE` for more information.
