# Wave 3, feature #13: landscape paint layers, rivers and lakes, wave-aware buoyancy (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: weightmap paint layers on `.ocland`, a layered terrain material (N layers, height blend) in the one material evaluator, per-layer physical surface (footsteps, friction), a layer foliage scatter command, spline rivers and closed-spline lakes as `.ocworld` records drawn through the ordinary material path, flow driving the water material's normal scroll, optional terrain carving, and buoyancy that reads the real Gerstner wave and river current. Not in scope: runtime (gameplay-time) painting, vertex waves on rivers and lakes, swimming locomotion, tributaries/junction merging, erosion, non-destructive (edit-layer) carving, virtual texturing.

## 0. Decisions in one screen

1. **No new module.** Paint, carve and weights live in `modules/landscape` (+ `modules/formats` for `.ocland`); spline, river/lake meshes and water queries in `modules/fluids`; the join is `Runtime` (`GameLandscape`, `GameWater`), as today. Hence no new `module.json`. The sampling seams to C# and to physics are *provider callbacks* (the `aver_fw_set_fluid_spawn_provider` pattern, `GameWater.cpp:87`), so physics and framework still link nothing.
2. **The layered terrain is a material feature, not a landscape pipeline.** Same argument the material header already makes for the slope layer (`Material.hpp`, "A MATERIAL FEATURE, NOT A LANDSCAPE FEATURE"): a landscape-owned pipeline would take over the scene PSO (`VoxiRenderer::overridesScenePipeline` is all-or-nothing) and lose GI, shadows and ray hits. So terrain layers are one new `MaterialFlag` evaluated in `material_prelude.hlsl` for raster and in `rtHitSurface` for rays, through the existing 8 texture slots (reinterpreted under the flag). **`MaterialConstants` does not grow** (it is at 192 of the 256-byte draw cap, `RHIResources.hpp:368`).
3. **Layers are defined in the `.ocmat` the `LANDSCAPE` record already names** (`TERRAIN` + `LAYER` records), not in a new asset. Paint data in `.ocland` refers to layers **by name**, so reordering or editing the material never scrambles paint.
4. **Weights are 8-bit, one per layer per height sample**, layer 0 implicit (`255 - sum`), so a fresh landscape stores nothing and unpainted terrain is layer 0 everywhere, ring tiles included. Max 8 layers per material.
5. **Rivers and lakes are `.ocworld` records** (`RIVER`/`RIVERPT`, `LAKE`/`LAKEPT`), edited in the Landscape mode panel (new Water tab), built into plain static meshes (`IDevice::createMesh`), drawn through `bindSurfaceLook` + `drawMesh` like `drawDisplaced`. Rays see them because they are ordinary geometry (same reason `WaterSurface` is a mesh, `WaterSurface.hpp` header).
6. **Flow rides in mesh `uv0`** of river meshes (world XY velocity, cm/s) and is consumed by two new material-graph nodes. River materials must not sample textures by mesh UV (they are world-position based already, `G_WaterRipple.ocgraph`).
7. **Carving is a one-shot, undoable editor command** that writes `.ocland` heights (a normal `LandscapeStroke` undo entry). Rivers are never re-carved automatically; moving a river later does not move the bed.
8. **Buoyancy reads a host-supplied sampler** (`aver_phys_set_water_sampler`), called per dynamic body per fixed substep, replacing the infinite flat plane. The host's sampler answers from lakes, rivers and the ocean with CPU Gerstner evaluated at the GPU's own clock. This also fixes the bounded-`WATER` bug: today the plane is infinite whatever the record's bounds say (`BuoyancyAbi.cpp:set_water_plane`, `Buoyancy.cpp` `evaluate`).

## 1. What exists today, and what is missing

| Need | Exists | Missing |
|---|---|---|
| Height grid | `OcLandData` (`OcLand.hpp:28`): `heights` f32, pinned quant range (`LRNG`), optional chunks ignored by old readers (`parseAvr1` never checks Required, `Avr1.hpp`) | Any per-sample layer data; layer names |
| Sculpt | `Sculpt.hpp` (`BrushMode` Raise/Lower/Smooth/Flatten/Ramp/Noise, `brushRect`, `applyBrush`), `HeightfieldRay.hpp`; editor `handleSculpt` (`SandboxViewport.cpp:1772`), `beginSculptStroke/endSculptStroke` (`:410/:416`), `GameLandscape::beginStroke/growStroke/endStroke/sculpt` (`GameLandscape.cpp:439-505`), undo kind `EditCmd::Kind::LandscapeStroke` (`SandboxApp.hpp:1987`, `SandboxSelection.cpp:1274/1327`) | Paint brush, weight undo, carve |
| Landscape surface | One flat material per renderer: `LandscapeRenderer::setSurfaceBinding(set, constants, bytes)` (`LandscapeRenderer.hpp`), bound by `GameLandscape::applySurface` (`GameLandscape.cpp:140`) from the `LANDSCAPE` record's `material` (`OcLandscapePlacement::material`, `OcWorld.hpp:240`). `LandVertex` == `rhi::MeshVertex` (32 B, uv0 only), UVs world-aligned (`ChunkMesh.hpp`) | Per-sample blend of N materials; ring tiles (`RingTile`) have no paint |
| Material slots | 8 texture slots, `TextureSlot` order (`Material.hpp`), one binding set per material (`MaterialSystem.hpp`), ray path reads the same slots from a bindless table (`texIndex[8]`, `averRtSampleSlot`, `voxi_rt.hlsli:98`); slope-blended second layer (`averBlendLayers`, `material_prelude.hlsl:384`; ray copy at `voxi_rt.hlsli:349-365`) | N-layer blend; per-section textures (weightmaps) in a material; a texture source that is not a file |
| Ground query | `GameLandscape::groundHeightAt`, `surfaceHeightAt` (`HeightfieldRay.hpp`) | Normal, layer weights, physical surface; any C#/graph access to terrain (grep: the only C# mentions are comments, `Physics.cs:92`, `GraphInterop.cs:35`) |
| Physical material | Per-body friction/restitution setters (`physics_abi.h` "Material and mass"); contact listener runs on worker threads and ignores `ContactSettings` (`PhysicsWorld.cpp:83`); terrain body is an unowned heightfield (`GameLandscape.cpp:65-90`) | Any per-surface value; footstep source of truth |
| Water records | `WATER` (+`WAVE`) parse/write (`OcWorld.cpp:490/541`, `:1062/:1108`), `OcWaterPlacement` (`OcWorld.hpp:312`): rectangle bounds or infinite only | Spline bodies; only the first `WATER` is rendered (`GameWater.cpp:105+`, warning) |
| Water render | `WaterRenderer` analytic, `WaterSurface` compute-displaced grid (`WaterSurface.hpp`), ripple normals from the *global* 3-wave set via `WaveNormal` graph node (`MaterialGraphHlsl.cpp:641`, `shared_prelude.hlsl:66-113`); water look = ordinary `.ocmat` (volume absorption + scatter, `Material.hpp`, `M_SeaWater.ocmat`) | Flow; any water that is not a camera-following grid |
| Buoyancy | One global infinite plane + per-body overrides, evaluated in the fixed loop before `Update` (`PhysicsWorld.cpp:410-433`, `Buoyancy.cpp:evaluate`); host feeds a flat height (`GameWater.cpp:275-283`) | Wave height/normal/current per point. `gerstnerHeightCm` evaluates at the *rest* coordinate (`GerstnerWave.hpp`), not at the displaced world position, and its only callers are tests (`fluids/README.md`) |
| Foliage | `.ocfoliage` types, brush `handleFoliage`/`foliagePlaceOne` (`SandboxViewport.cpp:492/542`), `FoliageStroke` undo (`SandboxSelection.cpp:1208`), terrain-aware rotation (`FoliageAlign.hpp:122`); procedural scatter `ChunkGenerator.hpp` | Layer-driven placement |
| Graph nodes | Table-driven generic emitter (`GraphGameSystemNodeTable.cs`, `Table()` ends `.Concat(GasTable())` at `:120`), palette mirror `sandbox/src/GraphNodeDefs*.hpp`; material-graph emitter `MaterialGraphHlsl.cpp` with palette in `sandbox/src/GraphNodeDefs.hpp:998+` | Terrain/water nodes (game and material) |

Stale docs to fix as part of this work: `modules/landscape/README.md` (still says no pixel was ever drawn), `modules/fluids/README.md` ("nothing else calls" the wave math), `docs/LANDSCAPE_EDITOR.md` (texture painting listed as deferred).

## 2. Design

### 2.1 `.ocland` paint data (`modules/formats`)

`OcLand.hpp` additions (all optional on disk):

```cpp
inline constexpr u32 kOcLandChunkLayers  = avrFourCC("LLYR");
inline constexpr u32 kOcLandChunkWeights = avrFourCC("LWGT");
inline constexpr u32 kOcLandMaxLayers = 8;
struct OcLandData {
    ...existing...
    std::vector<std::string> layerNames;  // empty = never painted. [0] is the implicit base layer.
    std::vector<u8> weights;              // (layerNames.size()-1) * N*N, planar, layers 1..L-1, row-major like heights
    std::string warning;                  // set by parse when layers were dropped; never written
    bool hasLayers() const { return !layerNames.empty(); }
    bool layersValid() const;             // L<=8, names match [A-Za-z0-9_]{1,31} and are unique, weights.size()==(L-1)*N*N
    u8 weightAt(u32 layer, u32 ix, u32 iy) const;   // layer 0 = 255 - sum of the stored planes (clamped >= 0)
};
```

Chunks: `LLYR` = `u8 count (1..8)`, then per layer `u8 nameLen, bytes[nameLen]`. `LWGT` = `(count-1)*N*N` bytes, plane 1 first. Written only when `hasLayers()`; both flags 0, version 1. `writeOcLand` refuses (`why`) an invalid `layersValid()` or any sample whose stored sum exceeds 255. `parseOcLand`: missing chunks = unpainted; `LLYR` present but `LWGT` shorter than expected, a bad name, or `count > 8` = **fail soft**: heights load, layers dropped, `warning` filled (the log line is the caller's). Quantisation of heights is untouched (`LRNG` logic stays). Downgrade note: an older engine build re-saving the file drops the paint (it ignores and does not rewrite unknown chunks); stated in the user guide.

Append-only docs: `docs/formats/FORMAT_SPECS.md` new section "11e `.ocland` weight layers" (letter assigned at merge if taken).

### 2.2 Paint brush (`modules/landscape`, new `Paint.hpp/.cpp`)

```cpp
enum class PaintMode { Paint, Erase, Smooth };
struct PaintParams {
    f32 centerCm[2] = {0, 0}; f32 radiusCm = 500.0f; f32 strength = 1.0f;   // strength 0..1: share of the gap closed per full-amount tick
    f32 falloff = 1.0f; PaintMode mode = PaintMode::Paint; u32 layer = 0;   // layer: index into data.layerNames
    f32 target = 1.0f;                                                      // Paint stops at this weight (opacity cap)
};
BrushRect paintRect(const fmt::OcLandData&, const PaintParams&);            // == brushRect geometry (same clamp)
BrushRect applyPaint(fmt::OcLandData&, const PaintParams&, f32 amount = 1.0f);
bool ensureLayer(fmt::OcLandData&, std::string_view name, u32& outIndex);   // appends a zero plane; false at 8 layers or bad name
bool removeLayer(fmt::OcLandData&, u32 index);                              // index>=1; its weight folds into layer 0
void fillLayer(fmt::OcLandData&, u32 layer);                                // whole section -> that layer
BrushRect autoFillBySlope(fmt::OcLandData&, u32 layer, f32 minSlopeDeg, f32 maxSlopeDeg,
                          f32 minZCm, f32 maxZCm, f32 featherDeg, f32 amount);   // rect = whole section
bool layerWeightsAt(const fmt::OcLandData&, f32 worldX, f32 worldY, f32 out[8]); // bilinear, normalised to sum 1; false outside the footprint
u32  dominantLayer(const f32 w[8], u32 count);                              // arg max, ties -> lower index
void packWeightTextures(const fmt::OcLandData&, const i32 materialChannelOfLayer[8], u32 materialLayerCount,
                        std::vector<u8>& rgbaA, std::vector<u8>& rgbaB);    // N*N RGBA8 each; see 2.4
void buildMipChainRGBA8(u32 n, const std::vector<u8>& level0, std::vector<std::vector<u8>>& levels); // box filter to 1x1
f32  brushFalloff(f32 t01, f32 falloff);                                    // extracted from Sculpt.cpp's radial weight, declared in Sculpt.hpp
```

Per touched sample (radial weight `b = clamp(amount * strength * brushFalloff(d/r, falloff), 0, 1)`), with `w[i]` the float weights (`w[0] = 1 - sum`):
- **Paint**: `w[L] += (target - w[L]) * b` (no change if already above `target`); the other layers scale by `(1 - w[L]') / (1 - w[L])` (all others 0 when `w[L] == 1`).
- **Erase**: `w[L] *= (1 - b)`; the freed mass is added to the other layers in proportion to their weights, to layer 0 if they are all zero; erasing layer 0 does nothing.
- **Smooth**: `w[i] = lerp(w[i], mean of the 3x3 neighbourhood w[i], b)` for every layer.
- **Quantise** with largest remainder so the L values sum to exactly 255, then store layers 1..L-1. A test pins this (sum is 255 for every touched sample).

`applyPaint` returns the touched rect and changes nothing else; it never touches `heights`, bounds or the quadtree. `autoFillBySlope`'s slope comes from the height grid's central difference.

### 2.3 Layered terrain material (`modules/render.pbr`, `modules/formats` OcMat, `modules/assets`)

**`.ocmat` records** (new subsection in FORMAT_SPECS §7; unknown to old readers = skipped, material renders as a plain one):

```
TERRAIN tile 512 pad 8 blend 0.15          # presence = layered terrain material. tile 256|512|1024 texels, pad 4..16 (even), blend 0..1 height-blend depth
LAYER Grass tiling 400 heightbias 0 baseColor {path:Textures/T/grass_diff.png} normal {path:Textures/T/grass_nor.png} metalRough {path:Textures/T/grass_orm.png} height {path:Textures/T/grass_h.png} surface Grass friction 0.8 restitution 0.05 foliage Foliage/Grass.ocfoliage density 8 minweight 0.5
LAYER Rock tiling 600 ...
```
One `LAYER` per line, file order = layer index, first = the implicit base. `tiling` = world cm per texture tile; `height` optional (alpha from the albedo's luminance when absent); `surface` is a free name (default = layer name); `friction` default 0.6, `restitution` 0; `foliage` optional `.ocfoliage` path, `density` = instances per 100 m^2, `minweight` default 0.5. More than 8 layers, a duplicate name or a bad name = the whole `TERRAIN` block refused with a log line (material loads as plain). The usual PARAMs still apply (`uvTiling` = the mesh-UV tile size the landscape renderer bakes, `roughnessFactor` etc. multiply the blended result).

**`Material.hpp` additions** (delimited block `// ---- wave3 terrain layers ----`): `kMaxTerrainLayers = 8`, `struct TerrainLayerDesc { std::string name, surface, foliage; TextureRef baseColor, normal, metalRough, height; f32 tilingCm = 400, heightBias = 0, friction = 0.6f, restitution = 0, foliageDensity = 0, foliageMinWeight = 0.5f; }`, `struct TerrainDesc { u32 tileTexels = 512, padTexels = 8; f32 blendDepth = 0.15f; std::vector<TerrainLayerDesc> layers; bool enabled() const { return !layers.empty(); } }`, `MaterialDesc::terrain`. `OcMat.cpp` parses/writes it (round trip). `MaterialFlag_TerrainLayers = 1u << 17` in `MaterialGpu.hpp`, set by `packMaterial` when `terrain.enabled()`; it clears `MaterialFlag_SlopeBlend` (they do not combine). `AVER_MAT_TERRAIN (1u << 17)` in `material_prelude.hlsl`. The ray-table copy (`RtMaterial` is a typedef of `AverMaterialData`) needs nothing more.

**Slots under the flag** (a slot index is still its register, and the ray path's `texIndex[]` still addresses the same eight):

| Slot | Meaning under `AVER_MAT_TERRAIN` | Format |
|---|---|---|
| 0 BaseColor | albedo atlas, rgb sRGB, **a = layer height** (for the blend) | RGBA8 sRGB |
| 1 MetalRough | atlas, R = AO, G = roughness, B = metallic (glTF ORM, as `averSampleMaps` reads) | RGBA8 |
| 2 Normal | tangent-space normal atlas | RGBA8 |
| 3 Occlusion | weightmap A = layers 1..4 (rgba) | RGBA8 linear, full mip chain |
| 4 Emissive | weightmap B = layers 5..7 (rgb) | RGBA8 linear, full mip chain |
| 5 Layer1BaseColor | **parameter texture**, 8 x 9 | R32Float |
| 6, 7 | unused | fallback |

Atlas layout: `cols x rows` cells (8 layers = 4 x 2); a cell is `S = tile + 2*pad` texels square, the tile in its middle, the pad holding the **wrapped** neighbouring texels so bilinear sampling at a tile edge continues the tile. Mips are built **per cell** (downsample the padded cell, then assemble), which stays aligned while `S` is divisible by `2^m`; with the default 528 that is 16, and the shader stops sampling the atlas at `log2(pad) + 0.5` and blends to the layer's flat average beyond it, so the residual pad bleed never shows.

**Parameter texture** (`pbr::terrainParamTexels(const MaterialDesc&, const TerrainTextures&, const TerrainSection*, f32 uvTilesPerCm, f32 out[72])` in new `render.pbr/include/aver/pbr/TerrainLayers.hpp`; texel (x, y), R32Float):
```
y0: uvScale_i      layer tiles relative to the mesh UV = (1/tilingCm_i) / uvTilesPerCm
y1: heightBias_i
y2,y3,y4: average albedo r,g,b (linear) of layer i      y5: average roughness   y6: average metallic
y7: x0 cols, x1 rows, x2 stride S, x3 tile T, x4 pad, x5 layerCount, x6 blendDepth, x7 atlasWidth
y8: x0 sectionOriginX(cm), x1 sectionOriginY, x2 invExtent = 1/(N*spacing) (0 = no weightmap), x3 halfTexel = 0.5/N, x4 atlasHeight
```
`struct TerrainSection { f32 originXCm, originYCm, spacingCm; u32 n; }`; null = `invExtent 0`. R32Float rather than half because a section origin in cm needs the precision. The HLSL row indices are `#define AVER_TERRAIN_ROW_*`; a test reads the HLSL and compares them with the C++ constants (the `AbiEnumTest` approach).

**Evaluation** (`averTerrainMaps(AverVertex v, float2 uv, float2 gx, float2 gy)` in `material_prelude.hlsl`; raster `averStockAuthored` calls it first thing when the flag is set; `rtHitSurface` (`voxi_rt.hlsli:334`) calls the same body, fetching through `averRtSampleSlot`. ONE body, two fetch back-ends selected by a static material pointer exactly as `averRtSampleSlotGraph` does (`voxi_rt.hlsli:117`); a requirement, not a suggestion: no duplicated blend maths):
1. `uvW = (wpos.xy - origin) * invExtent + halfTexel`; weights are 0 (all base layer) when `invExtent <= 0` or `uvW` leaves [0,1]. Else `wA = slot3.SampleGrad(uvW, ...)`, `wB = slot4.SampleGrad(...)` (gradients = `ddx/ddy(uvW)`, or the ray differentials scaled the same way). `w = {saturate(1 - (wA.r+wA.g+wA.b+wA.a+wB.r+wB.g+wB.b)), wA.r, wA.g, wA.b, wA.a, wB.r, wB.g, wB.b}`, renormalised to sum 1.
2. Active layers: `w_i >= 0.004` (always keep the largest). For each, `uvI = uv * uvScale_i`; atlas texel = `cellOrigin_i + pad + frac(uvI) * T`; gradients `gx * uvScale_i * T / atlasSize`; `lod = log2(max(|g|*..., 1))`. **Pass 1** fetches albedo (rgb + height a) only.
3. **Height blend** (Mishkinis' formula): `a_i = w_i + h_i + heightBias_i`; `m = max a_i`; `b_i = max(a_i - m + blendDepth, 0)`; `b_i /= sum(b)`. `blendDepth` large = plain linear weights.
4. **Pass 2** fetches normal and ORM for layers with `b_i > 0.01` and combines: `albedo = sum b_i*albedo_i`, `rough/metal/AO` likewise, normals summed in tangent space and renormalised (`normalLen` = the summed length before renormalising). Beyond the atlas LOD limit a layer contributes `avgAlbedo/avgRough/avgMetal` from the parameter texture instead of a fetch.
5. `AVER_RT_HIT_LITE` hits (secondary GI bounces) evaluate only the **dominant** layer's albedo/ORM, no height blend. Opacity is 1.

No change to any non-terrain material: every addition is behind the flag, so existing gates and baselines must not move (a gate run proves it).

**Atlas builder** (`modules/assets`, new `TextureAtlas.hpp/.cpp`; pure CPU part testable without a device):
```cpp
struct AtlasTile { std::string colorPath, alphaPath; bool alphaFromLuma = false; f32 fill[4] = {0.5f, 0.5f, 0.5f, 0.5f}; };  // empty colorPath = flat fill
struct AtlasSpec { u32 cols = 4, rows = 2, tileTexels = 512, padTexels = 8; TextureUsage usage = TextureUsage::Colour; std::vector<AtlasTile> tiles; };
struct AtlasImage { u32 width = 0, height = 0; std::vector<std::vector<u8>> levels; f32 tileAverage[8][4] = {}; };  // RGBA8 per mip
bool buildAtlasImage(const AtlasSpec&, AtlasImage& out, std::string* err);
rhi::TextureHandle uploadAtlas(rhi::IResourceFactory&, const AtlasImage&, bool srgb, std::string* err, TextureUploadInfo* info = nullptr);
```
It reuses `TextureUpload.cpp`'s decoder, box resize and `usage`-aware mip filter (extract them into a `detail::` namespace in that file; do not re-implement). RGBA8, not BC7 (memory at 512 tiles: 2112 x 1056 x 4 B x 1.33 x 3 maps ~ 36 MB; 1024 tiles ~ 143 MB and logs a warning). Caching the atlas in the DDC is future work.

**`MaterialSystem` seams** (`MaterialSystem.hpp/.cpp`; minimal on purpose, wave 4 #6 "material instances" may subsume them):
```cpp
struct TerrainTextures { rhi::TextureHandle albedo = 0, normal = 0, orm = 0; f32 avg[8][5] = {}; u32 cols = 0, rows = 0, stride = 0, tile = 0, pad = 0, atlasW = 0, atlasH = 0; usize bytes = 0; };
using TerrainResolver = bool (*)(const MaterialDesc& d, TerrainTextures& out, void* user);
void setTerrainResolver(TerrainResolver fn, void* user);
// A TextureRef whose path starts "@rt:" resolves here before the host resolver. Not owned: never refcounted or destroyed by this system.
rhi::TextureHandle setRuntimeTexture(const std::string& name, rhi::TextureHandle h);   // returns the replaced handle (caller retires it AFTER the next update())
rhi::TextureHandle clearRuntimeTexture(const std::string& name);
rhi::TextureHandle runtimeTexture(const std::string& name) const;
```
`writeSlots`: for a terrain desc, slots 0-2 come from the resolver (cached per material name + hash of the layer refs; the atlas textures are owned and refcounted like other resolved textures), slots 3-5 from their `@rt:` refs, else fallbacks: slots 3,4 a 1x1 zero RGBA8 (all weight on the base layer), slot 5 a parameter texture MaterialSystem builds itself with `invExtent = 0`. So a terrain material is valid on any mesh with no section data. `GameContent::resolveMaterialTexture`'s sibling `GameContent::resolveTerrainAtlas` (static, `Runtime/src/GameContent.cpp`) is the host resolver: three `buildAtlasImage` + `uploadAtlas` calls (Colour/Data/NormalMap usage), installed beside `setTextureResolver` (`GameApp.cpp:742`, `SandboxApp.cpp:1123`).

**Per-section materials.** `GameLandscape` keeps, for the authored section, a *derived* `MaterialDesc` (copy of the base terrain desc, name `<base>#sec`, `textures[Occlusion].path = "@rt:land.w0"`, `[Emissive] = "@rt:land.w1"`, `[Layer1BaseColor] = "@rt:land.params"`), made with `MaterialLibrary::create`, and one shared `<base>#ring` with no `@rt:` refs for every ring tile. The renderers bind `bindingSet(h)` / `constants(h)` of those handles (`applySurface`, `GameLandscape.cpp:140`, picks by `RingTile` or section). The ray path finds them like any material (`gpuMaterialIndex`), no change. The derived descs are destroyed with the section.

**Weight textures.** `packWeightTextures` maps section layers to material layers *by name*: material channel of a section layer = the index of the same name in `terrain.layers` (a section layer the material lacks folds into the base layer; a material layer the section never painted is weight 0). `GameLandscape::flushWeights(device, materials)` (called at the top of `draw`, like the existing surface-binding retry at `:339`) rebuilds both RGBA8 textures with the mip chain and swaps them via `setRuntimeTexture` + `MaterialLibrary::touch`, **at most every 100 ms while a stroke is down and immediately at stroke end**; the replaced textures are destroyed after the next `MaterialSystem::update()` so no binding set points at a dead texture. A dirty-rectangle upload (`copyBufferToTexture`) is a later optimisation; whole-texture swap costs ~4 MB x2 at 1025 samples.

**D3D12 and Vulkan.** One HLSL source for both (the engine's DXC path); nothing here uses a feature outside the existing material bindings (Texture2D slots, R32Float via `Load`, `SampleGrad`). Parity is checked, not assumed: section 4's gate list runs on both backends.

### 2.4 Landscape paint integration (`Runtime/GameLandscape`)

New members/methods (additive, delimited `// ---- wave3 layers ----`):
```cpp
bool ensureLayer(const std::string& name, u32& outIndex);                  // also marks dirty
landscape::BrushRect paint(rhi::IDevice*, const landscape::PaintParams&, f32 amount);   // grows the weight stroke first, applies, marks weightsDirty_
void beginWeightStroke();  void growWeightStroke(const landscape::BrushRect&);
bool endWeightStroke(std::vector<u8>& before, std::vector<u8>& after, u32& x0, u32& y0, u32& x1, u32& y1, u32& planes);
bool applyWeightRect(u32 x0, u32 y0, u32 x1, u32 y1, u32 planes, const std::vector<u8>& src);   // undo/redo; planes = stored layers at stroke time
struct GroundInfo { f32 z, normal[3]; f32 weights[8]; u32 dominant; f32 friction, restitution; std::string surface; };
bool sampleGround(f32 x, f32 y, GroundInfo&) const;                         // CPU only: works headless
u32  materialLayerCount() const;  const std::string& materialLayerName(u32) const;
void flushWeights(rhi::IDevice*, pbr::MaterialSystem*);
```
`ensureLayer` happens when a layer is chosen in the panel, *before* a stroke, so a stroke's undo records planes for a fixed layer count (the layer table only grows; "remove unused layers" is an explicit button and clears the undo stack of weight entries). The weight stroke mirrors the height stroke: lazily captured before-planes over the unioned rect, one undo entry per drag, no entry for a no-op. `save()` already writes via `saveOcLand`, so paint persists with `Save Terrain`; it does **not** call `rebuildCollision` for weight-only dirt (heights unchanged). `dirty()` covers both. `loadForLevel` after `loadLandscape` resolves the material, builds the layer remap and section/ring materials; the first draw performs the initial weight upload. Without `AVER_MODULE_PBR` the layer table is empty and paint data is carried but not drawn.

### 2.5 Physical surfaces

Data lives on the material layers (`surface`, `friction`, `restitution`), so one definition serves the renderer, the query and physics.
- **Query**: `GameLandscape::sampleGround` (above): friction/restitution are the weight-averaged layer values, `surface` the dominant layer's. Outside the section, or with no terrain material: `Found = false`, defaults.
- **Framework ABI** `framework_terrain_abi.h` (2.9) relays it to C# and graphs. A *surface id* is the 32-bit FNV-1a of the surface name (0 never; a zero hash becomes 1), so graphs compare ints without a registry (`Surface.IdOf(string)`).
- **Rigid-body friction**: new physics ABI (`physics_water_abi.h`, 2.8): `aver_phys_set_surface_sampler(fn, user)` and `aver_phys_body_set_surface_sampled(body, on)`. `GameLandscape` flags the heightfield body (`landscapeBody_`) after `rebuildCollision`. In the contact listener, when either body is flagged, the *other* body's friction is combined with the sampled layer friction at the contact point (`mCombinedFriction = sqrt(fBody * fLayer)`, Jolt's default rule), restitution likewise; implemented in `OnContactAdded` and a new `OnContactPersisted` (both receive `ContactSettings&`; today it is unnamed and ignored, `PhysicsWorld.cpp:83`). **The sampler runs on physics worker threads**: it must be a pure read of the immutable weight array, and weights are not edited while a Play session or Simulate mode runs (the paint tool is disabled then; stated in the panel).
- **Characters** (`CharacterVirtual`) are not affected by the friction sampler; footsteps and slip are gameplay reads of `Landscape.Sample`.
- Non-landscape hits have no surface; `Landscape.Sample` says so. A general physical-material system for meshes is out of scope (open question 5).

### 2.6 Layer foliage (simple on purpose)

An editor command, not a runtime system: Landscape panel > Paint > **Scatter foliage from layer**. For the selected layer's `foliage` type (`OcFoliageData`), walk the section on a jittered grid whose cell area is `1e6 / density` cm^2; a candidate at (x, y) is kept when the layer's weight there is >= `minweight`; the position and rotation come from the existing machinery (`surfaceHeightAt`, `foliagePlacementRotation`, `FoliageAlign.hpp:122`), through a new overload `SandboxApp::foliagePlaceAt(Engine&, const FoliageSpecies&, f32 x, f32 y, u32 seed)` factored out of `foliagePlaceOne` (`SandboxViewport.cpp:542`; behaviour of the brush unchanged). The jitter is a pure hash of `(layer, cell)`, so re-running gives the same result. Everything lands in one `FoliageStroke` undo entry (`SandboxSelection.cpp:1208`). A second run adds more (it does not replace the first); removal is the existing erase brush or undo. Streaming/`ChunkGenerator` is untouched (a live per-layer scatter density would need a `GeneratorSettings` hook in `modules/world`, owned by the level-streaming feature; open question 6).

### 2.7 Water bodies

**Records** (`OcWorld.hpp/.cpp`, one delimited block `// ---- wave3 water bodies ----` after `OcGerstnerWave`, parse after `WAVE` at `OcWorld.cpp:541`, write after the `WAVE` emitter at `:1108`; FORMAT_SPECS §11 gets the grammar):
```
RIVER name Thames material M_River depth 200 bank 300 slow 0.35 flowscale 1.0
RIVERPT river Thames at 1000 -2000 150 width 900 flow 180        # one line per control point, file order = downstream
LAKE name Mere level 150 material M_Lake depth 300 bank 400 edge 2500
LAKEPT lake Mere at 4000 4000                                    # >= 3 points, implicitly closed, any winding
```
```cpp
struct OcRiverPoint { f64 x = 0, y = 0, z = 0, widthCm = 800, flowCmS = 150; };
struct OcRiver { std::string name, material; f64 depthCm = 200, bankCm = 300, bankSlow = 0.35, flowScale = 1.0; std::vector<OcRiverPoint> points; };
struct OcLakePoint { f64 x = 0, y = 0; };
struct OcLake { std::string name, material; f64 levelCm = 0, depthCm = 300, bankCm = 400, maxEdgeCm = 2500; std::vector<OcLakePoint> points; };
// OcWorldData: std::vector<OcRiver> rivers; std::vector<OcLake> lakes;
```
Parse rules like `WAVE`: a `*PT` line names its body; an unknown name is skipped; unnamed bodies get "unnamed" on write; missing optional tokens keep the defaults (and are omitted on write when default, so an untouched record round-trips byte-identically, the `WATER` rule). `levelHeader_` carries them through `saveLevel` automatically (it copies the header wholesale, `SandboxLevelEdit.cpp:74-102`); a test proves a level with a `RIVER` survives a load/save with no editor edit.

**CPU module** (`modules/fluids`, new `WaterBody.hpp/.cpp`, Core + the RHI vertex type only, no device):
```cpp
struct SplinePoint { f32 x = 0, y = 0, z = 0, width = 0, flow = 0; };
class Spline {   // XY: centripetal Catmull-Rom (alpha 0.5). z, width, flow: monotone cubic (PCHIP) over the same parameter, so they never overshoot
public:
    void set(std::vector<SplinePoint> pts, bool closed);
    struct Sample { f32 pos[3]; f32 tangent[2]; f32 width, flow, s; };
    f32 length() const;  Sample at(f32 s) const;                       // arc length
    void sample(f32 stepCm, std::vector<Sample>& out) const;           // >= 4 steps per CR segment, <= 4096 total (step grows to fit)
    bool closest(f32 x, f32 y, f32& outS, f32& outDist) const;
};
struct RiverDesc { std::string name, material; Spline spline; f32 depthCm = 200, bankCm = 300, bankSlow = 0.35f, flowScale = 1; };
struct LakeDesc  { std::string name, material; Spline outline; f32 levelCm = 0, depthCm = 300, bankCm = 400, maxEdgeCm = 2500; };
void buildRiverMesh(const RiverDesc&, f32 stepCm, std::vector<rhi::MeshVertex>& v, std::vector<u32>& idx);
void buildLakeMesh(const LakeDesc&, std::vector<rhi::MeshVertex>& v, std::vector<u32>& idx);
enum class WaterKind : u8 { None = 0, Ocean = 1, Lake = 2, River = 3 };
struct WaterQuery { bool over = false; f32 surfaceZ = 0, normal[3] = {0, 0, 1}, current[3] = {}, depthCm = 0; WaterKind kind = WaterKind::None; u32 body = 0; };
class WaterBodySet {
public:
    void clear();  u32 addRiver(RiverDesc);  u32 addLake(LakeDesc);  void finalize();   // builds a 1000 cm uniform grid over segments/triangles
    bool sample(f32 x, f32 y, WaterQuery& out) const;     // highest surface wins where bodies overlap
    std::function<bool(f32, f32, f32&)> ground;           // optional ground height, for depthCm = surfaceZ - ground
};
```
- **River mesh**: rows every <= 250 cm (curvature subdivision as in `sample`), 3 vertices per row (left, centre, right) at `pos ± leftNormal * width/2`, `z` from the spline; normal = `normalize(-T.xy * dz/ds, 1)`; `uv0 = (T.x, T.y) * flow * flowScale * profile`, `profile = 1` at the centre and `bankSlow` at the banks (banks slower, which also reads as shear). Winding front-facing from +Z (the `(p,q),(p,q+1),(p+1,q)` rule `buildWaterGrid` documents). Fewer than 2 points = nothing, logged once.
- **Lake mesh**: outline sampled every 300 cm, normalised to counter-clockwise, **ear-clipped**, then triangles are 1:4 midpoint-subdivided (shared edge midpoints cached, so the mesh stays watertight) until every edge <= `maxEdgeCm` (the AMD ray-test strips from 10 km triangles, `aver-water-material`, are why). Flat at `levelCm`, normal +Z, `uv0 = (0, 0)`.
- **Query**: lake = point-in-polygon (crossing number) at `levelCm`; river = closest point within `width(s)/2`, surface `z(s)`, current = `T.xy * flow(s) * flowScale * profile(lateral)` (z 0). `depthCm` = `surfaceZ - ground(x,y)` when `ground` is set, else the body's authored depth.
- No vertex waves on these meshes (risk 2); the ripple is the material's.

**Carving** (`modules/landscape`, new `Carve.hpp/.cpp`, `Aver.Formats` only; takes sampled points so it needs no spline type):
```cpp
struct CarvePoint { f32 x, y, surfaceZ, halfWidth; };
struct CarveParams { f32 depthCm = 200, bankCm = 300, bankSlopeDeg = 35, smooth = 0.5f; };
BrushRect carveRect(const fmt::OcLandData&, const CarvePoint* pts, u32 n, const CarveParams&);
BrushRect carveRiver(fmt::OcLandData&, const CarvePoint* pts, u32 n, const CarveParams&);
BrushRect carveBasin(fmt::OcLandData&, const f32* polyXY, u32 count, f32 levelCm, const CarveParams&);
```
River: for every sample within `halfWidth + bankCm` of the polyline, `bed = surfaceZ - depth * (1 - (lateral/halfWidth)^2)` inside the channel, `bed = surfaceZ + (lateral - halfWidth) * tan(bankSlope)` outside; `h' = min(h, bed)`, then `smooth` of one 3x3 relaxation over the rect. Basin: inside the polygon `bed = level - depth * smoothstep(0, bankCm, distInside)`, outside `bed = level + distOutside * tan(slope)`; `h' = min(h, bed)`. Only ever lowers. The wrapper `GameLandscape::carve(...)` does `carveRect` -> `growStroke` -> carve -> `collisionStale_ = true` and the usual tree rebuild/invalidate (exactly `sculpt`'s tail), and the editor pushes one `LandscapeStroke` entry (`endSculptStroke` path). Quantisation: carving only lowers, so `LRNG` may grow downward with the existing headroom rule.

**Runtime** (`GameWater`, additive, delimited): `applyLevel` is restructured so it never returns before the new calls: `applyBodies(device, w)` runs first (rebuilds only when a hash of the river/lake records and the ground source changed; the editor re-applies on every Details edit); the existing ocean path is unchanged. State: `WaterBodySet bodies_`, `std::vector<BodyDraw> draws_` (`{MeshHandle, material name, lake?}`), `rhi::IDevice* dev_` (set in `init`, so `unload()` can destroy meshes without a device argument), `std::function<bool(f64,f64,f64&)> ground_` via `setGroundHeightSource` (one line in each host's init, beside `water_.init`: `GameApp.cpp:1751`, `SandboxApp.cpp:827`, lambda over `landscape_.groundHeightAt`). `draw()` gains `drawBodies`: per body `bindSurfaceLook(...)` (`GameWater.cpp:42`) then `drawMesh(mesh, identity, look...)`, blended exactly as `drawDisplaced`. A body whose material is empty uses the fallback look. `unload()` destroys the meshes. `waterLevelCm()`/`attached()` also count bodies (underwater fog stays ocean-only, open question 7). The single-ocean limit (`:105+`) is unchanged.

**Material graph nodes** (`MaterialGraphHlsl.cpp`, palette `GraphNodeDefs.hpp:998+`, `shared_prelude.hlsl`):
- `VertexFlow`: outputs `velocity` (float2, cm/s) = `v.uv`, and `speed` (float) = its length. Raw mesh uv0, not `averSurfaceUV` (a `worlduv` material would otherwise return a projection).
- `FlowNormal`: inputs `position` (float2, world XY; default wired by authors from `WorldPosition`), `flow` (float2, default = `VertexFlow.velocity`), `cycle` (float, default 1.5 s); output `value` (float3 tangent-space normal). Two-phase flow-map advection of the *global* wave field: `t = gTime.x / cycle; p0 = frac(t); p1 = frac(t + 0.5); w0 = 1 - abs(2*p0 - 1)`; `n = normalize(lerp(Nlive, normalize(w0*N(p - flow*p0*cycle) + (1-w0)*N(p - flow*p1*cycle)), saturate(length(flow) / 20)))`, where `N(p)` is the wave normal with its time term frozen. This needs `averWaveNormalT(float2 p, float t)` in `shared_prelude.hlsl` (`averWaveNormal(p) = averWaveNormalT(p, gTime.x)`; same for height/focus) so the frozen sampler shares the single wave definition the caustics use. With `flow = 0` (lakes) the result is exactly `averWaveNormal(p)`.
- A river material is `G_WaterRipple.ocgraph` with `FlowNormal` in place of `WaveNormal` (same `.ocmat` volume parameters as `M_SeaWater`). The engine ships no content: the implementer authors `G_RiverFlow.ocgraph`, `M_River.ocmat` and a 4-layer `M_Terrain.ocmat` in the PTTest project (`Aver Projects/PTTest/Content/Materials`, where `M_SeaWater` lives) for the headed checks in section 4; open question 3 asks whether a template should carry them.
- Per-point flow *speed* variation is physical (buoyancy, nodes) and visible in the normal scroll because the flow vector is per vertex; the two-phase blend is what keeps varying speed from shearing the pattern over time.

### 2.8 Buoyancy from the real wave and flow

**Wave maths** (`GerstnerWave.hpp`, header-only, tested in `GerstnerWaveTest`):
```cpp
// Surface at a WORLD (x, z): solves rest = world - horizontal displacement by fixed-point iteration (3 steps; converges because the clamped steepness sums to <= 1),
// returns height and the unit normal at the solved rest point. Zero waves -> height 0, normal (0,0,1).
void gerstnerSurfaceCm(const GerstnerWave* waves, size_t count, f32 worldXCm, f32 worldZCm, f32 tSeconds, f32& outHeightCm, f32 outNormal[3]);
```
Axis note as in the header: second horizontal axis = engine Y.

**Clock.** `WaterSurface` and `WaterRenderer` expose `f32 elapsedSeconds() const` (they already own `elapsedSeconds_`, `WaterSurface.cpp:259`); `GameWater` samples with the clock of whichever surface is live, so CPU and GPU agree. `setFrozen(true)` holds both. The compute pass fades waves out near the camera-following grid's edge (`water_displace.hlsl` `fade`); the CPU does not, an accepted difference at the far rim.

**Physics ABI** (new `modules/physics/include/aver/physics/physics_water_abi.h`, implemented in `BuoyancyAbi.cpp` and `Buoyancy.cpp`; the physics ABI has no version macro today, so none is added; one is only added if the module grows one):
```c
typedef struct AverPhysWaterSample {
    float surfaceZCm;      /* world Z of the surface at (x, y) */
    float normal[3];       /* unit, engine axes */
    float currentCmS[3];   /* water velocity, cm/s */
    float buoyancy;        /* as aver_phys_set_water_plane; <= 0 = none here */
    float linearDrag, angularDrag;
} AverPhysWaterSample;
/* 1 = water here (use `out`); 0 = no water here (the body gets no buoyancy); -1 = no opinion (fall back to the per-body override or the plane). */
typedef int32_t (*aver_phys_water_sampler_fn)(float xCm, float yCm, float zCm, AverPhysWaterSample* out, void* user);
AVER_PHYS_API int32_t aver_phys_set_water_sampler(aver_phys_water_sampler_fn fn, void* user);   /* replaces; returns 1 */
AVER_PHYS_API void    aver_phys_clear_water_sampler(void);
AVER_PHYS_API int32_t aver_phys_has_water_sampler(void);

typedef int32_t (*aver_phys_surface_sampler_fn)(float xCm, float yCm, float* outFriction, float* outRestitution, void* user);  /* 1 = valid */
AVER_PHYS_API int32_t aver_phys_set_surface_sampler(aver_phys_surface_sampler_fn fn, void* user);
AVER_PHYS_API void    aver_phys_clear_surface_sampler(void);
AVER_PHYS_API int32_t aver_phys_body_set_surface_sampled(int32_t body, int32_t on);   /* 0 for a dead handle */
```
`WaterVolumeTable::evaluate` (`Buoyancy.cpp`), per fixed substep, for each rigid dynamic body without a per-body override: if a sampler is set, take the centre of mass in engine coordinates (`fromJolt`), sample; for a body whose horizontal AABB exceeds 300 cm sample the centre plus the four AABB corners and average height, current and (renormalised) normal; `1` -> `ApplyBuoyancyImpulse` with `surfacePosition = toJolt(x, y, surfaceZ)`, `toJoltUnit(normal)`, `toJoltDir(current)`; `0` -> skip; `-1` -> the existing plane path. Static and soft bodies are skipped before sampling (`buoyant()` already filters; do it first). `aver_phys_buoyant_body_count` keeps counting. `aver_phys_shutdown` clears both samplers (it already clears planes/overrides, `PhysicsWorld.cpp:312`).

**Host sampler** (`GameWater::physicsWater`, static, installed in `GameWater::init` beside the fluid-spawn relay and again when `applyLevel` changes what exists): lake/river from `bodies_.sample`; else the ocean record if present (not `simulate`, inside `bounds` when bounded) at `level + gerstnerSurfaceCm(...)`, normal from the same call, current 0. Defaults: buoyancy 1, linear drag 0.5 (rivers 0.8), angular 0.01. Overlap: the highest surface at (x, y) wins. It never returns -1, so a level with only analytic bounded water stops buoying bodies outside its bounds.

### 2.9 Framework ABI, C# API, graph surface

**`modules/framework/include/aver/framework/framework_terrain_abi.h`** (own version, `framework_state_abi.h` style; `modules/framework/src/TerrainAbi.cpp` added to `add_library(Aver.Framework ...)`; `framework_abi.h` untouched):
```c
#define AVER_FW_TERRAIN_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_terrain_abi_version(void);
#define AVER_FW_TERRAIN_MAX_LAYERS 8
#define AVER_FW_WATER_KIND_NONE  0
#define AVER_FW_WATER_KIND_OCEAN 1
#define AVER_FW_WATER_KIND_LAKE  2
#define AVER_FW_WATER_KIND_RIVER 3
/* 1 = inside the section. outWeights has AVER_FW_TERRAIN_MAX_LAYERS floats (material layer order, sum 1). Any pointer may be null. */
typedef int32_t (AVER_FW_CALL* aver_fw_terrain_sample_fn)(float x, float y, float* outZ, float* outNormal3, int32_t* outDominant, float* outWeights, void* user);
typedef int32_t (AVER_FW_CALL* aver_fw_terrain_layer_count_fn)(void* user);
/* Copies NUL-terminated UTF-8 into the buffers (truncated); 1 if the layer exists. */
typedef int32_t (AVER_FW_CALL* aver_fw_terrain_layer_fn)(int32_t layer, char* nameBuf, int32_t nameCap, char* surfaceBuf, int32_t surfaceCap, float* outFriction, float* outRestitution, void* user);
AVER_FW_ABI int32_t aver_fw_set_terrain_provider(aver_fw_terrain_sample_fn s, aver_fw_terrain_layer_count_fn c, aver_fw_terrain_layer_fn l, void* user); /* always 1 */
AVER_FW_ABI int32_t aver_fw_terrain_sample(float x, float y, float* outZ, float* outNormal3, int32_t* outDominant, float* outWeights);  /* 0 with no provider */
AVER_FW_ABI int32_t aver_fw_terrain_layer_count(void);                                                                                  /* 0 with no provider */
AVER_FW_ABI int32_t aver_fw_terrain_layer(int32_t layer, char* nameBuf, int32_t nameCap, char* surfaceBuf, int32_t surfaceCap, float* outFriction, float* outRestitution);
/* outDepth = surface minus ground (cm); outCurrent3 cm/s. 1 = water covers (x, y). */
typedef int32_t (AVER_FW_CALL* aver_fw_water_sample_fn)(float x, float y, float z, float* outSurfaceZ, float* outNormal3, float* outCurrent3, float* outDepth, int32_t* outKind, void* user);
AVER_FW_ABI int32_t aver_fw_set_water_provider(aver_fw_water_sample_fn fn, void* user);                                                  /* always 1 */
AVER_FW_ABI int32_t aver_fw_water_sample(float x, float y, float z, float* outSurfaceZ, float* outNormal3, float* outCurrent3, float* outDepth, int32_t* outKind);  /* 0 with no provider */
```
Providers: `GameLandscape::installProviders()` (new; one call per host beside `water_.init`) and `GameWater::init` (precedent for the guard: `#if AVER_MODULE_FRAMEWORK`). Docs: `docs/ABI.md` subsection "Terrain and water queries".

**C#** (`scripting/csharp/Aver.Framework/`; public XML docs on everything, `GenerateDocumentationFile` is on):
```csharp
// TerrainNative.cs   internal static class FwTerrain { [DllImport("Aver.Framework")] ...all 7 entry points... } + [UnmanagedFunctionPointer(Cdecl)] delegates only if a managed provider is ever needed (none: the providers are native)
// Landscape.cs
public enum WaterKind { None = 0, Ocean = 1, Lake = 2, River = 3 }
public readonly record struct GroundSample(bool Found, float Z, Vec3 Normal, int DominantLayer, string Surface, int SurfaceId, float Friction, float Restitution);
public static class Landscape {
    public static int LayerCount { get; }
    public static string LayerName(int layer);  public static int LayerIndex(string name);       // -1 if unknown
    public static GroundSample Sample(float xCm, float yCm);  public static GroundSample Sample(Vec3 p);
    public static float LayerWeight(float xCm, float yCm, int layer);  public static float LayerWeight(float xCm, float yCm, string layer);
    public static bool Weights(float xCm, float yCm, Span<float> dst);                         // dst.Length >= 8 (writes LayerCount)
}
public static class Surface { public static int IdOf(string name); }                          // FNV-1a 32 of UTF-8, never 0
// Water.cs
public readonly record struct WaterSample(bool Over, bool Submerged, float SurfaceZ, float DepthCm, Vec3 Current, WaterKind Kind);
public static class Water {
    public static WaterSample Sample(Vec3 p);  public static WaterSample Sample(Entity e);      // e.WorldPosition
    public static bool IsInWater(Vec3 p);  public static Vec3 CurrentAt(Vec3 p);
}
```
Strings cross the ABI in fixed buffers (256 B name, 64 B surface); no allocation beyond the returned strings. All calls are main-thread, never throw (native failure = `Found/Over = false`). Enum parity with C is pinned by `AbiEnumTest` (add a `kGroups` row `AVER_FW_WATER_KIND_` <-> `WaterKind`, and the signature table row copied from the `framework_state_abi.h`/`EntityStateNative.cs` row).

## 3. Graph nodes

Implemented from these rows by the graph-node agent; owner class `TerrainGraph` (`internal static`, `scripting/csharp/Aver.Framework/TerrainGraph.cs`, written FIRST so the rows have real methods; `GameSystemNodes.Validate()` checks them). Rows in new `scripting/csharp/Aver.Graph/GraphTerrainNodeTable.cs` (`private static GenericNodeSpec[] TerrainTable()` in `partial class GameSystemNodes`); `Table()` gains `.Concat(TerrainTable())` after `.Concat(GasTable())` (`GraphGameSystemNodeTable.cs:120`). Palette mirror `sandbox/src/GraphNodeDefsTerrain.hpp`, pin for pin. Strings are node attributes (graph pins are float/int/bool, `PinType`); an `int` id pin overrides the attribute where both exist, as in GAS.

| Type | Kind | `TerrainGraph` method | Inputs | Outputs | Attribute |
|---|---|---|---|---|---|
| `AN_LandscapeSample` | Pure | `bool SampleForGraph(float x, float y, out float z, out float nx, out float ny, out float nz, out int layer)` | x, y (cm) | `found:bool, z, nx, ny, nz, layer:int` | none |
| `AN_LandscapeLayerId` | Pure | `int LayerIdForGraph(string layer)` | none | `layerId:int` (-1 unknown) | `layer` |
| `AN_LandscapeLayerWeight` | Pure | `float LayerWeightForGraph(float x, float y, int layerId, string layer)` | x, y, layerId | `weight:float` | `layer?` |
| `AN_LandscapeSurface` | Pure | `int SurfaceForGraph(float x, float y, out float friction, out float restitution, out int layer)` | x, y | `surfaceId:int, friction, restitution, layer:int` (0 = none) | none |
| `AN_SurfaceId` | Pure | `int SurfaceIdForGraph(string surface)` | none | `surfaceId:int` | `surface` |
| `AN_WaterSample` | Pure | `bool WaterSampleForGraph(float x, float y, float z, out bool submerged, out float surfaceZ, out float depth, out float flowX, out float flowY, out float flowZ, out int kind)` | x, y, z | `over:bool, submerged:bool, surfaceZ, depth, flowX, flowY, flowZ, kind:int` (0 none, 1 ocean, 2 lake, 3 river) | none |
| `AN_EntityWater` | Pure | `bool EntityWaterForGraph(int entity, float heightCm, out float submersion, out float flowX, out float flowY, out float flowZ, out int kind)` | entity, heightCm (default 180) | `over:bool, submersion:float (0..1 of heightCm), flowX, flowY, flowZ, kind:int` | none |

Rows follow the existing grammar, for example `Reg("an_landscapesample", GenericKind.Pure, typeof(TerrainGraph), "SampleForGraph", "x:float,y:float", "found:bool,z:float,nx:float,ny:float,nz:float,layer:int", "p:x,p:y,>z,>nx,>ny,>nz,>layer", "found")` and `Reg("an_landscapelayerweight", GenericKind.Pure, typeof(TerrainGraph), "LayerWeightForGraph", "x:float,y:float,layerId:int=-1", "weight:float", "p:x,p:y,p:layerId,@layer?", "weight")`. Failure never throws; a missing provider or a point off the section returns the defaults (found false, weight 0, surfaceId 0).

**Parity table.** `Landscape.Sample/LayerWeight/LayerIndex/Weights` and `Surface.IdOf` -> `AN_LandscapeSample`, `AN_LandscapeLayerWeight`, `AN_LandscapeLayerId`, `AN_SurfaceId`/`AN_LandscapeSurface` (`Weights` span form is C#-only; the node reads one layer at a time). `Water.Sample(Vec3)` -> `AN_WaterSample`; `Water.Sample(Entity)` -> `AN_EntityWater`; `IsInWater`/`CurrentAt` are their projections (use the pins). Material graph: `VertexFlow` and `FlowNormal` (2.7). Authoring-time APIs (paint, carve, spline edit) are editor commands, not runtime APIs, and have no nodes by design. Events (`entered water`) are not provided: a graph polls `AN_EntityWater` from a timer, or uses a physics sensor; listed in section 6.

### 2.10 Editor surface (extends the existing Landscape mode panel; no new window)

`buildLandscapeModePanel` (`SandboxPanels.cpp:667`) keeps its create-a-landscape branch and PLACEMENT block and wraps the SCULPT/BRUSH/GENERATE content in an `ImGui::BeginTabBar` with three tabs **Sculpt | Paint | Water** (the sculpt content moves unchanged into the first). `EditorMode` is not extended; `editorModeIsLandscape()` stays the gate, and a new `landscapeTab_` enum selects the viewport handler (`handleSculpt` is called from `SandboxViewport.cpp:1482`; add `handlePaint` and `handleWaterSpline` beside it, same guard and the same `viewportRay` + `raycastHeightfield` hit).

**Paint tab.** Material row: the level's `LANDSCAPE` material name (shown from `levelHeader_.landscapes.front().material`) with a picker over the project's `.ocmat` files that carry a `TERRAIN` block; choosing one writes the record (`recordLandscapeInLevel`, `SandboxLevelEdit.cpp:80`, extended to keep the choice) and re-binds. Layer list from `terrain.layers` (swatch = layer average colour, name, surface, friction); click selects (and `ensureLayer`s). Tools: Paint, Erase, Smooth (buttons like the sculpt tools), Fill Layer, Auto-fill by slope/height (parameters in a popup), Remove Unused Layers, Scatter Foliage From Layer. Brush: radius, strength, falloff, opacity (= `target`); the cursor ring is `drawSculptCursor` (`SandboxViewport.cpp:1876`) reused for both. Disabled with a one-line reason while Simulate/Play runs or when the level has no terrain material. Hotkeys: none new (the sculpt keys 1-4 stay on the Sculpt tab).

**Undo.** `EditCmd::Kind` gains `LandscapeWeights` and `WaterBodies` (delimited additions at `SandboxApp.hpp:1987` and in `undo()/redo()`, `SandboxSelection.cpp:1274/1327`). `LandscapeWeights`: rect + `landPlanes` + `std::vector<u8> landWBefore, landWAfter` (a sibling of `landBefore/landAfter`, `SandboxApp.hpp:2041`), applied by `GameLandscape::applyWeightRect`. `WaterBodies`: `std::vector<fmt::OcRiver> riversBefore, riversAfter; std::vector<fmt::OcLake> lakesBefore, lakesAfter;` (small), restoring into `levelHeader_` then `water_.rebuildBodies`. One entry per drag/gesture, no entry for a no-op, exactly like the height stroke; `markLevelUnsaved()` for water edits (they are in the level file), `landscape_.dirty()` for paint (they are in `.ocland`).

**Water tab.** Lists rivers and lakes (select, rename, delete), New River / New Lake, and for the selected body: material picker (any `.ocmat`), depth, bank, slow, flow scale; per selected point: width, flow speed, z. Viewport tool (only while the tab is open): LMB on terrain appends a point (lake: before closing) with z snapped to the terrain height (rivers: also not above the previous point's z unless Shift); LMB-drag a point handle moves it on the terrain (Shift = vertical, uses the existing viewport drag plane code); Ctrl+LMB on a segment inserts; Alt+LMB or Del removes; handles and the centreline/width edges are drawn with `createLineMesh`/`drawLines` (`RHI.hpp:658-668`), rebuilt on edit and destroyed with the body (`drawSculptCursor`'s depth-off line state). Picking is screen-space distance to projected handles (< 10 px) using the existing viewport matrices. Buttons: **Carve terrain** (river: samples the spline every 250 cm into `CarvePoint`s; lake: the sampled outline), **Snap points to terrain**, **Reverse direction**, **Fit lake level to lowest point**. Every edit calls `water_.rebuildBodies(*device, levelHeader_)` (throttled to once per frame during a drag).

**Material panel.** `materialPanel(handle)` (`SandboxPanels.cpp:213`) gets a "Terrain layers" section when `terrain.enabled()` or via a "Make terrain material" button: tile/pad/blend, a per-layer collapsible editor (texture refs through the existing slot picker used by the texture slots, tiling, height bias, surface, friction, restitution, foliage), add/remove/reorder (reordering is safe: paint is stored by name). It writes through the same Material undo entry the panel already pushes. The `.ocmat` is saved by the panel's existing save path (`writeOcmat`).

### 2.11 Runtime, packaged game, save/load

- **Both hosts** run the same `GameLandscape`/`GameWater`; the only host edits are the two init lines (providers, ground source) and `setTerrainResolver` beside `setTextureResolver`. The packaged game ships `Content` (including `.ocland` with paint and `.ocmat` with `TERRAIN`) unchanged; `stage-game.ps1` needs nothing.
- **Headless/unscripted**: `sampleGround`, `WaterBodySet`, paint and carve are CPU and device-free; with no device `flushWeights`/`drawBodies` are no-ops.
- **Save/load**: paint persists in `.ocland` (`Save Terrain`); water bodies persist in the `.ocworld` through `levelHeader_`; there is nothing in save games (`aver::save`) because neither is mutable at run time. Level instances (feature #12) that embed an `.ocworld` must apply the instance transform to `RIVERPT`/`LAKEPT` and the landscape `at`; flagged to that owner in section 5.
- **Streaming**: `GameLandscape` still holds one authored section plus a ring (`GameLandscape.cpp:180` warns for more); paint per section works unchanged when multi-section lands, because weights are per `OcLandData`.
- **Performance budget**: terrain shader cost scales with active layers (typically 2-3, worst 8); atlas memory per section group is shared across sections of one material.

## 4. Tests

Names are exact; executables are discovered as `*Test.exe` in `<build>/bin` (no CTest in this repo, `AbiEnumTest.cpp` header), so registration = `add_executable` + `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`. Every test runs without a GPU unless stated.

| Test (file, target) | Asserts |
|---|---|
| `OcLandLayersTest` (`tests/formats/src/`, registered in `tests/formats/CMakeLists.txt` beside `LandscapeTest`) | Write/parse round trip of names and planes; a legacy file loads with `hasLayers() == false` and re-saves with identical heights/quantisation (the ITEM 0.7 invariants); `LLYR` present with short `LWGT`, 9 layers, a bad name: heights load, `warning` set, no layers; writer refuses sum > 255 and duplicate names; `weightAt(0, ...) == 255 - sum`. |
| `OcMatTerrainTest` (`tests/formats/src/`) | `TERRAIN`+`LAYER` parse and byte-stable round trip; defaults; 9 layers / duplicate name refuses the block and the material still loads plain; unknown tokens skipped; `packMaterial` sets bit 17 only with layers and clears the slope flag; `sizeof(MaterialConstants) == 192` unchanged. |
| `TerrainLayoutTest` (`tests/render.pbr/src/`, new target in `tests/render.pbr/CMakeLists.txt`) | Reads `material_prelude.hlsl` and compares every `AVER_TERRAIN_ROW_*`/`AVER_MAT_TERRAIN` define with `TerrainLayers.hpp`; `terrainParamTexels` against hand-computed numbers for a 3-layer desc (scales, cell geometry, `invExtent == 0` for a null section). |
| `TextureAtlasTest` (`tests/formats/src/`, links `Aver.Assets`) | `buildAtlasImage` from generated PNGs: cell origin of layer i, pad texels equal the wrapped opposite edge, per-level cell alignment through mip 3, alpha-from-luma, flat fill for a missing tile, average equals mean of the tile. |
| `LayerPaintTest` (`tests/landscape/src/`, `tests/landscape/CMakeLists.txt`) | After every `applyPaint` mode the per-sample sum is exactly 255; Paint converges to `target`, never exceeds it, leaves untouched samples bit-identical; Erase conserves mass; Smooth is a mean-preserving blur; falloff 0 vs 1 shapes; rect equals `paintRect`; `ensureLayer` caps at 8; `removeLayer` folds into layer 0; `layerWeightsAt` bilinear value at sample centres and between, false outside; `autoFillBySlope` on a ramp selects by slope band; `packWeightTextures` name remap (material has the section's layers in another order, one missing, one extra). |
| `CarveTest` (`tests/landscape/src/`) | Carve only lowers; centre bed depth equals `depth`; width profile parabolic; bank slope meets original terrain; `carveRect` contains every changed sample; `carveBasin` for a concave polygon; a no-op carve (terrain already lower) changes nothing; undo-style before/after restore is exact. |
| `WaterBodyTest` (`tests/fluids/src/`, `tests/fluids/CMakeLists.txt`) | PCHIP never overshoots monotone z/width/flow; arc-length `at(length)` is the last point; closed spline wraps; river mesh: vertex/index counts, all triangle normals +Z, centre-row `uv0` parallel to the tangent with magnitude `flow*flowScale`, bank rows scaled by `bankSlow`; lake mesh: area equals the shoelace area (1e-3 relative), every interior edge shared by exactly two triangles (watertight), max edge <= `maxEdgeCm`, concave and clockwise inputs; `WaterBodySet::sample`: inside/outside/edge, highest surface wins on overlap, river current direction and bank scaling, `depthCm` with and without a `ground` function. |
| `WaterBodyRecordTest` (`tests/fluids/src/`, links `Aver.Formats`) | `RIVER/RIVERPT/LAKE/LAKEPT` parse, order kept, unknown body names skipped, round trip byte-identical, a level with no bodies writes nothing new, a `WATER`-only level is byte-identical to before. |
| `GerstnerSurfaceTest` cases added to `GerstnerWaveTest` | `gerstnerSurfaceCm(x, z)`: displacing the solved rest point lands within 0.01 cm of the requested (x, z) for random points and 4 waves at the clamped steepness limit; height/normal equal `gerstnerDisplaceCm` at that rest point; zero waves -> (0, +Z). |
| `WaterSamplerTest` (`tests/physics/src/`, links `Aver.Physics Aver.Core`, ABI only, like `BuoyancyTest`) | A box with a flat sampler settles at the same height as the plane; a sampler returning 0 outside a rectangle lets a box there fall; a tilted-normal sampler tilts a floating box over time; a nonzero `currentCmS` moves a floating box downstream (+X velocity after N steps) and none moves it with 0; -1 falls back to the plane; per-body override still wins; `aver_phys_buoyant_body_count` counts sampled bodies; clear restores the plane behaviour. |
| `SurfaceFrictionTest` (`tests/physics/src/`) | A box sliding on a static heightfield flagged surface-sampled travels further with sampler friction 0 than 1; an unflagged heightfield ignores the sampler; the sampler is called off the main thread without a race (run under the same step count 3 times, identical result). |
| `TerrainAbiTest` cases added to `FrameworkTest` | No provider: every `aver_fw_terrain_*`/`aver_fw_water_sample` returns 0 and leaves outputs untouched; a fake provider's values and strings relay exactly, truncation at `nameCap`; `AVER_FW_TERRAIN_ABI_VERSION == aver_fw_terrain_abi_version()`. |
| `AbiEnumTest` rows | `AVER_FW_WATER_KIND_*` <-> `WaterKind`; the new P/Invoke signatures match `framework_terrain_abi.h`. |
| `TerrainNodeTests.cs` (`Aver.Graph.Tests`, one `RunAll()` line in `Program.cs`) | `Validate()` clean for the seven rows; defaults (`heightCm = 180`); pure nodes compile under both compilers; emitted IL valid (fails in the native call, never `InvalidProgramException`); `AN_LandscapeLayerId` without `layer=` fails by name; `Surface.IdOf` is FNV-1a-32 of known strings, never 0. |
| `MaterialGraphTest` cases (`tests/formats/src/MaterialGraphTest.cpp`) | `VertexFlow`/`FlowNormal` emit the expected HLSL; a graph using them compiles through the offline DXC check the unification work already runs (`ptcheck.py` harness, `aver-material-unification`). |
| `GameLandscapeLayersTest` (`tests/game/src/`, no device) | `sampleGround` weights/dominant/friction/surface through a fake material desc; weight stroke before/after + `applyWeightRect` undo/redo restores bit-exact; layer remap by name; ring materials have no `@rt:` refs. |

**Verification the implementer must do and report (not claim).** Build all configs; run every test above plus `AbiEnumTest`, `LandscapeTreeTest`, `SculptTest`, `BuoyancyTest`, `GerstnerWaveTest`, `MaterialTest`, `Aver.Graph.Tests`; the **gates** (existing gates must not move: the flag is off for them); then **headed**, 1440p, on **D3D12 and Vulkan** (`--backend`): (1) a sample project with a 4-layer terrain material, painted sections, screenshot at 3 distances (near atlas detail, mid height-blend, far flat-average blend, no seams at tile or section edge, no visible pad bleed); (2) ray-driven and raster modes both; (3) a river carved through a hill, a lake in a basin, flow visibly scrolling downstream and ripples at rest on the lake; (4) a crate floating on the ocean bobbing with the waves (compare CPU height vs the rendered crest at the contact), a crate carried down the river, a crate outside a bounded `WATER` falling; (5) paint -> save -> reload -> same; stroke undo/redo; carve undo. Use the owner's `editor.ini` for editor tests (memory: FI present stall) and check the GPU is idle before timing.

## 5. Files to create or modify, and ownership

**Created (this feature owns outright)**
- `modules/formats`: none new (edits below). `docs/formats` text only.
- `modules/landscape`: `include/aver/landscape/Paint.hpp`, `src/Paint.cpp`, `include/aver/landscape/Carve.hpp`, `src/Carve.cpp` (add to `add_library(Aver.Landscape ...)`).
- `modules/fluids`: `include/aver/fluids/WaterBody.hpp`, `src/WaterBody.cpp` (add to `_fluids_sources`).
- `modules/assets`: `include/aver/assets/TextureAtlas.hpp`, `src/TextureAtlas.cpp`.
- `modules/render.pbr`: `include/aver/pbr/TerrainLayers.hpp`, `src/TerrainLayers.cpp`.
- `modules/physics`: `include/aver/physics/physics_water_abi.h`.
- `modules/framework`: `include/aver/framework/framework_terrain_abi.h`, `src/TerrainAbi.cpp`.
- `scripting/csharp/Aver.Framework/`: `TerrainNative.cs`, `Landscape.cs`, `Water.cs`, `TerrainGraph.cs`.
- `scripting/csharp/Aver.Graph/GraphTerrainNodeTable.cs`; `scripting/csharp/Aver.Graph.Tests/TerrainNodeTests.cs`.
- `sandbox/src/GraphNodeDefsTerrain.hpp`, `sandbox/src/LandscapePaintTool.cpp/.hpp` (the paint tab, `handlePaint`, scatter command), `sandbox/src/WaterSplineTool.cpp/.hpp` (the water tab, `handleWaterSpline`, handles, carve command) so `SandboxPanels.cpp`/`SandboxViewport.cpp` only gain call sites.
- Tests: `tests/formats/src/{OcLandLayersTest,OcMatTerrainTest,TextureAtlasTest}.cpp`, `tests/render.pbr/src/TerrainLayoutTest.cpp`, `tests/landscape/src/{LayerPaintTest,CarveTest}.cpp`, `tests/fluids/src/{WaterBodyTest,WaterBodyRecordTest}.cpp`, `tests/physics/src/{WaterSamplerTest,SurfaceFrictionTest}.cpp`, `tests/game/src/GameLandscapeLayersTest.cpp`.
- Docs: `docs/LANDSCAPE_LAYERS_WATER.md` (user guide: format, tools, material, nodes, limits).

**Modified, owned by this feature (additive; named blocks)**
- `modules/formats`: `OcLand.hpp/.cpp` (2.1), `OcMat.cpp` (+ `OcMat.hpp` doc only), `tests/formats/CMakeLists.txt`.
- `modules/landscape`: `Sculpt.hpp/.cpp` (extract `brushFalloff`), `CMakeLists.txt`, `README.md` (fix the stale status), `tests/landscape/CMakeLists.txt`.
- `modules/fluids`: `GerstnerWave.hpp` (`gerstnerSurfaceCm`), `WaterSurface.hpp/.cpp`, `WaterRenderer.hpp/.cpp` (`elapsedSeconds()`), `CMakeLists.txt`, `README.md`, `shaders` untouched, `tests/fluids/*`.
- `modules/render.pbr`: `Material.hpp` (block), `MaterialGpu.hpp/.cpp` (flag + pack), `MaterialSystem.hpp/.cpp` (seams), `MaterialGraphHlsl.cpp` (two nodes), `shaders/material_prelude.hlsl` (flag, `averTerrainMaps`), `CMakeLists.txt`.
- `modules/rhi/shaders/shared_prelude.hlsl` (`averWaveNormalT` family + `averFlowNormal`; the existing functions keep their signatures).
- `modules/render.voxi/shaders/voxi_rt.hlsli` (terrain branch in `rtHitSurface`).
- `modules/assets/src/TextureUpload.cpp` (extract decode/resize/mip helpers into `detail::`), `CMakeLists.txt`.
- `modules/physics/src/Buoyancy.hpp/.cpp`, `BuoyancyAbi.cpp`, `PhysicsWorld.cpp` (listener + shutdown clears + sampler arguments), `CMakeLists.txt` if a source is added.
- `modules/framework/CMakeLists.txt` (one line), `docs/ABI.md` (append), `docs/MATERIALS.md` (terrain section), `docs/LANDSCAPE_EDITOR.md` (status), `docs/formats/FORMAT_SPECS.md` (append: §7 `TERRAIN/LAYER`, §11 bodies, §11e weights).
- `Runtime`: `include/aver/game/GameLandscape.hpp`, `src/GameLandscape.cpp`, `include/aver/game/GameWater.hpp`, `src/GameWater.cpp`, `src/GameContent.cpp` + `include/.../GameContent.hpp` (`resolveTerrainAtlas`).
- `sandbox`: `SandboxPanels.cpp` (tab bar + `materialPanel` section), `SandboxViewport.cpp` (call sites, `foliagePlaceAt` factoring), `SandboxLevelEdit.cpp` (`recordLandscapeInLevel` keeps the material), `CMakeLists.txt` (new .cpp files).

**Shared files: owner named, my edits are delimited additions** (whoever merges second keeps both)

| File | Owner | My addition |
|---|---|---|
| `modules/formats/include/aver/formats/OcWorld.hpp`, `src/OcWorld.cpp` | level-instances feature | block `// ---- wave3 water bodies ----`: four structs, two vectors, parse after `WAVE`, write after the `WAVE` emitter. No other line. |
| `Runtime/src/GameApp.cpp`, `sandbox/src/SandboxApp.cpp` | shared (every wave) | `setTerrainResolver` beside `setTextureResolver`; `landscape_.installProviders()` and `water_.setGroundHeightSource(...)` beside `water_.init`. Three lines each. |
| `sandbox/src/SandboxApp.hpp`, `SandboxSelection.cpp` | shared | two `EditCmd::Kind` entries, the `landWBefore/After` and `rivers/lakesBefore/After` fields, new members/method declarations in a `// ---- wave3 layers/rivers ----` block, two `undo()/redo()` cases. |
| `sandbox/src/GraphNodeDefs.hpp` | shared | two material-graph palette entries (`VertexFlow`, `FlowNormal`) after `Rotator`; one include + call for `GraphNodeDefsTerrain.hpp` in the game-node list. |
| `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs`, `Aver.Graph.Tests/Program.cs` | shared | one `.Concat(TerrainTable())`; one `RunAll()` line. |
| `tests/abi/src/AbiEnumTest.cpp` | shared | two rows. |
| `tests/framework/src/FrameworkTest.cpp` | shared | the `TerrainAbiTest` cases appended. |
| `docs/formats/FORMAT_SPECS.md`, `docs/ABI.md` | shared, append-only | sections named above. |

**Not touched by this feature (partition)**: `modules/synapse*`, `scripting/csharp/Aver.Synapse` (AI; note: a navmesh bake could treat deep rivers as unwalkable, left to that owner, the data is `Water.Sample`); `modules/world` (`ChunkGenerator`, `LevelInstance`, streaming); the outliner (level-instances); `modules/framework/include/aver/framework/framework_abi.h` (no minor bump), `HostBridge.cs`, `ScriptHost.*` (no new export); `OcProject`/project keys (the terrain material is chosen by the existing `LANDSCAPE material` token; no new project key).

**Implementation order** (each step builds and tests): 1 formats (`.ocland` layers, `.ocmat` terrain, `OcWorld` bodies) + paint/carve + `WaterBody` + `gerstnerSurfaceCm` and their tests; 2 atlas builder, `MaterialSystem` seams, terrain HLSL (raster) + `TerrainLayoutTest`, then the ray branch; 3 `GameLandscape` layers + per-section materials + headed render check on both backends; 4 `GameWater` bodies + material nodes + headed render check; 5 physics samplers + `WaterSamplerTest`/`SurfaceFrictionTest` + host sampler; 6 framework ABI + C# + graph rows (graph-node agent, rows from section 3); 7 editor tabs, undo kinds, material panel; 8 foliage scatter, docs, stale READMEs.

## 6. Risks and open questions

1. **Atlas, not texture arrays.** The RHI has no array textures (`TextureDim` is Tex2D/Tex3D) and a material has 8 SRVs. The atlas costs memory (36 MB per material at 512 tiles, RGBA8), and a pad of 8 limits clean mips to level 3 (the flat-colour blend hides the rest). If the owner wants array textures in the RHI (both backends + the bindless table), this design's shader body keeps working with a different fetch; the on-disk formats do not change.
2. **Waves on rivers and lakes are not geometry.** v1 has the material ripple only, and buoyancy there sees a flat surface plus current. Giving them vertex waves needs a mesh mode in `WaterSurface` (compute reads rest positions from a buffer instead of a regular grid); the sampler already takes a per-body height, so only the CPU twin and the compute input change. Wanted in v1?
3. **Flow scroll is a graph effect.** A river without `FlowNormal` in its material (e.g. a plain `M_SeaWater`) still looks correct but does not appear to flow. Acceptable, or should the engine ship a default river graph material in a template?
4. **Weight textures re-uploaded whole** (~8 MB per swap on a 1025 section) every 100 ms while painting. Large sections (2049, 4097) will hitch; the follow-up is a dirty-rect `copyBufferToTexture` through a render feature.
5. **Surfaces exist only for landscape.** Footsteps on meshes still need a general physical-material concept (per mesh/material) if the owner wants one answer for every floor. This design deliberately did not add it; `Landscape.Sample` returns `Found = false` elsewhere.
6. **Layer foliage is a bake, not a streaming rule.** Painted layers do not change foliage already placed; a live layer-driven scatter needs a `GeneratorSettings` weight hook in `modules/world` (level-streaming feature's area).
7. **Underwater fog and `waterLevelCm()` remain ocean-only**; a camera in a lake does not get the underwater look. Easy follow-up through `WaterBodySet::sample`.
8. **Carving is destructive and one-shot.** Editing a river after carving leaves the old bed. A non-destructive edit-layer system (UE-style) is a different feature. Undo covers one carve step at a time.
9. **Level instances (#12)**: an instanced `.ocworld` must transform river/lake points and the section `at`; and its bodies must be added to `GameWater` per instance. Flagged, not implemented here.
10. **Physics thread-safety**: the surface sampler is invoked on Jolt worker threads and relies on weights being immutable during Play; the editor must keep painting disabled in Simulate (done in the panel, but any future hot-edit path breaks the assumption).
11. **Downgrade loses paint** (an old build re-saves `.ocland` without `LLYR/LWGT`); no version gate exists because `kAvrChunkRequired` is not enforced by any reader (`Avr1.hpp`).
12. **8-layer cap and the implicit base**: more layers means a third weightmap texture and a bigger atlas; layer 0 cannot be erased below the remainder by design.
13. **Cost of the terrain shader in GI/secondary hits** is bounded by the LITE rule (dominant layer only) but not measured; the implementer reports raster vs ray-driven frame cost on the Caldera-scale scene against the single-material baseline (check the GPU is idle first).
14. **`MaterialSystem` runtime textures and `@rt:` refs** overlap wave 4's material instances (#6). If that design lands first, the per-section desc becomes an instance with texture overrides and `@rt:` is dropped.
15. **Per-point river flow speed** varies the physical current and the scroll, but there is no flow-map texture, so side eddies and obstacles are not expressible. A baked flow map is the standard next step.
16. **Swimming**: no character swim locomotion exists; `Water.Sample` supplies depth and current for a game to build one.
