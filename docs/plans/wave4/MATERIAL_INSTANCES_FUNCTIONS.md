# Wave 4, feature #6: material instances, material parameters, static switches and material functions (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: named parameters in material graphs (scalar, vector, texture, static switch); `.ocmat` instances (a parent plus sparse overrides, chains, live propagation); static-switch permutations of the one generated shader; runtime dynamic instances from C# and graphs; reusable subgraphs (`.ocmatfn`) called from material graphs and inlined at compile; the editor surfaces that create and edit all of it. Not in scope: Substrate-style layered BSDF slots, vertex/displacement graphs, per-instance custom data for instanced draws (open question 3), more than 8 texture slots (texture-streaming sibling, open question 2), material attribute layers/blending assets, C#-source round-trip of instances from the Details panel (section 2.9).

## 0. Decisions in one screen

1. **An instance is an ordinary `MaterialDesc` in the library plus a link to its parent.** No new draw path, no second pipeline, no per-instance shader. Every draw still binds one binding set and one constant block; an instance just has different bytes in it. This is why instances cost nothing per draw and why raster, the ray-hit table and the cluster path all see them with no per-path code.
2. **Non-static parameters live in the b2 block, inline: 16 floats appended to `MaterialConstants` (192 to 256 bytes).** `kMaxDrawConstantBytes` is 256 (`RHIResources.hpp:368`), D3D12 rounds a root CBV to 256 anyway, and the ray table (`gRtMaterials`, `VoxiRenderer.cpp:3644`) is a `StructuredBuffer` of the same type, so growth needs no new binding, descriptor or Vulkan work. **This spends the last of the b2 headroom** (risk 1). No shader recompile when a parameter changes.
3. **Graph parameters are declared by nodes in the graph** (`ScalarParameter`, `VectorParameter`, `TextureParameter`, `StaticSwitchParameter`). The compiler assigns each a fixed place in the 16-float block (a per-graph *layout*, independent of static-switch values) and emits a constant-index read. Values are stored on the material **by name**, never by offset, so editing a graph never scrambles instances.
4. **A texture parameter is a named alias of one of the 8 existing texture slots.** Instances override it by writing `textures[slot]`; the binding set, the bindless `texIndex[]` and the colour-space-per-slot rule are untouched. The cap is therefore 8 textures per material, stated, not hidden.
5. **Static switches produce permutations as extra `case` arms in the one existing `averEvalMaterial` switch** (`MaterialGraphHlsl.cpp:844-873`), one registry id per (graph, switch-values) pair. A permutation is only compiled when some material asks for it; runtime switching to a permutation that was not preloaded is refused in a packaged game.
6. **Parents are linked, children are flattened.** Each instance stores its effective values plus the set of fields it overrides. Editing a parent re-copies every non-overridden field into its children (live propagation). One field table (`MatField`, section 2.1) drives file flattening, propagation, the editor's override checkboxes and the tests, so none of them can drift.
7. **Material functions are `OCGRAPH 1` text with `DOMAIN materialfn`, saved as `.ocmatfn`.** No new parser; the C++ reader, writer, undo and the node editor are reused. Interface is `FunctionInput`/`FunctionOutput` nodes. A call is inlined into the caller's pull emitter, so a function costs nothing at runtime and can contain parameters (hoisted into the caller's layout).
8. **Dynamic instances reuse the same machinery.** `Pbr.CreateInstance(parent)` makes a library material with a parent link and a registered name; `Entity.SetMaterial(name)` already takes names (`EntityScene.cs:153`), so no scene ABI change is needed. Per-entity convenience (`Entity.SetScalarParameter`) lazily makes a private instance, like Unreal's lazy MID.
9. **Parameter changes must not enter geometry/GI identity hashes** (section 2.2.4). The Voxi draw-list keys hash the whole constant block today (`VoxiRenderer.cpp:1981-2032`), so an animated parameter would rebuild the TLAS and the GI volume every frame. This is the one place the feature touches Voxi.
10. **Graphs are created from the editor**: Content Browser Add menu gets *New Material Graph*, *New Material Function*, *New Material Instance* (writes starter content that compiles). Nothing in this design requires hand-writing a graph file.

## 1. What exists today, and what is missing

| Need | Exists | Missing |
|---|---|---|
| Material data | `MaterialDesc` (`Material.hpp:105-278`): stock factors, 8 `TextureRef` slots, `graphId` (runtime-assigned, `:273`), `TerrainDesc` | Parent link, by-name parameters, override set |
| GPU block | `MaterialConstants` 192 bytes, `static_assert` at `MaterialGpu.hpp:234-236`; `_coatPad` is unused (`:147`); mirrored by cbuffer `AverMaterial` (`material_prelude.hlsl:21-96`), `AverMaterialData` (`:554-587`), `averMaterialData()` (`:590-626`); `RtMaterial` is a typedef of it (`voxi_rt.hlsli:75`) | Parameter rows |
| Dirty tracking | `MaterialLibrary::touch/update/consumeDirty` (`Material.cpp:185-215`) is one bool per material; `MaterialSystem::update` walks every material whenever any change count moves and re-resolves all 8 textures of a dirty one (`MaterialSystem.cpp:496-639`, `writeSlots` at `:560`) | Constants-only dirty; O(dirty) update |
| Graph compile | `compileMaterialGraph(const OcGraphData&)` (`MaterialGraphHlsl.cpp:775`): pull emitter, literals baked into HLSL, `SampleTexture` over the 8 slots (`:594-630`), no parameter nodes, no calls; `docs/MATERIALS.md:221` says outright "no exposed instance parameters" | Parameters, switches, functions, an environment argument |
| Registry | `MaterialGraphRegistry::add(key,name,graph)` (`MaterialGraphRegistry.cpp:16`): one id per path, ids never recycled, body-only entries (source graph not kept), `rebuild()` per add | Source retention, permutations, layouts, batching, dependency recompile |
| Dispatch | `emitSwitch` (`MaterialGraphHlsl.cpp:844`) raster arm keyed `gMaterialGraphId`, ray twin keyed `graphId` with token substitution (`:865-866`) | Token substitution for the parameter read |
| `.ocmat` | `PARENT` is parsed and kept in `OcMatExtras::parentId/parentPath` and **never applied** (`OcMat.cpp:191-193`, `OcMat.hpp:18-20`); the writer even says so (`OcMat.cpp:628-630`); FORMAT_SPECS §7 describes the intent ("a material instance overrides only listed params") | Sparse parse (which keys were present), chain flatten, instance writer, `SCALAR/VECTOR/TEXPARAM/SWITCH` records |
| Loading | `GameContent::materialForSurface` (`GameContent.cpp:1570-1602`): candidates Binaries, `Content/Materials`, `Content/<name>`; `resolveMaterialGraph` (`:1548`); a **second copy** in `AnimEditor.cpp:480-560`; graph preview registers `path_` directly (`GraphEditor.cpp:3764`) | One binder used by all three; parent resolution |
| Surface to handle | Scene stores an interned **name token**; `GameContent::authoredFor(token)` reads `surfaceMaterials_` (`GameContent.cpp:1651`); `Entity.SetMaterial(name)` interns (`EntityScene.cs:153`, `scene_abi.h:183`) | A way for a runtime-created material to be found by name |
| C#/ABI | `pbr_abi.h` factor setters by handle, `Pbr.cs` (`Aver.Scripting`, referenced by `Aver.Framework.csproj:29`); `MaterialBuilder.cs:207 Emit` writes the full `.ocmat`, its comment says the C++ writer is a second implementation that must agree | Parameter calls, instance creation, a builder instance mode |
| Editor | `materialPanel` (`SandboxPanels.cpp:214-401`) edits the shared desc with one undo entry per drag (`EditCmd::Kind::Material` snapshots whole descs); `Save to C#` rewrites the `[AverMaterial]` class (`:157-198`); *New Material* writes a C# class (`SandboxContentBrowser.cpp:403`); there is no menu item for a material graph (docs call it "hand-write `DOMAIN material`") | Parameter UI, override checkboxes, three new Add entries, `.ocmatfn` asset type, graph palette entries |
| Voxi keys | `giDrawsKey` (`VoxiRenderer.cpp:2481`) and `rtDrawHash` (`:2042`) both hash `d.mat` whole via `hashDrawMaterialInto` (`:1981`); a mover's emissive is zeroed before hashing and re-keyed by `rtMaterialKey` (`:3668-3695`, `patchRtMovers` `:2200-2220`) | The same treatment for parameter rows on any draw |

## 2. Design

### 2.1 Data model (`modules/render.pbr`, Core-only DLL)

`Material.hpp` additions. Names are the contract.

```cpp
enum class ParamKind : u8 { Scalar, Vector, Texture, Switch };
struct ParamValue {                    // one named value set on a material
    std::string name;                  // [A-Za-z_][A-Za-z0-9_]*, case-sensitive, <= 63 chars
    ParamKind   kind = ParamKind::Scalar;
    f32         v[4] = {0, 0, 0, 0};   // Scalar: v[0]; Vector: rgba; Switch: v[0] != 0
    TextureRef  tex;                   // Texture only
};

// Every authored stock field an instance can override, one id each. The enum order is the on-disk
// canonical order of the sparse writer and never changes (append only).
enum class MatField : u16 {
    BaseColorFactor, EmissiveFactor, Metallic, Roughness, NormalScale, OcclusionStrength,
    Reflectance, F90, Ior, Transmission, SubsurfaceWeight, SubsurfaceRadius, SubsurfaceColor,
    AttenuationColor, AttenuationDistance, ScatterColor, ScatterAnisotropy,
    CoatWeight, CoatRoughness, CoatF0, LightIntensity,
    Blend /*alphaMode+alphaCutoff*/, TwoSided, CastShadow, UvMode, UvTiling, SlopeBlend /*flag+lo+hi+scale*/,
    Tex0, Tex1, Tex2, Tex3, Tex4, Tex5, Tex6, Tex7,     // Tex0 + TextureSlot
    Count
};
AVER_PBR_API bool        copyField(MatField f, const MaterialDesc& from, MaterialDesc& to);   // one switch, the only copy table
AVER_PBR_API const char* matFieldKey(MatField f);       // the .ocmat spelling: "param:roughnessFactor", "tex:baseColor", "blend", "flags:twosided", ...
AVER_PBR_API bool        matFieldFromKey(std::string_view key, MatField& out);

struct InstanceInfo {                  // empty parent = a root material (every material today)
    MaterialHandle parent = 0;         // process-local, relinked on load
    std::string    parentName;         // what the file says (surface stem or content path); what the writer emits
    std::vector<MatField>     fields;  // stock fields this level overrides (sorted)
    std::vector<std::string>  params;  // graph-parameter names this level overrides (sorted)
};
```

`MaterialDesc` gains (all default-empty, so every existing material is a root with no parameters and shades bit-identically):

```cpp
std::string             graphRef;      // content-relative GRAPHREF the material's ROOT names; "" = none. Copied from OcMatExtras; not written by writeOcmat from here
std::vector<ParamValue> params;        // effective by-name values that differ from the graph defaults (sparse)
InstanceInfo            inst;
bool                    dynamic = false;   // created at runtime (section 2.8); never written to disk
```

`MaterialLibrary` additions (the DLL is Core-only; anything needing the graph registry goes through two registered function pointers, the `setTextureResolver` pattern, `MaterialSystem.hpp:48-57`):

```cpp
using GraphBinderFn  = u32  (*)(const MaterialDesc& d, void* user);                 // graphRef + Switch params -> permutation id (0 = none/failed); never compiles twice
using ParamSchemaFn  = bool (*)(u32 graphId, const char* name, ParamKind& kind, void* user); // is `name` a parameter of this graph, and of what kind
using SurfaceLoaderFn = MaterialHandle (*)(const char* surfaceName, void* user);    // load/return the resident material for a surface stem (GameContent)
void setGraphBinder(GraphBinderFn, void*);   void setParamSchema(ParamSchemaFn, void*);   void setSurfaceLoader(SurfaceLoaderFn, void*);

bool   link(MaterialHandle child, MaterialHandle parent);        // refuses a cycle; records the reverse edge
void   unlink(MaterialHandle child);
const std::vector<MaterialHandle>& children(MaterialHandle h) const;
void   touchParams(MaterialHandle h);                            // constants-only dirty (no texture re-resolve)
enum : u32 { kDirtyConstants = 1, kDirtySlots = 2, kDirtyBinding = 4 };
u32    consumeDirtyFlags(MaterialHandle h);                      // consumeDirty() stays: returns flags != 0
void   drainDirty(std::vector<MaterialHandle>& out);             // handles dirtied since the last drain, each once
u64    layoutRevision() const;                                   // moves when a graph's parameter layout changes (set by the registry through notifyLayoutChanged)
MaterialHandle createDynamic(const char* parentSurface, const char* name);   // 2.8
MaterialHandle findByName(const std::string& name) const;        // dynamic materials only
```

Semantics:
- `touch()` keeps meaning "everything" (flags 7). Setting a stock field or a parameter through any setter calls the narrowest touch. `update()`/`touch()`/`touchParams()` on a material that has children **propagate**: for each child (breadth first, cycle-free by construction) copy every field *not* in `child.inst.fields` and every parent param whose name is *not* in `child.inst.params` from the parent's effective desc (`copyField`), recompute the child's `graphId` through the binder if a Switch param or `graphRef` changed, and dirty the child with the narrowest flags the copy implies. Depth cap 8, a child set cap of 4096 per parent (log and stop).
- `create()` of an instance: the loader passes the already-flattened effective desc and then `link()`s; `destroy(parent)` clears `children`' parent link (children keep their last effective values and become roots; logged).
- `sanitise()` is extended: `params` names validated against the grammar, vectors clamped to finite, `inst.fields`/`params` kept sorted and unique, `graphRef` untouched. Parameter value ranges are *not* clamped here (the graph's `min`/`max` are editor hints, section 2.3).
- `copyField` is the single copy table. A test (4A) writes a child with exactly one field present for every `MatField` and checks it survives a flatten over a differing parent; adding a `MaterialDesc` field without a `MatField` is caught by a `sizeof`-style audit test that enumerates `MaterialDesc` members through a macro list in `Material.cpp` (same technique `MaterialTest.cpp:821-867` uses for `MaterialConstants` offsets).

### 2.2 GPU side

#### 2.2.1 Block layout
`MaterialConstants` appends, after `scatterAnisotropy` (offset 188):

```cpp
f32 params[16];     // offset 192. Four float4 rows. Packed by the graph's layout (2.3). Zero for a material with no graph or no parameters.
```
- `_coatPad` (offset 108, currently `f32`, assigned 0 in `packMaterial` and read nowhere) becomes `u32 paramGiMask` (bit i = float i of `params` may change GI-visible radiance, section 2.2.4) and `MaterialFlag_ParamRowsShift = 18`: bits 18..20 of `flags` hold the number of 16-byte rows in use (0..4). Nothing in HLSL reads either except the generated code; `_gCoatPad` in the cbuffer stays declared as `float` (same four bytes).
- `static_assert(sizeof(MaterialConstants) == 256)` replaces `:235`. Mirrors to change, **all in this feature's commit** (the file's own comment lists the same set at each earlier growth, `MaterialGpu.hpp:196-222`): `material_prelude.hlsl` cbuffer (`float4 gMatParams[4];` after `gScatterAnisotropy`), `AverMaterialData` (`float4 params[4];`) and `averMaterialData()`; `voxi_rt.hlsli` needs nothing (typedef); `tests/formats/src/MaterialTest.cpp:821-867` (size 256, `params` at 192); `modules/upgrade/src/Upgrade.cpp:152` note; `FORMAT_SPECS.md` §7 "192 bytes" sentences; `MaterialConstants` comments naming 192.
- `kMaxDrawConstantBytes` stays 256 and is now exactly consumed: add a `static_assert(sizeof(MaterialConstants) <= rhi::kMaxDrawConstantBytes)` in `MaterialGpu.hpp` so the next growth fails at compile time with the reason.
- `Voxi::Draw::mat` is `sizeof(MaterialConstants)` (`VoxiRenderer.hpp:806`): +64 bytes per recorded draw (about 0.8 MB at 12 k draws). Accepted; measured in 6.

`packMaterial(d)` (Materials static lib) gains: write `params` from the layout of `d.graphId` (defaults, then `d.params` by name), `paramGiMask`, the row count into `flags`. The name-to-offset step caches `(layoutId, offset)` inside `ParamValue` scratch fields (`mutable u32 cacheLayout; mutable i16 cacheOff`) so a per-frame `SetScalar` is a vector scan of the material's own few entries, not a map lookup per pack.

#### 2.2.2 HLSL contract
- Raster arm: the compiler emits `gMatParams[row].comp` (static indices). Ray twin: `emitSwitch`'s `rayTwin` substitution list (`MaterialGraphHlsl.cpp:861-866`) gains `"gMatParams["` to `"gAverGraphMat.params["`. `gAverGraphMat` is already set for every graph arm (`voxi_rt.hlsli:403, 494`).
- Texture parameter: the existing `averSampleSlot(slot, uv)` / `averRtSampleSlotGraph` path. No change.
- Static switch: compile-time only, nothing in HLSL (the unselected branch is not emitted).
- No new `#define`, no change to `materialShaderDefines`.

#### 2.2.3 Dirty and update path (`MaterialSystem`)
`MaterialSystem::update()` (`MaterialSystem.cpp:496`) becomes:
1. `layoutRevision` moved: mark every library material with `graphId != 0` `kDirtyConstants` (their offsets changed). Rare.
2. Otherwise iterate `lib.drainDirty()` only (O(dirty)); the full enumeration walk survives only for create/destroy (`changeCount` vs `seenLibraryChanges_` already separates them; keep the table-row logic as is, `:574-586`).
3. For each dirty handle: `kDirtyConstants` repacks `entries_[h].constants` and patches `gpuTable_[row]` **in place** (no `tableScratch_` rebuild, no `gpuRevision_` bump: rows did not move); `kDirtySlots` calls `writeSlots(.., retryFailed=true)` exactly as today. A parameter set never reaches `writeSlots`; a texture-parameter set does.
4. New `u64 paramRevision()` bumps when any constants-only patch landed this frame; Voxi uses it (2.2.4).
`writeSlots`, `resolveTexture`, the refcount/evict code are **not touched** by this feature (texture-streaming sibling owns them; shared-file rule in section 5).

#### 2.2.4 Voxi identity keys (the trap)
`hashDrawMaterialInto` (`VoxiRenderer.cpp:1981`) hashes `d.mat` whole; `giDrawsKey` (`:2481`) and `rtDrawHash` (`:2042`) feed it. Without a change an animated parameter makes "draw list changed" true every frame: a TLAS rebuild and a GI volume rebuild per frame (the known GI-gate churn class). Changes, all inside that file:
1. `hashDrawMaterialInto` takes a `ParamHash` mode: `None` (accel key: zero all of `params` before hashing, they never alter geometry or flags), `GiMasked` (GI keys: zero the floats whose `paramGiMask` bit is clear, so only parameters the author marked `gi=1` rebuild GI), `Full` (not used by keys). Hash only `192 + 16 * rows` bytes (rows from `flags` bits 18..20): a material with no parameters hashes exactly the bytes it hashed before, so there is no per-draw cost regression (this is why the row count is in `flags`).
2. `emissiveKeyTag` (`:1018`) becomes `materialKeyTag`: it additionally folds the in-use `params` bytes, so `rtMaterialKey` (`:3668`) gives a draw whose parameters changed a **new table row** (the mover/emissive precedent, `patchRtMovers` `:2200-2222` already drops unused rows and calls `buildMaterialTable` without touching the TLAS).
3. New gate branch in the accel-skip path (`:2105`): if `rtAccelDrawsKey()` is unchanged but `materials_.paramRevision()` moved since the last build, run `refreshRtMaterialParams()` = re-key the instances/foliage parts whose material `flags` rows > 0, `buildMaterialTable`, `uploadFoliagePartTable` (the `matChanged` block of `patchRtMovers` factored out), and return "unchanged" for the TLAS. Without this the ray table is stale because the skip branch never reaches `buildMaterialTable` (`:1860` is inside the full build).
4. `rtMaterialKey`'s per-frame `matConstantsScratch_` (`:1620-1622`) must be cleared when the refresh runs (implementer verifies lifetime, the comment at `:2215` shows rows are pruned by key).
Raster needs nothing: `submit()` copies `constants()` bytes each frame (`:1109`) after `MaterialSystem::update`.

### 2.3 Parameters in material graphs (`MaterialGraphHlsl.cpp`, `sandbox/src/GraphNodeDefs.hpp`)

New material-domain node types (palette category `Parameter`). All attributes are `key=value` tokens with **no spaces** (`extraTokens`, `OcGraph.hpp:49`); labels come from `name` with `_` shown as space.

| Node | Pins (out) | Attributes | Emits |
|---|---|---|---|
| `ScalarParameter` | `value:float` | `name=` (req), `default=0`, `min=`, `max=`, `group=`, `order=`, `gi=0\|1` | `gMatParams[r].c` |
| `VectorParameter` | `rgba:float4`, `rgb:float3`, `r,g,b,a:float` | `name=`, `default=1,1,1,1`, `width=3\|4` (3 leaves `.w` for a scalar), `space=linear\|srgb` (srgb: editor shows a colour picker, CPU decodes with the same `pow 2.2` as `baseColorFactor`, `MaterialGpu.cpp`), `group=`, `order=`, `gi=` | one memoised float4 fetch, pins swizzle it (the `SampleTexture` fetch-memo idiom, `:608-630`) |
| `TextureParameter` | `rgb`, `a`, `rgba` (+ input `uv`) | `name=`, `slot=` (one of the 8 `kSlotNames`, req), `default=<content path>`, `group=`, `order=` | same as `SampleTexture slot=` |
| `StaticSwitchParameter` | `result` (generic width) with inputs `on`, `off` | `name=`, `default=0\|1`, `group=` | the selected input's expression; the other branch is never walked |

Rules enforced by the compiler (every one is an error naming the node id and parameter, as the file already does, `MaterialGraphHlsl.cpp:304-322`):
- Names unique per graph across all four kinds; the same name declared in two nodes is allowed only if kind, default, slot and attributes are identical (it is one parameter, read twice). This is also what makes a function called twice share its inner parameters.
- `TextureParameter` slots are exclusive per name: two different texture parameters on one slot are an error. A `SampleTexture slot=X` and a `TextureParameter slot=X` coexist (same texture).
- Slot colour space is fixed by slot (`colourClass`, `MaterialSystem.cpp:39-48`): `baseColor`, `emissive`, `layer1BaseColor` sRGB; `normal`, `layer1Normal` normal map; `metalRough`, `occlusion`, `layer1MetalRough` linear data. The node's tooltip in the editor shows it, because a mask in the `baseColor` slot would be decoded as sRGB.
- A `TextureParameter` whose slot is empty on the root material gets its `default=` path bound by the binder (the stock fallback identity texture if none); the graph editor preview uses the same rule.
- `StaticSwitchParameter`'s width is the widest of both linked inputs regardless of the value, so every permutation types identically.

**Layout** (`MaterialParamLayout`, `aver/pbr/MaterialParams.hpp`, built by a new `analyzeMaterialGraph(graph, env)` pass that walks *every* node and every called function body, switch-independent):

```cpp
struct MaterialParam { std::string name; ParamKind kind; u8 width; u8 offset /*float 0..15; texture: slot; switch: bit*/;
                       f32 def[4]; f32 min, max; std::string group, texDefault; i32 order; bool gi, srgb; };
struct MaterialParamLayout { std::vector<MaterialParam> params; u8 rows; u16 giMask; u32 id; };
```
Packing: vectors first by (`order`, name), each takes a whole row (a `width=3` vector offers its `.w` as a hole); scalars next by (`order`, name), first-fit into holes then new rows. Deterministic, so CPU packing and emitted HLSL agree by construction (the emitter reads `offset` from the same struct). More than 16 floats: compile error `graph 'G_X' needs 19 of 16 parameter floats: Tint(4) Detail(4) ...; mark fewer as parameters or make one a static switch`. Texture parameters and switches take no floats.

`gi=1` sets the parameter's float bits in `giMask`. Default `gi=0`: a parameter is assumed not to affect GI-visible radiance (a roughness slider, a dirt amount). Emissive-strength or albedo parameters that should relight the scene set `gi=1`; the cost is a GI volume rebuild on change (the same as editing the material today).

### 2.4 Static switches and permutations (`MaterialGraphRegistry`)

- A graph's `StaticSwitchParameter`s (max 8) define a *switch set*. A permutation is the graph compiled with a concrete value vector. Registry key: `<graph key>?S=<Name>:<0|1>,...` over **all** the graph's switches sorted by name, so an unset switch and an explicit default are the same key.
- Registry changes: each `Entry` keeps `std::shared_ptr<const fmt::OcGraphData> src` and its base key, switch vector, `layoutId`, `fnDeps` (called function stems with mtimes). New API:
  ```cpp
  u32  add(const std::string& key, const std::string& name, const fmt::OcGraphData& g);             // unchanged: all-defaults permutation
  u32  addPermutation(const std::string& baseKey, const std::vector<std::pair<std::string,bool>>& sw);   // 0 + log if the base is unknown/uncompilable
  const MaterialParamLayout* layoutOf(u32 id) const;   const std::vector<SwitchDecl>* switchesOf(u32 id) const;
  u32  baseIdOf(u32 id) const;   u64 layoutRevision() const;
  void beginBatch();  void endBatch();                 // one rebuild() and one revision bump per batch (level load, project open)
  void setFunctionResolver(const MaterialFunctionResolver*);   usize recompileDependents(std::string_view fnStem);
  ```
  `ids` are still never recycled and `clear()` still does not reset `nextId_` (`MaterialGraphRegistry.cpp:56`).
- Limits (errors, logged once per graph): 8 switches per graph, 16 permutations per graph, 128 registry entries in the process, 256 KiB of generated HLSL in total. The last two exist because every pipeline that calls `averEvalMaterial` recompiles when `revision()` moves (`VoxiRenderer.cpp:1182`, `ActorPreview.cpp:687`, `SandboxAssets.cpp:298`).
- Binder (`MaterialBinder.cpp`, Materials static lib, installs itself as the library's `GraphBinderFn`/`ParamSchemaFn` in `MaterialSystem::init`): given a `MaterialDesc`, loads the graph at `contentDir\graphRef` through `fmt::loadOcgraph` once (cached by path+mtime), `add`s it, then `addPermutation` with the desc's `Switch` params, and returns the id. `GameContent::resolveMaterialGraph` (`GameContent.cpp:1548`) becomes a one-line wrapper; `AnimEditor.cpp:480-560` and the graph editor preview call the same function.
- **Runtime policy** `RENDER.MATERIALPERMUTATIONS` (project key, `OcProject`/`ProjectDesc`, default `editor`): `editor` compiles on demand; `preloaded` (the default of a packaged `Runtime`, set by `GameApp` before level load) refuses `SetStaticSwitch` to a permutation the registry does not already hold, returns 0, logs once. Preloading: every loaded `.ocmat` registers its own permutation at load; a root `.ocmat` may add `PERMUTATION UseDetail=1,UseFog=0` records (any number) to prewarm combinations scripts will switch to; the project cook lists them.
- Compile of the pipelines is already background-capable (async pipeline creation); the first frame on a new permutation shows the last good shader. The editor logs `[MaterialGraph] 'G' permutation S=..: id N, X ms, graph HLSL now Y KiB`.

### 2.5 Material functions (`.ocmatfn`)

Format: `OCGRAPH 1`, then `DOMAIN materialfn` directly under it. `OcGraphDomain` gains `MaterialFunction` (`OcGraph.hpp:226`, `OcGraph.cpp:520-535`); a build without it reads `Unknown` and skips the file, which is the documented safe direction (FORMAT_SPECS §10a `DOMAIN`). `HostBridge.DeclareGraphClasses` and `GameApp::discoverProjectGraphs` walk `*.ocgraph` only and never see `.ocmatfn`.

```
OCGRAPH 1
DOMAIN materialfn
NAME MF_Triplanar
DESCRIPTION Projects a texture along the three world axes.

NODE inUv  FunctionInput  -300 0 name=Scale type=float default=1 order=0
PIN inUv value out float
NODE outC  FunctionOutput  500 0 name=Color type=float3 order=0
PIN outC value in float3
NODE tex   TextureParameter 100 0 name=Albedo slot=basecolor
...
LINK tex.rgb outC.value
```
Interface nodes:
- `FunctionInput name= type=float|float2|float3|float4 default=<comma list|@uv|@time> order=`; output pin `value` of that type. `@uv`/`@time` reproduce the unwired-reads-the-surface convention (`uvInput`/`timeInput`, `MaterialGraphHlsl.cpp:270-282`). Names unique per function, 63 chars.
- `FunctionOutput name= type= order=`; input pin `value`. At least one; names unique among outputs.
- `CallFunction fn=<stem> [prefix=<name>]`: pins are the callee's inputs and outputs by name. `fn` is the **file stem**, unique project-wide (a duplicate stem is a compile error naming both paths), resolved by the function resolver (below); stems, like surface names, survive folder moves, and `.ocmatfn` joins `cbFindReferencesTo`'s extension list for renames (section 5). `MaterialOutput` is illegal in a function; `FunctionInput/Output` are illegal in a material graph (errors).
- Allowed inside a function: the whole material vocabulary, parameter nodes (hoisted), `CallFunction` (nesting depth 16, recursion is an error listing the cycle), `SampleTexture`/`TextureParameter` (they sample the *calling material's* slots; functions are inlined, not isolated).

Compile (changes in `MaterialGraphHlsl.cpp`): `Emitter` gains `const OcGraphData* g` (swappable) and a frame stack `{graph*, callNodeId, inputs: name->Value, prefix}`. `resolve()` memo keys and the cycle `stack` are prefixed with the frame path (`c1/c2/`), so two calls of one function emit independent statements while parameter reads share the layout entry. `emitNode("CallFunction")`: resolve each input pin in the *caller* frame (unlinked and untyped: the function's declared default; typed literal on the call pin wins), push a frame, resolve the `FunctionOutput` node's `value` input in the callee, pop, bind. `FunctionInput` in a frame returns its bound `Value` (widened to its declared type with the existing `widen()`). A call-site pin that names no interface member, or an interface member with no pin, is an error that lists the interface (`function 'MF_X' has inputs: Scale, Uv; the call node has: Scale, Foo`). The editor re-syncs call-node pins to the callee interface on load and after a callee save, dropping links to removed pins with a visible warning.

Parameters inside a function: hoisted into the caller's layout under their own name, or `prefix.Name` when the call sets `prefix=` (so a function called twice can expose two independent parameter sets, e.g. `Moss.Tint`, `Dirt.Tint`). Names with `.` are legal only through `prefix`. Duplicate resolution follows the section 2.3 rule.

Resolver: `MaterialFunctions` (`aver/pbr/MaterialFunctions.hpp`, Materials static lib) indexes `*.ocmatfn` under the content root lazily (stem to path), caches parsed graphs with mtimes, exposes `setContentRoot`, `refresh()` (called by the editor on save and by the content watcher when a `.ocmatfn` changes) and `find(stem)`. After a refresh the registry's `recompileDependents(stem)` recompiles every entry whose `fnDeps` list the stem (ids unchanged, `add`'s existing same-id-new-body path, `MaterialGraphRegistry.cpp:28-38`) inside one `beginBatch/endBatch`, and bumps `layoutRevision` if a layout changed. A parse or compile failure in a callee marks the *calling* graph uncompilable with `function 'MF_X' (path): <its error>`, the broken-graph policy unchanged (material shades stock).

Editor (GraphEditor): `openGraphDomain()` (`GraphEditor.cpp:497`) keeps returning `kDomainMaterial` for `materialfn` files (the palette bitmask is not extended; a small `nodeAllowedIn(typeId, isFn)` filter hides `MaterialOutput` in functions and `FunctionInput/Output` in materials, so the ~60 material rows in `GraphNodeDefs.hpp` stay untouched). The Viewport tab of a function shows a sphere shaded by a synthetic wrapper graph (one `CallFunction` with default inputs, its first float3/float output wired to `BaseColor`, a scalar output splatted) with an output dropdown. Library tab: *Function Library* lists every `.ocmatfn` with a drag target onto the canvas (creates a pre-pinned `CallFunction`). Asset presentation: `GraphAssetFamily` (`GraphAssetPresentation.hpp:21`) gains `MaterialFunction` ("Material Function").

### 2.6 `.ocmat` instances: format and resolution

New text records (FORMAT_SPECS §7 gets a new subsection `7.y`; all optional, order free, unknown tokens skipped as today):

```
OCMAT 1
NAME M_WoodDark
PARENT {path:M_Wood}                  # surface stem OR content-relative .ocmat path (as materialForSurface resolves)
PARAM roughnessFactor 0.8             # in a file with PARENT, every PARAM/TEX/BLEND/CULL/FLAGS record present IS an override
TEX baseColor {path:Textures/wood_dark.png} uv0 sRGB
SCALAR WearAmount 0.35                # graph parameters, by name (valid on a root too: it overrides the graph default)
VECTOR Tint 0.2 0.1 0.05 1
TEXPARAM Detail {path:Textures/detail.png}
SWITCH UseDetail 1
PERMUTATION UseDetail=0,UseFog=1      # prewarm, repeatable
```
- Without `PARENT` the file is a root: every field is "set" as today and `writeOcmat` is unchanged. A root writes `SCALAR/VECTOR/TEXPARAM/SWITCH` only for parameters whose value differs from the graph default. `GRAPHREF`, `TERRAIN`, `LAYER`, `SHADER` in a file with `PARENT` are ignored with a warning (the root owns the shader); everything else in `OcMat.cpp`'s grammar is overridable.
- **Present-key tracking**: `OcMatExtras` gains `std::vector<std::string> present` (canonical `matFieldKey` spellings plus `param:<name>` for graph params) filled by `parseOcmat`; `OcMatExtras::params` is not needed (values land in `MaterialDesc::params`). The parser's PARAM chain stays; a unit test (4A) asserts every key it accepts maps through `matFieldFromKey`.
- `SCALAR/VECTOR/TEXPARAM/SWITCH` values: finite floats; a `SWITCH` takes 0/1; a repeated name keeps the last. Unknown graph parameter names are *kept* (they may belong to a later graph revision) and warned once at bind: `[Material] 'M_X': SCALAR 'Foo' is not a parameter of graph 'G_Y' (it has: ...)`. Kind mismatch (a `SCALAR` naming a vector) is warned and ignored.
- **Resolution** (new `modules/formats/src/OcMatInstance.cpp`, `OcMatInstance.hpp`, in `Aver.Formats.Material`, which already depends on `Aver.Render.PBR`): `loadOcmatResolved(ctx, nameOrPath, out, extras, err)`, with `ctx` = content/binaries directories and a file reader. Parse the leaf; if `PARENT`, resolve the parent through the same candidate order as `materialForSurface` (Binaries, `Content/Materials`, `Content/<name>`; `editor::projectMaterialStems` in `MaterialResolve.hpp` shares it) recursively, depth cap 8, cycle error printing the chain (`M_A -> M_B -> M_A`); start from the parent's effective desc, then `copyField` for each present key, merge params by name (child wins), set `inst.parentName/fields/params`. `extras.graphRef/shader/cull` come from the root. A missing parent makes the instance **fail to load** (the surface draws the fallback, one error naming the file and the missing parent): a half-applied instance would look plausibly wrong. This is the one place the "broken graph degrades to stock" policy is not copied, on purpose.
- Loader (`GameContent::materialForSurface`, `GameContent.cpp:1570`): uses `loadOcmatResolved`; for an instance, loads the parent *through itself* (`materialForSurface(parentName)`, which caches in `materialAssets_` and is the `SurfaceLoaderFn`) so parent and child are library materials, `link()`s them, `graphId = binder(desc)`. `releaseMaterialsExcept` (`:1616`) keeps a parent alive while any kept material descends from it. Level-scoped residency applies to instances like any material.
- **Writers** (`writeOcmat`, new `writeOcmatInstance(desc, extras)` beside it): an instance writes `OCMAT 1`, `NAME`, `PARENT {path:<parentName>}` and only its `inst.fields` (as the existing record spellings) and `inst.params`, plus `PERMUTATION` lines; the "PARENT is recorded but not yet applied" comment in `OcMat.cpp:629` is removed. `MaterialBuilder.cs` is the second writer (its comment, `:12`), see 2.8; a test (4A) feeds both the same description and diffs the bytes.

### 2.7 Loading paths and the single binder
Three duplicate resolutions collapse into one call: `GameContent::materialForSurface` (`GameContent.cpp:1570`), `AnimEditor.cpp:480-560` (its comment at `:436-442` says it deliberately does not use GameContent; the cause was ownership, not behaviour), and `GraphEditor::buildMaterialPreview` (`GraphEditor.cpp:3764`). All use `fmt::loadOcmatResolved` + `pbr::bindMaterial(desc)`. The graph editor preview stops relying on a stock desc: it keeps one transient library material `previewMat_` (graph id from the binder, parameters at the graph defaults, texture parameters at `default=`) and passes it as `PreviewDraw::materialHandle` (`ActorPreview.hpp:20-60`; `ActorPreview.cpp:836-844` already resolves a handle to a binding set and constants, then overwrites `graphId`: with a real material this is the fix that makes parameters show their defaults in the preview instead of zeros). The preview of an instance in the Details panel is the real material.

### 2.8 Runtime: dynamic instances, C ABI, C#, graphs

**C ABI** (`pbr_abi.h`, additive, same return conventions: 1/0, neutral values for stale handles; the DLL has no version macro, the header gets a "MINOR 1" comment like `scene_abi.h:26`):

```c
aver_pbr_material aver_pbr_create_instance(const char* parentSurface, const char* name /* "" = "<parent>#dyn<N>" */);
int32_t     aver_pbr_is_instance(aver_pbr_material m);          aver_pbr_material aver_pbr_get_parent(aver_pbr_material m);
const char* aver_pbr_get_surface_name(aver_pbr_material m);     /* the name Entity.SetMaterial takes; "" for a non-registered material */
int32_t aver_pbr_set_scalar_parameter(aver_pbr_material m, const char* name, float v);
int32_t aver_pbr_get_scalar_parameter(aver_pbr_material m, const char* name, float* out);   /* effective value; 0 if not a parameter of the graph */
int32_t aver_pbr_set_vector_parameter(aver_pbr_material m, const char* name, float r, float g, float b, float a);
int32_t aver_pbr_get_vector_parameter(aver_pbr_material m, const char* name, float* out4);
int32_t aver_pbr_set_texture_parameter(aver_pbr_material m, const char* name, const char* contentPath);   /* "" clears to the parent's */
int32_t aver_pbr_set_static_switch(aver_pbr_material m, const char* name, int32_t on);    /* may bind another permutation; 0 if refused by RENDER.MATERIALPERMUTATIONS=preloaded */
int32_t aver_pbr_get_static_switch(aver_pbr_material m, const char* name);
int32_t aver_pbr_clear_parameter(aver_pbr_material m, const char* name);                 /* back to the parent's value */
int32_t aver_pbr_parameter_count(aver_pbr_material m);   const char* aver_pbr_parameter_name(aver_pbr_material m, int32_t i);   int32_t aver_pbr_parameter_kind(aver_pbr_material m, int32_t i);
```
Setters check the name against `ParamSchemaFn(graphId, ...)`: unknown name or kind mismatch returns 0 and logs once per (material, name). A scalar set that equals the current value is a no-op (no dirty). `create_instance` calls the `SurfaceLoaderFn` for the parent (so a parent not yet resident loads), copies its effective desc, `dynamic = true`, `link()`s, registers `name` in a DLL-side name map; `Pbr.cs`/`GameContent` look it up with `findByName`. `GameContent::materialForSurface(name)` and `authoredFor(token)` (`GameContent.cpp:1651`) consult `findByName` first (one hash lookup), so a dynamic material is drawable through the existing token path with no scene ABI change. `releaseProjectMaterials` and a level change destroy dynamics owned by the old session; `Dispose()` destroys one early.

**C#** (`Aver.Scripting/Pbr.cs`: `PbrMaterial` gains `CreateInstance`, `Parent`, `SetScalarParameter`, `GetScalarParameter`, `SetVectorParameter`, `SetTextureParameter`, `SetStaticSwitch`, `ClearParameter`, `Parameters`; XML docs on all). `Aver.Framework/MaterialInstances.cs` (new):

```csharp
public sealed class MaterialInstance : IDisposable {            // owns one dynamic instance
    public string Name { get; }  public bool IsValid { get; }
    public bool SetScalar(string p, float v);  public bool SetVector(string p, float r,float g,float b,float a=1f);
    public bool SetTexture(string p, string contentPath);  public bool SetSwitch(string p, bool on);  public bool Reset(string p);
    public bool TryGetScalar(string p, out float v);  public void Dispose();
}
public static class Materials {
    public static MaterialInstance? Create(string parentSurface, string? name = null);
    public static MaterialInstance? ForEntity(Entity e);        // the entity's private instance, created on first use from its current material
    public static void Update();                                // once per frame group 0 (HostBridge.DispTickAll, beside UiSystem.Pump): frees instances of dead entities
}
public readonly partial struct Entity { /* Gas/EntityAbilities.cs precedent */
    public bool SetScalarParameter(string p, float v);  public bool SetVectorParameter(string p, float r,float g,float b,float a=1f);
    public bool SetTextureParameter(string p, string path);  public bool SetStaticSwitch(string p, bool on);  public bool ResetParameter(string p);
}
```
`ForEntity` reads `CMeshRenderer.material` (`EntityScene.cs:153` writes it) and maps the token with `aver_scene_material_name`. **Limitation, stated**: an entity whose `material` is 0 uses its mesh's slot-0 default (`GameContent::meshDefaultMaterial`, `GameContent.cpp:1638`), which C# cannot see; `ForEntity` returns null with the log `entity N has no explicit material; call SetMaterial(name) first`. Open question 4. The sweep follows GAS's choice (no per-entity unbind callback for plain entities): `Update()` checks `Entity.IsAlive` over the entries and destroys orphans; `Shutdown` on script unload drops everything (the `Timers.Shutdown` reason, `HostBridge.cs:294`), with `HostBridge` one-line calls only.

**Graph nodes** (section 3). **Builder** (`Aver.Materials/MaterialBuilder.cs`): `Parent(string)` switches to instance mode; every existing setter records a `HashSet<string> _set` entry using the same `matFieldKey` spellings (a table in the C# file mirrored by a test); `Scalar(name,v)`, `Vector(name,r,g,b,a)`, `TextureParam(name,path)`, `Switch(name,on)`, `Permutation(...)`; `Emit` in instance mode writes `PARENT` + only set fields in `MatField` order + params sorted by name. `Slot` is extended with the three layer1 members so `TextureParam` and `Texture` reach all 8 slots (`MaterialBuilder.cs:186-195` only knows 5). `Aver.MaterialCompiler/Program.cs` is unchanged (duplicate detection by bound name still applies). Instance classes cannot be saved from the Details panel (2.9).

### 2.9 Editor surface

1. **Content Browser** (`SandboxContentBrowser.cpp`): type table (`:1904`) adds `.ocmatfn` ("Material Function"); an `.ocmat` whose head (first 4 KB, cached by mtime) contains `PARENT` presents as "Material Instance" (`GraphAssetPresentation`-style pure function, tested headlessly); Add menu (`:1328`) gains *New Material Graph* (writes `G_<Name>.ocgraph` with `DOMAIN material`, `MaterialOutput`, a `VectorParameter Tint` into `BaseColor`, a `ScalarParameter Roughness` into `Roughness`, plus a root `M_<Name>.ocmat` in `Content\Materials` with `GRAPHREF`), *New Material Function* (writes `MF_<Name>.ocmatfn` with one input and one output), *New Material Instance* (also on the context menu of a material: `Content\Materials\<Parent>_Inst[N].ocmat` with `PARENT`, no overrides, then opens it in Details). All through `editor::writeNewFile` and `cbAdoptNewAsset` like `cbCreateGameplayDefs`; menu items get `uiReg_.track("cb.add.materialGraph"|"materialFunction"|"materialInstance")`. `cbIsSourceFile` (`:315`) is not extended: these open in the graph editor / Details.
2. **Details > Material** (`materialPanel`, `SandboxPanels.cpp:214`):
   - Header: `Instance of M_Wood` (clickable) / `Root material`; a chain breadcrumb; *Create Instance* button.
   - For an instance every stock control gets a leading override checkbox keyed by `MatField` (a small `ovr(MatField, drawWidget)` helper wrapping the existing `track` lambda); unchecked = greyed, shows the parent's effective value, ticking copies it into the child and adds the field to `inst.fields`; unticking removes it and re-copies from the parent. A reset-arrow per overridden row. Roots show no checkboxes.
   - **Parameters** section (any material whose `graphId` has a layout): grouped by `group`, ordered by `order` then name; Scalar = slider if `min`/`max` set else drag; Vector = colour picker (`space=srgb`) or `DragFloat4`; Texture = the existing slot path field + asset drop; Switch = checkbox (a change that needs a new permutation shows `compiling...` until `layoutRevision`/pipeline rebuild settles). Same override/reset rules. Edits call `touchParams` and ride the existing bracketed undo (`EditCmd::Kind::Material` snapshots the whole desc, including `params` and `inst`, so undo of an override toggle just works; `update()` restores the link info).
   - **Save**: new `GameContent::materialSourcePath(name)` says where a resident material came from. A Content `.ocmat` (root or instance) saves with `saveOcmat`/`saveOcmatInstance` (atomic, `OcMat.cpp:667`), no C# involved. A Binaries material from a C# class keeps *Save to C#* (roots only); for a C#-defined instance the panel is read-only for fields with a note `defined in code: edit the Parent(...) chain in <file>.cs`. Editing a parent in the panel propagates to open children immediately (library propagation); the status line shows `N instance(s) updated`.
3. **Graph editor**: parameter/switch/function nodes in the palette (`GraphNodeDefs.hpp`, new categories `Parameter`, `Function`), same `pin()`/`attr()` idiom; node attribute panel gets a *Parameter* editor (name uniqueness check, kind, defaults, group); a **Parameters panel** listing the layout (`4 of 16 floats used`, per-parameter row, red on overflow) beside the existing Variables panel pattern; the compile error line (`GraphEditor.cpp:3355`) shows layout errors with node ids.
4. **Material preview** accepts a chosen permutation (switch checkboxes) and parameter overrides for a quick look, in memory only.

### 2.10 Settings, console, quality ladder
- No quality-ladder entry: nothing here trades quality for speed. The one policy knob is the project key `RENDER.MATERIALPERMUTATIONS` (`editor` | `preloaded`; editor default `editor`, packaged default `preloaded`), parsed in `OcProject` beside the other `RENDER.*` keys; not live-switchable (a runtime flip only affects later requests).
- Limits as `constexpr` in `MaterialParams.hpp`: `kMaxMaterialParamFloats=16`, `kMaxStaticSwitches=8`, `kMaxPermutationsPerGraph=16`, `kMaxGraphEntries=128`, `kMaxFunctionDepth=16`, `kMaxInstanceDepth=8`.
- CLI (sandbox): `--material-report` prints the registry (graphs, permutations, layouts, floats used, HLSL KiB), the library (roots, instances, dynamics, links) and the last update() cost; `--dump-material-hlsl <dir>` writes `materialGraphHlsl()` and per-graph bodies for review. Both run after level load in the headed editor like `--gpu-timing`.

### 2.11 Vulkan and D3D12 parity
- Nothing in the design is backend-specific: b2 is a plain constant block on both (`setDrawBinding` copies up to 256 bytes on both backends, `VulkanRenderContext.cpp:451`; the Vulkan path binds it through `setConstantBuffer(kDrawConstantRegister, ...)`, `:1692`, so 256 bytes is a uniform buffer range, not a push constant, and the "b1 = push constants" trap in the project notes does not apply). `float4 gMatParams[4]` is a legal SPIR-V array in a uniform block with 16-byte stride.
- No SM 6.6 features (no `ResourceDescriptorHeap`), nothing the Vulkan path refuses; the mesh-shader variants of the cluster path that Vulkan refuses use the same pixel shader function and are unaffected.
- **Material graphs have never been compiled for SPIR-V** (`docs/MATERIALS.md:222`; `MaterialGraphTest` loads `dxcompiler.dll` and asks for DXIL only). This feature adds `MaterialGraphSpirvTest` (4A) compiling the generated raster arm and ray twin with `third_party/dxc-spirv/dxcompiler.dll -spirv` for a graph exercising every new node, and a headed Vulkan run of the fixture (section 7). A failure there is fixed in this feature, not deferred, because the new parameter path is the first thing to put arrays into the generated block.
- What Vulkan lacks that touches this: nothing new. The bindless gap in the ray path is why texture parameters are slot aliases and not extra bindless entries on either backend.

## 3. Graph nodes (gameplay graphs, C#-facing)

Owner class `MaterialParamGraph` (`internal static`, `scripting/csharp/Aver.Framework/MaterialParamGraph.cs`, written first), rows in `scripting/csharp/Aver.Graph/GraphMaterialParamNodeTable.cs` (`private static GenericNodeSpec[] MaterialParamTable()`, one `.Concat(...)` added to `Table()` in `GraphGameSystemNodeTable.cs`, merge-adjacent), palette mirror `sandbox/src/GraphNodeDefsMaterialParams.hpp` (included from `GraphNodeDefs.hpp` like `appendGasNodeDefs`, category `Material`). Same resolution rule as GAS: the parameter name is a NODE attribute (`param=Wear`), never a pin (`PinType` has no strings).

| Type | Kind | Method | Inputs | Outputs | Attribute |
|---|---|---|---|---|---|
| `AN_SetMaterialScalar` | Exec | `bool SetScalarForGraph(int entity, string param, float value)` | entity, value | success | `param` |
| `AN_SetMaterialVector` | Exec | `bool SetVectorForGraph(int entity, string param, float r, float g, float b, float a)` | entity, r,g,b,a (a=1) | success | `param` |
| `AN_SetMaterialTexture` | Exec | `bool SetTextureForGraph(int entity, string param, string path)` | entity | success | `param`, `path` |
| `AN_SetMaterialSwitch` | Exec | `bool SetSwitchForGraph(int entity, string param, bool on)` | entity, on | success | `param` |
| `AN_ResetMaterialParam` | Exec | `bool ResetParamForGraph(int entity, string param)` | entity | success | `param` |
| `AN_GetMaterialScalar` | Pure | `bool GetScalarForGraph(int entity, string param, out float value)` | entity | value, success | `param` |
| `AN_SetMaterialInstance` | Exec | `bool SetInstanceForGraph(int entity, string parentSurface)` | entity | success | `parent` | (gives the entity a fresh private instance of `parent` and assigns it)

Row strings follow the existing grammar, e.g. `Reg("an_setmaterialscalar", GenericKind.Exec, typeof(MaterialParamGraph), "SetScalarForGraph", "entity:int,value:float", "success:bool", "p:entity,@param,p:value", "success")`. `GameSystemNodes.Validate()` checks every row against the real method. Failure never throws: false plus one log per node id. `entity` 0 fails.

## 4. Tests

**A. Native, `tests/formats` (registered in `tests/formats/CMakeLists.txt`; names exact)**

| File | Asserts |
|---|---|
| `MaterialInstanceTest.cpp` | `matFieldKey`/`matFieldFromKey` round trip for every `MatField`; `copyField` per field; every `PARAM` key `parseOcmat` accepts has a `MatField`; flatten of a 3-deep chain equals an explicit root with the same values (desc compare field by field); a present key equal to the parent's still counts as an override; sparse writer emits only overridden records in `MatField` order and re-reads to the same desc; cycle and depth-9 chains refused with the chain printed; missing parent fails the load; `GRAPHREF/TERRAIN` in an instance ignored with a warning; `SCALAR/VECTOR/TEXPARAM/SWITCH/PERMUTATION` parse, clamp non-finite, last wins; `MaterialBuilder` bytes equal `writeOcmatInstance` bytes for the same description (the two-writer rule). |
| `MaterialLibraryInstanceTest.cpp` | `link` refuses a cycle; parent edit propagates to non-overridden fields only, through two levels, in one `drain`; overridden fields survive; destroying a parent unlinks children and keeps their values; `touchParams` yields `kDirtyConstants` only; `drainDirty` returns each handle once; `update()` snapshot/undo restores `inst`. |
| `MaterialParamLayoutTest.cpp` | packing is deterministic and independent of switch values; vec3 + scalar share a row; 17 floats errors with the listing; `giMask` bits; duplicate name with identical decl is one parameter, with different decl an error; two texture params on one slot errors; CPU `packMaterial` writes defaults then overrides at the offsets the emitter reads (HLSL text contains `gMatParams[1].z` for the offset the layout reports). |
| `MaterialGraphParamTest.cpp` | Through real DXC (the `MaterialGraphTest` harness): a graph with all four parameter nodes compiles for the raster arm and the ray twin (`AVER_RT_BINDLESS`), the twin text contains `gAverGraphMat.params[` and no `gMatParams[`; static switch emits only the selected branch (the other branch's unique node text absent); both permutations compile; a type error in the dead branch is caught by `validateAllBranches`; `TextureParameter` and `SampleTexture` on one slot emit one fetch each only per node; stock graphs' generated text is byte-identical to before this change (golden, guards the "no graph pays nothing" claim). |
| `MaterialFunctionTest.cpp` | A function inlined twice emits independent temporaries; defaults (`@uv`, `@time`, literal) apply for unlinked inputs and a typed call-pin literal wins; unknown/extra call pins error with the interface listed; recursion and depth 17 error; hoisted parameters share by name and split by `prefix=`; `MaterialOutput` in a function and `FunctionInput` in a material error; a callee edit followed by `recompileDependents` changes the caller's body with the same id; callee failure names the callee; `DOMAIN materialfn` round-trips through `writeOcgraph` byte for byte. |
| `MaterialPermutationTest.cpp` | Same base + same switches gives the same id; unset equals explicit default; ids never recycled across `clear()`; `beginBatch/endBatch` bump `revision()` once; limits (9 switches, 17 permutations, 129 entries) refuse with a log; `RENDER.MATERIALPERMUTATIONS=preloaded` refuses an unloaded permutation. |
| `MaterialGraphSpirvTest.cpp` | The Vulkan gap: compiles the parameter fixture's raster arm and ray twin with the SPIR-V DXC (skipped, not failed, when `third_party/dxc-spirv/dxcompiler.dll` is absent). |
| `MaterialTest.cpp` (edit) | `sizeof == 256`, `params` at 192, `paramGiMask`, flag row bits; existing 192-byte assertions updated, every earlier offset unchanged. |
| `MaterialDirtyTest.cpp` | A scalar set after `update()` repacks the constants and the GPU table row without calling `writeSlots` (counter) and without a `gpuRevision_` bump; a texture-parameter set does call it; update cost is O(dirty) (10 000 materials, 1 dirty, wall time under a fixed bound relative to 1 000 materials). |

**B. Editor, `tests/editor`**: `MaterialInstancePresentationTest.cpp` (head-sniff classification: instance, root, unreadable); `MaterialRefScanTest.cpp` (`.ocmatfn` and `PARENT` references are found and rewritten by `cbFindReferencesTo`/`cbRewriteReferences`, anchored matching); `GraphEditorMaterialFnTest.cpp` (load/save a `.ocmatfn`, call-pin resync after a callee interface change drops links with a warning, palette filter by `nodeAllowedIn`); a `SandboxSelfTests.cpp` case driving *Create Instance* then toggling an override then undo.

**C. Managed**: `scripting/csharp/Aver.Graph.Tests/MaterialParamNodeTests.cs` (registered by one line in `Program.cs`, mirrors `GameSystemNodeTests`): rows validate against methods, emitted IL is valid for each node, a missing `param=` fails by name, a sample graph parses. `Aver.Framework.Tests` gets `MaterialInstanceApiTests.cs` with a fake pbr backend behind an interface (`IMaterialBackend`, the `IGasHost` precedent in `GAS.md` 2.1): `ForEntity` caches per entity, sweep frees on death, null on token 0, `Reset` semantics. A real-CLR test `tests/save`-style is not needed (no save state).

**D. Headed verification the implementer performs and reports (not claims)**: section 7.

## 5. Files to create or modify, and ownership

**Created (this feature owns outright)**
- `modules/render.pbr/include/aver/pbr/MaterialParams.hpp`, `MaterialBinder.hpp`, `MaterialFunctions.hpp`; `modules/render.pbr/src/MaterialFields.cpp` (DLL target: add to `add_library(Aver.Render.PBR SHARED ...)`, `CMakeLists.txt:9`), `src/MaterialParams.cpp`, `src/MaterialBinder.cpp`, `src/MaterialFunctions.cpp` (Materials static target)
- `modules/formats/include/aver/formats/OcMatInstance.hpp`, `modules/formats/src/OcMatInstance.cpp` (target `Aver.Formats.Material`, `modules/formats/CMakeLists.txt:120`)
- `sandbox/src/GraphNodeDefsMaterialParams.hpp`, `sandbox/src/MaterialFunctionLibrary.cpp/.hpp` (function library tab), `sandbox/src/MaterialParamPanel.cpp/.hpp` (Details parameters section and Parameters panel)
- `scripting/csharp/Aver.Framework/MaterialInstances.cs`, `MaterialParamGraph.cs`; `scripting/csharp/Aver.Graph/GraphMaterialParamNodeTable.cs`
- tests in section 4; `test-content/MaterialInstances/` (section 7)
- docs: `docs/MATERIALS.md` (rewrite section 10's "no exposed instance parameters" bullet, add instance/function/parameter sections, fix "192 bytes", the Vulkan sentence), `docs/formats/FORMAT_SPECS.md` (section 7.y records, §10a `DOMAIN materialfn`, new section for `.ocmatfn`, block size 256), `docs/AVER_NODE_NODES.md`, `docs/ABI.md` (pbr ABI additions), `modules/render.pbr/README.md`

**Modified (this feature's edits)**
- `modules/render.pbr`: `Material.hpp/.cpp` (2.1, DLL ABI functions), `pbr_abi.h`, `MaterialGpu.hpp/.cpp` (2.2.1), `MaterialSystem.hpp/.cpp` (**only** `update()` and the dirty/paramRevision members; not `writeSlots`/`resolveTexture`/retain/release), `MaterialGraphHlsl.hpp/.cpp` (environment argument, parameter/switch/function nodes, substitution), `MaterialGraphRegistry.hpp/.cpp`, `shaders/material_prelude.hlsl` (cbuffer + `AverMaterialData` + `averMaterialData()` only), `CMakeLists.txt`, `module.json` unchanged
- `modules/formats`: `OcMat.hpp/.cpp` (present tracking, new records, instance writer), `OcGraph.hpp/.cpp` (`MaterialFunction` domain), `OcProject.hpp/.cpp` (`RENDER.MATERIALPERMUTATIONS`), `CMakeLists.txt`
- `modules/render.voxi/src/VoxiRenderer.cpp` + `include/aver/voxi/VoxiRenderer.hpp`: only `hashDrawMaterialInto`, `emissiveKeyTag`→`materialKeyTag`, `rtMaterialKey`, the accel-gate skip branch, `patchRtMovers`' `matChanged` block (factored into `refreshRtMaterialParams`). No other function.
- `modules/upgrade/src/Upgrade.cpp` (note only)
- `Runtime/include/aver/game/GameContent.hpp`, `Runtime/src/GameContent.cpp` (`materialForSurface`, `resolveMaterialGraph`, `authoredFor`, `releaseMaterialsExcept`, `releaseProjectMaterials`, `materialSourcePath`), `Runtime/src/GameApp.cpp` (permutation policy at startup, one line)
- `sandbox/src/`: `SandboxPanels.cpp` (`materialPanel`, `saveMaterialSource` untouched), `SandboxContentBrowser.cpp`, `SandboxApp.hpp` (declarations), `GraphEditor.cpp/.hpp`, `GraphNodeDefs.hpp` (include + new material rows), `GraphAssetPresentation.hpp`, `AnimEditor.cpp` (the material resolution function only, `:480-560`), `MaterialResolve.hpp`, `SandboxSelfTests.cpp`
- `scripting/csharp/Aver.Scripting/Pbr.cs`, `scripting/csharp/Aver.Materials/MaterialBuilder.cs` (+ `Material.cs` if `Slot` lives there), `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs` (two one-line calls: `Materials.Update()` in `DispTickAll` group 0 beside `UiSystem.Pump()`, `Materials.Shutdown()` in `DrainAndUnload` beside `Timers.Shutdown()`), `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs` (one `.Concat`), `Aver.Graph.Tests/Program.cs` (one line)
- `tests/formats/src/MaterialTest.cpp`, `tests/formats/CMakeLists.txt`, `tests/editor/CMakeLists.txt`

**Shared-file rules with the other wave-4 features**
- *Geometry (Trifactor, cluster draw/culling)*: also edits `VoxiRenderer.cpp`/`.hpp` and `SandboxApp` cluster code. This feature touches only the functions named above; geometry must not change `hashDrawMaterialInto` or `rtMaterialKey`. The cluster pixel shader `PSClusterMain` calls `averEvalMaterial` and needs no edit. `Draw::mat` grows by 64 bytes (cluster draw tables must not copy `Draw` by a fixed byte count).
- *Many-light (Voxi lighting, light table)*: shares `voxi.hlsl`/`voxi_rt.hlsli`; this feature edits neither (the ray twin picks up the new block through the existing typedef). `lightIntensity`/`MaterialFlag_Light` semantics are untouched; if the many-light work wants material bytes it must ask: **b2 has no room left** (2.2.1).
- *Texture streaming (assets, RHI residency, render.pbr bindings)*: owns `MaterialSystem.cpp` `writeSlots`/`resolveTexture`/`retainSlotTextures`/`releaseSlotTextures` and the `texIndex[]` meaning. This feature edits `MaterialSystem::update()`'s dirty branch (`:552-562`) to call `writeSlots` only for `kDirtySlots`; whoever merges second keeps both. `kTextureSlotCount` stays 8 (`MaterialGpu.hpp:234` asserts it); widening it is the streaming feature's decision and touches `MatField::Tex*`, `kSlotNames` (`MaterialGraphHlsl.cpp:595`), `texIndex` and the cbuffer, which would also need to find 64 bytes: coordinate through open question 2.
- Merge-adjacent, additive-line files (whoever merges second keeps both): `GraphNodeDefs.hpp` include list, `GraphGameSystemNodeTable.cs` `Table()`, `SandboxContentBrowser.cpp` type table/Add menu, `HostBridge.cs`, `FORMAT_SPECS.md`, `ABI.md`, `AVER_NODE_NODES.md`, `tests/formats/CMakeLists.txt`.

**Implementation order** (each step builds and its tests pass): 1 `MaterialConstants` to 256 with an empty parameter block + all mirrors + `MaterialTest`, gate baselines unchanged (pixel-identical stock path); 2 `MaterialFields.cpp`, `InstanceInfo`, library link/propagate/dirty flags/`drainDirty`, `MaterialDirtyTest`; 3 parameter nodes, layout, emitter, registry source retention and layouts, DXC D3D12 + SPIR-V tests; 4 `.ocmat` present tracking, new records, `OcMatInstance`, binder, `GameContent`/`AnimEditor`/preview unification; 5 static switches and permutations, batching, policy key; 6 Voxi key exclusion and `refreshRtMaterialParams` (its own commit; measured before and after); 7 material functions (domain, nodes, inliner, resolver, dependents); 8 dynamic ABI, C#, graph nodes, builder; 9 editor surfaces; 10 docs, `test-content/MaterialInstances`, headed Vulkan and D3D12 verification.

## 6. Performance budget and how to measure

| Item | Budget | Measured by |
|---|---|---|
| Stock material, no parameters (every existing project) | GPU frame time within noise (|delta| < 0.05 ms at 1440p) and CPU `submit`/key time within 0.1 ms on Jungle Ruins (12 494 entities) | `--gpu-timing` and `--frame-time` before/after step 1 and step 6, same level, same flags, **GPU idle first** (the owner's other GPU work skews it, project notes on perf traps), three runs each, headed. The key hashing covers `192 + 16*rows` bytes so a stock draw hashes what it did before; the CPU number proves it. |
| Ray table | +64 B per distinct material (about +38 KB at 600 materials); primary-hit ray-driven pass within 0.05 ms | `--gpu-timing` pass spans on Sponza in ray-driven mode |
| Parameter read | One `gMatParams[r].c` cbuffer read per parameter: unmeasurable per pixel; a 16-parameter graph vs the same graph with literals within 0.05 ms at 1440p full screen | `--gpu-timing` on the fixture sphere wall |
| Animated parameter on N draws | `MaterialSystem::update` for 2 000 animating instances <= 0.15 ms CPU; zero TLAS rebuilds and zero GI rebuilds attributable to the parameter (acceptance: the `[Voxi] GI rebuild gate rejected on` counts and the TLAS build count do not move over 600 frames) | `--material-report` per-frame update time + the existing gate log + a TLAS build counter if one is not already exposed (add `VoxiRenderer::debugCounters()` only if absent) |
| Permutations | Adding a permutation = one `case` arm: pipeline rebuild time on a 128-entry registry within 2x the 1-graph time; generated HLSL < 256 KiB | wall time around `createScenePipelines` (logged by the existing rebuild line) before/after a 16-permutation fixture |
| Level load | N materials with instances load within the old per-material cost + 0.05 ms per instance (one parse, one flatten, one `copyField` pass) | `[PBR] N material(s) built in X ms` log on the fixture with 500 instances |
| Memory | `Draw::mat` +64 B/draw (< 1 MB at 12 k draws) | arithmetic, reported |

## 7. Visual verification (headed, 1440p, with a ping at each stop per the remote-session rule)

`test-content/MaterialInstances` (a project folder following `test-content/MaterialGraph`): master `M_Master.ocmat` with `GRAPHREF G_Master.ocgraph` (a `Tint` vector, `Roughness` scalar, `Stripes` static switch, `Detail` texture parameter on `layer1basecolor`, a `MF_Triplanar.ocmatfn` call); instances `M_Red`, `M_RedStripes` (child of `M_Red` with `SWITCH Stripes 1`), `M_Wet` (overrides `PARAM roughnessFactor`); equivalent hand-built roots `R_Red`, `R_RedStripes`, `R_Wet` with the same values and no `PARENT`.
1. Row of spheres, instance vs its equivalent root, **pixel difference 0** (mean and max) on D3D12 raster and on ray-driven; same on Vulkan.
2. Edit `M_Red`'s tint in Details: `M_RedStripes` follows live; edit `M_RedStripes`' own override: `M_Red` does not.
3. Play script (`Aver.Framework.SampleActor`-style) sweeps `Roughness` per frame on 200 entities with `ForEntity`: no GI rebuild lines, no TLAS rebuilds, shot at two parameter values differ and match the equivalent roots.
4. `SetStaticSwitch` at runtime in editor mode (permutation compiles, last good shader shown meanwhile) and in a `preloaded` run (refused unless a `PERMUTATION` record prewarmed it).
5. Edit `MF_Triplanar` and save: both calling graphs and their instances update without restart; break the function: the caller reports it by name and keeps drawing stock.
6. Falsification, as `docs/MATERIALS.md` §9 does: delete one `SCALAR`/`SWITCH` line and the sphere must change; set `gi=1` on the tint, animate it, and the GI bounce on the floor must follow (and with `gi=0` it must not, which is the intended trade).
7. Water: convert `G_WaterRipple.ocgraph`'s baked ripple strength to a parameter and add an `M_LakeWater` instance of `M_SeaWater` (the two look different, one shader); the existing sea must be pixel-identical when nothing is overridden.

## 8. Risks and open questions for the owner

1. **b2 headroom is gone.** 256 of 256 bytes. The wave-3 landscape design stated "MaterialConstants does not grow" (`LANDSCAPE_LAYERS_RIVERS.md` section 0, item 2); that held for terrain, which this does not disturb, but no later feature can add a field. Alternative if you prefer to keep 64 bytes free: 8 parameter floats (240 bytes) now, or a parameter `StructuredBuffer` indexed by a per-material row (needs a new SRV in every material-shaded pipeline layout and Vulkan work; the generated code would change only inside `ParamRead`). Recommendation: take the 16 inline floats; the indirection is the escape hatch and the emitter has exactly one place to switch it.
2. **8 texture slots total** caps texture parameters (a detail layer, a mask and a stock set already use most of them). Widening needs the streaming feature's binding work and 32+ more constant bytes. Which feature should own the widening?
3. **No per-instance data for instanced/foliage draws.** Every distinct parameter set is its own library material, binding set and ray-table row. Fine up to a few thousand; a forest of 50 000 trees each with its own wetness would need UE-style per-instance custom data, which is not designed here. Decide whether that is wave 5+.
4. **Entities with `material == 0`** (mesh default) cannot be reached by `Entity.SetScalarParameter` from C# without a native lookup. Adding `aver_scene_mesh_default_material` or a framework provider fixes it; I left it out to avoid a scene/framework ABI change.
5. **A missing parent fails the instance** (fallback look + error) instead of degrading to stock. Deliberate; say if you want the other policy.
6. **Dead-branch errors.** A static-switch branch not selected is not emitted, so a compile error inside it surfaces only through the editor's "validate all branches" on save and the permutation test. A broken permutation requested at runtime in an `editor` run falls back like any broken graph.
7. **C#-defined instances are read-only in the Details panel**; only `.ocmat` instances are editable and saveable there (`Save to C#` for instances would need `MaterialScript.cpp`'s rewriter taught the instance chain; deferred). Binaries output wins over Content for the same stem (existing rule), which can hide an editor-made instance.
8. **Function resolution by stem** means two `.ocmatfn` with one stem in different folders is an error, and renaming a function file breaks callers unless the Content Browser rename repoints them (`cbRewriteReferences` is extended to `fn=`; a rename done outside the editor is not caught).
9. **Parameter semantics are by name, not by node id**: renaming a parameter in a graph orphans the values instances stored (warned once, kept, not migrated). Matches how paint layers are stored by name in `.ocland`; a rename-with-migrate command is a follow-up.
10. **`gi` defaults to off.** An author who animates an albedo-like parameter and forgets `gi=1` sees direct lighting follow and GI bounce lag. This is the cost of not rebuilding GI every frame; the Parameters panel flags `gi` per row and the tooltip says so.
11. **Shader compile cost grows with permutations**: every pipeline recompiles when a new permutation lands in an `editor` session. The caps (16 per graph, 128 total, 256 KiB) are guesses to validate with the 16-permutation measurement in section 6.
12. **Patent check.** Not requested for this feature and only cursory: one web search for patents on material-instance/parameter-override/permutation schemes returned none and only Epic documentation of the pattern (a parent material, overridden scalar/vector/texture parameters, static parameters compiled to shader permutations; its UE3-era documentation, `udn.epicgames.com/Three/MaterialInstanceConstant.html`). The ideas here (parameter override, parent chains, inlined subgraphs, switch-selected permutations) are the common material-graph baseline across engines and predate any plausible live claim, and no Epic code or structure is copied (names, formats and the packing scheme are Aver's). I did not search Google Patents/USPTO/Espacenet in depth, so this is **not a clearance**; if the owner wants one before release, the specific techniques to search are "static switch permutation selection at material compile" and "parameter layout packing into a per-material constant block".
