# Wave 4, feature #7: runtime texture mip streaming with a residency budget (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: material textures opened through `GameContent::resolveMaterialTexture` (BC7 DDC chains of at least 512 texels) get a resident mip window that a GPU feedback pass, a CPU planner and a worker thread move inside a VRAM budget. Not in scope: tiled/sparse virtual texturing, streaming of terrain atlases (`uploadAtlas`), decals, light shapes, UI/editor textures, non-BC7 textures, packaged-game cooking (section 8, question 2).

## 0. Decisions in one screen

1. **A streamed texture is an ordinary RHI texture whose top mips may be absent.** Its `TextureDesc` is the *resident window*: `width = fullW >> first`, `height = fullH >> first`, `mips = fullMips - first`. Shaders never learn the difference: LOD comes from the bound resource's own dimensions, so sampling a clipped chain at distance is identical to sampling the full chain at mip >= `first`. No shader code reads material-map dimensions (checked: the only `GetDimensions` on texture arrays is the decal path, `voxi_decal.hlsli:50`, not streamed).
2. **The `TextureHandle` never changes.** A mip change builds a new backing resource off-thread and swaps it in under the same handle (the precedent is `adoptExternalDepthTexture(..., existing)`, `D3D12Device.cpp:7400`, `VulkanResourceFactory.cpp:2014`). The RHI rewrites every descriptor that names the handle (binding sets and bindless tables, both backends), so MaterialSystem, Voxi, the path tracer and the ray path need no change to survive a swap.
3. **Feedback is software and portable**: the shading passes write, per texture id, the smallest UV footprint (UV units per pixel) any sampled pixel needed, with `InterlockedMin` on float bits into a 16 KB UAV buffer, read back with a 3-frame lag; the CPU converts footprint to a wanted first mip. No sampler feedback, no hardware residency map, no tiles, no sparse resources.
4. **Texture id = bindless index = `MaterialConstants::texIndex`**, now owned by `pbr::MaterialSystem` and valid on raster as well as the ray path. This is the only way the raster pixel shader can name the texture it is sampling without changing the `MaterialConstants` layout (which is mirrored in three places and will be touched by the material-instances feature). `texIndex` already exists in the b2 block (`material_prelude.hlsl:73-74`) and is unread by raster today.
5. **Partial reads from the existing DDC container**: a `.octex` ("OCTX" v1, `TextureCache.cpp:31`) stores mip 0 first and every level's size is a pure function of width/height, so mip `m`'s byte offset is computable. **No format change, no cache invalidation.**
6. **Uploads never drain the queue.** Bytes go worker -> persistently mapped upload ring -> `copyBufferToTexture` recorded into the frame's own command list in `VoxiRenderer::prePass`. (`uploadInitialData` waits on a fence, `D3D12Device.cpp:7106-7190`; the 10-10 stutter work found every such wait is a queue drain.) The initial tail seed in v1 still uses blocking `createTexture` (section 2.9, deferred seed is stage 5).
7. **Budget is VRAM-derived and global**: pool = fraction of `IDevice::videoMemory()` local budget minus non-texture usage, overridable. Over pool, a single global mip bias rises until the wanted set fits (deterministic, no per-texture priority tuning), then unseen textures are trimmed LRU.
8. **Without feedback the system degrades to static**: no feedback source (path tracer view, headless, Null/D3D11 device, previews) means every streamed texture wants mip 0 and the same global-bias fit applies. Previews open textures pinned.

## 1. What exists today, and what is missing

| Need | Exists (file:line) | Missing |
|---|---|---|
| Decode + BC7 + cache | `uploadTexture` (`modules/assets/src/TextureUpload.cpp:221`): hit -> `uploadCachedTexture` (:58) uploads the WHOLE chain via blocking `createTexture`; miss -> decode, `generateMipChain`, `uploadCompressed` (:94) writes the cache then uploads | Partial reads, any notion of a resident window |
| Cache container | `CachedTexture` (`TextureCache.hpp:42`), header 40 bytes (`TextureCache.cpp:35`: magic, version, format, w, h, mips, avg[3], flags), `bc7MipBytes` (:66), key = path + size + mtime + usage + encoder/container versions (`cacheKey`, :103) | Header-only stat, ranged read of mips `[first, N)`, public mip offset math |
| Eligibility | `textureCompressionEligible` (:162): multiple of 4 and max dim >= 256; `AVER_TEXTURE_COMPRESSION=0` switch (:147) | A streamable predicate (BC7, max dim >= 512) |
| Host resolve | `GameContent::resolveMaterialTexture` (`Runtime/src/GameContent.cpp:1437-1478`) picks usage from the slot, calls `uploadTexture`, returns `{handle, averageLinear, bytes}`; installed at `Runtime/src/GameApp.cpp:744`, `sandbox/src/SandboxApp.cpp:1143`, previews at `:1146` | A streamed open, a pinned variant for previews |
| Material residency | `MaterialSystem` caches by reference + colour class (`cache_`, `MaterialSystem.cpp:161-199`), refcounts handles (`textureRefs_`), destroys at zero (`releaseSlotTextures`), exposes effective handles per set (`textures(set)`, `MaterialSystem.hpp:94`) | Stable texture ids, `forget` notification, ready check |
| Raster sampling | 8 `Texture2D` per draw table + one sampler (`material_prelude.hlsl:6-17`); `averSampleMaps` (:342), `averBlendLayers` (:398), `averSampleSlot` (:438); aniso 8 (`VoxiRenderer.cpp:135`) | Footprint recording |
| Ray sampling | `gRtTextures[]` bindless (`voxi_rt.hlsli:63`); one choke point `averRtSampleSlot(mat, slot, uv, gx, gy, fb)` (:98) carrying the UV footprint; primary hit surface built at `voxi.hlsl:2099` | Footprint recording, primary-only flag |
| Bindless index | `VoxiRenderer::residentTexture` (`VoxiRenderer.cpp:3544`): append-only `rtTexIndex_`, capacity `kRtTextureCapacity = 4096` (`VoxiRenderer.hpp:505`); `rtMaterialKey` fills `mc.texIndex` from it (:3683) | Ids owned by MaterialSystem |
| Voxi shared table | `giTableKinds`/`giLayout` (`VoxiRenderer.cpp:195-230`): `kVoxiSrvCount == 25`, `kVoxiUavCount == 25`, u24 = froxel volume, `static_assert` ties `kVolumeUavSlot == kVoxiUavCount - 1` | A feedback UAV slot |
| D3D12 descriptors | Binding sets: CPU staging range + one shader-visible range per frame in flight, copied when the set's version moved (`noteBindingSetWritten`, :6783; `setSrv`, :8531) - versioned since ee333dc1. **Bindless tables are NOT versioned**: `setBindlessTexture` (:7858) writes the single shader-visible range in place (safe only because entries are append-only). `kFrameCount = 2` (:59), heap 65536 (:1928) | Reverse index texture -> (set, slot); versioned bindless table |
| Vulkan descriptors | Binding sets ringed with replay (`RhiBindingSet`, `VulkanCommon.hpp`), each slot remembers its `image_`; bindless table ringed with per-frame pending lists, `forgetBindlessTexture` (`VulkanResourceFactory.cpp:3733`), `bindlessSetForFrame` (:3745) | Rewrite-by-image on swap (reuse the scrub pattern) |
| Off-thread GPU creation | `IBufferStager`/`adoptBuffer` (`RHIResources.hpp:429-442`; `D3D12Device.cpp:7487-7539`; `VulkanResourceFactory.cpp:2126-2154`); `IMeshStager` (`RHI.hpp`) | A texture stager; `BufferStaging` exposes no mapped pointer |
| Copies | `IRenderContext::copyBufferToTexture(dst, mip, src, srcOffset)`, `textureCopyFootprint` (`RHIResources.hpp:533, 611`; D3D12 `D3D12Device.cpp:9268, 9318`; Vulkan `VulkanRenderContext.cpp:792`); `copyTexture` whole-resource, identical mip counts only (:9343) | Per-mip texture-to-texture copy |
| VRAM | `IDevice::videoMemory()` -> `VideoMemoryInfo` (`RHI.hpp:492`, both backends); `VoxiRenderer::reportVramUsage` (:859), `vramTight()` (:1412); status bar (`SandboxShell.cpp:2609`); `docs/rendering/VRAM.md` | A texture category in the report |
| Hook point | `VoxiRenderer::prePass(ctx)` (:1177) calls `materials_.update()` (:1220) before any scene target is bound | Streamer call and feedback copy-out |
| File API | `readFileBytes` only (`FileSystem.hpp:54`), traced by `setFileTrace` | `readFileRange` |

Not found anywhere: any texture residency tracking, a worker that reads textures, a GPU feedback buffer, a texture memory budget.

## 2. Design

### 2.1 Alternatives rejected (so the implementer does not revisit them)

- **Tiled/reserved resources (D3D12) and sparse images (Vulkan).** Would keep handles and descriptors fixed, but `sparseResidencyImage2D` is optional and patchy on Vulkan, unmapped-tile reads return zero (a black flash with no way to clamp per texture without a per-sample LOD clamp in every shader), and the 64 KB tile is coarser than the mips that matter. Not portable; parity rule says no.
- **Sampler feedback (D3D12 only)**: not portable, and patented in hardware form (section 7).
- **Per-material feedback rows**: needs a per-draw material row in the raster draw block and a frame-stable row-to-texture snapshot ring. Per-texture ids avoid both.
- **CPU analytic estimate only (distance times mesh texel density)**: no occlusion, wrong for world-UV and layer-1 scales, and a per-draw loop. Kept only as a concept; not built.
- **Swapping by new bindless index per version**: would put the lifetime problem in Voxi, the path tracer and every future table. The RHI owns descriptors, so the RHI rewrites them.

### 2.2 Data model

`modules/assets` (new files, section 5):

```cpp
// aver/assets/TextureStreaming.hpp
struct StreamedTexture {            // CPU record, render-thread owned, one per streamed handle
    rhi::TextureHandle handle;
    std::string path; TextureUsage usage; rhi::Format format;
    u32 fullW, fullH, fullMips;     // the logical chain
    u32 firstMip;                   // resident window [firstMip, fullMips)
    u32 tailFirst;                  // lowest-detail mip allowed to be the top: never evicted below this
    u32 allowedMask;                // bit m: mip m may be the top (>=4x4, extents multiple of 4)
    u32 feedbackId;                 // MaterialSystem texture id, ~0u if none
    u32 wantFirst;                  // latest wanted top mip
    u64 lastSeenFrame, lastChangeFrame;
    u8  state;                      // Idle, Loading, Ready(adopt pending), Pinned
    u32 pendingFirst; u32 generation;   // request in flight and its generation
    CachedTextureInfo src;          // header + mip offsets of the DDC file, valid if src.ok
};
```

- **Window byte size**: `windowBytes(w, h, first, N) = sum_{m=first}^{N-1} ceil(max(w>>m,1)/4) * ceil(max(h>>m,1)/4) * 16` (the same arithmetic as `bc7MipBytes`, `TextureCache.cpp:66`; export it).
- **Allowed top mips.** Mip `m` may be the top only if `(w>>m) % 4 == 0 && (h>>m) % 4 == 0 && both >= 4` (the BC7 `createTexture` rule, `D3D12Device.cpp:7317`). Powers of two always qualify down to 4x4; a 1500x1500 texture has no legal top above mip 0 and is pinned. Requests snap to the nearest allowed mip at or below (lower index = more detail) the wanted one.
- **Tail floor**: `tailFirst` = smallest `m` with `max(w>>m, h>>m) <= tex.tailDim` (default 64, 5.4 KB per chain). A texture is never below `tailFirst`.
- **Initial window**: `max(tailFirst, smallest m with max dim <= tex.initialDim)` (default 256, about 87 KB per chain). A cache miss falls back to today's full upload; the streamer then registers the full chain as `firstMip = 0` and the planner trims it (trimming needs no source: it is a GPU-side mip copy).
- **Pinned** (never changes, still counted in the pool): not streamable (RGBA8, under 512, no legal top above mip 0), DDC header unreadable or mismatched with the live texture, source `mtime` changed since registration, or opened by a pinned resolver (previews).

### 2.3 RHI contract (modules/rhi, rhi.d3d12, rhi.vulkan)

New header `modules/rhi/include/aver/rhi/TextureStreaming.hpp`. `RHIResources.hpp` gains only the additive virtuals below, appended at the end of the two classes. Every default is a no-op/false/null, so D3D11, Null and test mocks compile unchanged and report "streaming unavailable".

```cpp
namespace aver::rhi {
struct TextureBacking { virtual ~TextureBacking() = default; };   // an unseeded, copy-ready resource made off-thread

struct TextureCopyLayout {          // thread-safe, from a TextureDesc alone (no resource needed)
    u64 offset;                     // within the upload buffer for this mip, multiple of offsetAlign
    u32 rowPitch, rowBytes, rows;   // D3D12: rowPitch = align(rowBytes, 256). Vulkan: rowPitch = rowBytes.
    u64 bytes;
};
class ITextureStager {              // any thread, like IBufferStager; keeps what it needs of its device alive
public:
    virtual ~ITextureStager() = default;
    virtual std::unique_ptr<TextureBacking> stage(const TextureDesc& residentDesc) = 0;   // null on failure/OOM
    virtual u32  offsetAlign() const = 0;                                                  // 512 on both backends
    virtual bool layout(const TextureDesc& residentDesc, u32 mip, u64 baseOffset, TextureCopyLayout& out) const = 0;
};
struct TextureMipKeep   { u32 srcFirst = 0, dstFirst = 0, count = 0; };   // current backing's mip srcFirst.. -> new backing's dstFirst..
struct TextureMipUpload { u32 mip = 0; u64 srcOffset = 0; };              // `mip` indexes the NEW backing
class ITextureStreamer;             // section 2.6
}
```

`IResourceFactory` additions: `std::shared_ptr<ITextureStager> textureStager()`; `u8* bufferMapped(BufferHandle)` (the persistent pointer of an `Upload` buffer, stable for its life, null otherwise; the worker writes through it, never through `writeBuffer`, which reads the factory's vectors from the wrong thread); `bool texturesStreamable() const`.

`IRenderContext` addition (one call encapsulates every backend hazard):

```cpp
// Replaces the backing of `h` with `backing` (made by this device's stager) and rewrites every descriptor naming `h`.
// Records, in order: new backing -> CopyDest; copy `keep` mips from the current backing; copyBufferToTexture per upload from `upload`;
// new backing -> ShaderResource; swap; retire the old backing behind the frame fence. False (nothing changed) on any mismatch.
virtual bool streamTextureMips(TextureHandle h, std::unique_ptr<TextureBacking> backing, const TextureMipKeep& keep,
                               BufferHandle upload, const TextureMipUpload* uploads, u32 uploadCount) { return false; }
```

Call it only from `prePass`, before any pass binds a material set this frame (Vulkan's "nothing writes a binding set after binding it in the same frame" invariant, `RhiBindingSet` comment).

**Swap semantics (both backends).**
- `RhiTexture::desc` becomes the new resident desc (`width`, `height`, `mips`); `textureInfo(h)` therefore reports the window. `textureCopyFootprint(h, mip)` indexes the window. Tracked state (`AVER_RHI_TRACK_STATE`) is reset to `ShaderResource` for every mip.
- Per-mip views of a streamed texture (`setSrv(..., mip != kAllMips)`, `setUav`) are refused with an error naming the texture; materials only use whole-chain SRVs.
- Streamed textures have no UAV/RTV/DSV. If `uiSrvCpu` is set (D3D12 `destroyTexture` frees it, :8434), the swap re-creates it through `uiBackend_`; the implementer greps for ImGui users of material textures and asserts none today.

**D3D12.**
1. `D3D12TextureStager` mirrors `D3D12BufferStager` (:7487): `CreateCommittedResource` on the worker, initial state `COPY_DEST`, unseeded; `layout` uses `GetCopyableFootprints` on a desc (no resource). The upload ring is an ordinary `BufferKind::Upload` buffer (created once through `createBuffer`, mapped for life).
2. `streamTextureMips`: old backing all subresources -> `COPY_SOURCE`; `CopyTextureRegion` per kept mip (subresource-index to subresource-index) and per upload (placed footprint at `srcOffset`, 512-aligned); new -> `ALL_SHADER_RESOURCE`. The old resource is not restored (it is retired); the previous frame's list executes before this one on the single queue, so the barrier is ordered.
3. **Binding sets**: add `TextureHandle srvTex[kMaxBindingSlots]` to `RhiBindingSet`, written by `setSrv` and cleared by `clearSrv`. After a batch of swaps, one pass over alive sets: where `srvTex[slot]` is a swapped handle, call the same `CreateShaderResourceView` code as `setSrv` into the staging range and `noteBindingSetWritten` (which already clears the `fovSet_`/`dbSet_`/`dbValid_` caches). The per-frame ranges refresh on next bind, which is why an in-flight frame is unaffected. Cost: O(alive sets x 8) per frame that had a swap.
4. **Bindless table must be versioned** (it is not today). `RhiBindlessTable` gains `stageBase` (CPU-only staging range of `capacity`), `gpuBase[kFrameCount]`, `version`, `gpuVersion[kFrameCount]`, `std::vector<TextureHandle> textures`. `setBindlessTexture` writes staging, bumps `version`, and also writes the current frame's range directly (so a texture added mid-frame is visible to later draws in the same frame exactly as today); `setBindlessTable` copies staging -> this frame's range with one `CopyDescriptorsSimple(capacity)` when `gpuVersion[fi] != version`. Heap cost: +4096 shader-visible and +4096 staging descriptors (6% each of 65536). A swap re-issues the SRV for every index with `textures[i] == h`.

**Vulkan.**
1. `VulkanTextureStager` mirrors `VulkanBufferStager` (:2126): `createImageCommitted` under `StagerState::m`, usage `SAMPLED | TRANSFER_DST | TRANSFER_SRC`, plus the SRV view (created on the worker). `layout`: `bufferRowLength = 0`, tight pitch.
2. `streamTextureMips`: `suspendRenderScope(cb)` (as `copyBufferToTexture`, :792); old -> `TRANSFER_SRC`, new `UNDEFINED -> TRANSFER_DST`; `CmdCopyImage` per kept mip, `CmdCopyBufferToImage` per upload; new -> `SHADER_READ_ONLY`; the old image, memory and view are destroyed through `retire(...)`.
3. **Binding sets**: scan `srvSlots[].image_ == oldImage` with `ownedView == null` and `writeBindingSlot` the new whole-chain view (same code path as `scrubBindingSets`).
4. **Bindless table**: `for i: t.textures[i] == h -> t.views[i] = newView; pending[f].push_back(i)` for every frame slot (the `forgetBindlessTexture` loop, :3733). Already ringed; no structural change.

**Vulkan gaps that matter here**: the raster feedback is a fragment-stage UAV write plus `InterlockedMin`, which needs `fragmentStoresAndAtomics`. The device enables every feature `GetPhysicalDeviceFeatures2` reports (`VulkanDevice.cpp:705-748`), so it is on wherever it is supported. If a device lacks it, `DeviceCaps` reports `fragmentUav = false`, Voxi leaves `AVER_TEX_FEEDBACK` undefined, and streaming runs in the no-feedback static mode (section 0.8). Nothing here needs SM 6.6, wave intrinsics, mesh shaders or sparse images; the mesh-shader variants Vulkan refuses are unrelated. Vulkan still has no BLAS compaction (`VRAM.md`), so on Vulkan the texture pool sits in a tighter budget; the pool formula (2.5) reads live usage, so it adapts.

### 2.4 GPU feedback

**Buffer.** `RWStructuredBuffer<uint> gTexFeedback` (4096 entries = `kRtTextureCapacity`, 16 KB) at the next free Voxi table-0 UAV slot (`kTexFeedbackUavSlot`; today u25, appended after u24 exactly as the froxel volume was: `uav[...] = StructuredBuffer`, `kVoxiUavCount` widened, the `static_assert` rewritten to name both last slots). Every binding set built from `giTableKinds` already null-fills it; Voxi binds the real buffer in each set that binds `VoxiFroxelFog::kVolumeUavSlot` (grep that symbol: that list IS the list). Value: float bits of the smallest `fp` seen; `0xFFFFFFFF` = unseen.

**Per frame, in `VoxiRenderer::prePass` after `materials_.update()` (:1220), before `streamer_->recordFrame`:**

| Step | Resources | Barriers |
|---|---|---|
| 1. copy-out of the frame that just finished | `fbBuf -> fbReadback[(n) % 4]` (`BufferKind::Readback`, 16 KB each) | `fbBuf` UAV -> CopySource -> (next step) CopyDest |
| 2. clear | `copyBuffer(fbBuf, fbFF, 16384)` where `fbFF` is a 16 KB `Upload` buffer filled with `0xFFFFFFFF` once | CopyDest -> UnorderedAccess |
| 3. consume | `readBuffer(fbReadback[(n - 3) % 4])` (>= kFrameCount + 1 frames old, so its fence has passed; the same window PcgVolume uses) -> `streamer_->onFeedback(bits, 4096, frameId)` | none |

Writes happen in whichever pass shades primary visibility this frame; a frame with no write leaves all `0xFFFFFFFF` and the streamer treats it as "nothing seen" (it does not distinguish that from "no feedback source"; Voxi calls `streamer_->setFeedbackAvailable(true)` only on frames that actually ran a feedback-writing pass).

**Shader** (`modules/render.pbr/shaders/material_prelude.hlsl`, compiled in only with `#ifdef AVER_TEX_FEEDBACK`, which Voxi defines when the UAV and device caps exist):

```hlsl
#ifdef AVER_TEX_FEEDBACK
RWStructuredBuffer<uint> gTexFeedback : register(AVER_TEX_FEEDBACK_REG);
static bool  gAverFbOn  = false;          // set true only around the PRIMARY surface of a pass
static uint2 gAverFbPix = uint2(0, 0);    // SV_Position.xy of that pixel
// gx, gy: UV change per pixel along screen x / y. ID is MaterialConstants::texIndex[slot] (0xFFFFFFFF = unbound).
void averFbRecord(uint id, float2 gx, float2 gy) {
    if (!gAverFbOn || id >= AVER_TEX_FEEDBACK_COUNT) return;
    const float a = length(gx), b = length(gy);
    const float fp = max(min(a, b), max(a, b) * AVER_TEX_FEEDBACK_INV_ANISO);   // INV_ANISO = 1/8 = 1 / maxAnisotropy (VoxiRenderer.cpp:135)
    if (!(fp > 1e-8) || !isfinite(fp)) return;
    const uint bits = asuint(fp);
    if (bits < gTexFeedback[id]) InterlockedMin(gTexFeedback[id], bits);          // racy read first: after warm-up the atomic is rare
}
#endif
```

- **Derivatives are computed unconditionally** in the caller (`ddx/ddy` of the sample UV) before the sampling-mask branch, never inside it.
- **Pixel decimation**: record only when `((gAverFbPix.x + 3 * gAverFbPix.y + frameLo) & 15) == 0` (`frameLo` from the existing frame counter in `VoxiFrame`; the implementer picks the field). 1 pixel in 16, rotating, so every pixel is eventually covered and the atomic traffic is about 130 k per frame at 1080p x at most 5 slots.
- **Raster** (`averSampleMaps`, :342): for slots 0..4 whose `AVER_MAT_*_MAP` flag bit is set and `gTexIndex*` is bound, `averFbRecord(id, ddx(uv), ddy(uv))`. `averBlendLayers` (:398): slots 5..7 with `uv1 = uv * gL1UvScale`, so the derivatives are those of `uv1`. `averSampleSlot` (:438): the generated graph path, `uv` is the argument. The pixel shader entry sets `gAverFbOn = true; gAverFbPix = uint2(i.pos.xy)` for opaque and cutout draws; blended draws leave it false (a translucent surface does not decide residency).
- **Ray-driven** (`averRtSampleSlot`, `voxi_rt.hlsli:98`): `averFbRecord(idx, gx, gy)` before the sample. The primary-hit callers set the statics: around `rtHitSurface(hit, ...)` at `voxi.hlsl:2099` (and its `AVER_RD_SPLIT` twin) set `gAverFbOn = true` with the pixel; every other `rtHitSurface` caller (`voxi.hlsl:881, 1027`, `voxi_pt.hlsli:122`, `voxi_restir.hlsli:350`, `voxi_rt.hlsli:1583`) leaves it false, so reflection/GI/PT hits never raise residency.
- **Terrain atlases and decals**: not recorded (not streamed).
- **Why a min of footprint, not a mip**: the shader does not know the full-resolution texture size once the bound chain is clipped; footprint in UV units is independent of it, and the CPU (which knows `fullW/fullH`) converts. It also keeps the buffer meaning stable when two textures of different sizes share a UV set.

**CPU conversion** (`assets::wantedFirstMip`): `lod = log2(fp * max(fullW, fullH))`, `first = clamp(floor(lod) + tex.biasMips, 0, fullMips - 1)` (floor, because trilinear reads `floor` and `floor+1`), then snap to an allowed mip. `tex.biasMips` default 0; `-1` buys sharper textures at 4x the memory.

**PrimaryVisibility cannot be reused**: that buffer exists only for NeuRAA capture (`RHIResources.hpp:677`).

### 2.5 The streamer (`modules/assets`, CPU)

```cpp
class TextureStreamer final : public rhi::ITextureStreamer {
public:
    struct Settings { bool enabled=true; u32 poolMiB=0; /*0 = auto*/ f32 poolFraction=0.25f; u32 reserveMiB=1024;
                      u32 uploadMiBPerFrame=8; u32 maxSwapsPerFrame=4; u32 tailDim=64, initialDim=256;
                      i32 biasMips=0; f32 holdSeconds=10; u32 workers=1; u32 ringMiB=64; };
    bool init(rhi::IDevice&, rhi::IResourceFactory&, const Settings&);      // false -> caller keeps today's path
    void shutdown();
    // Replaces uploadTexture for a material texture. Returns the handle and the same info uploadTexture gives.
    // `pinned` = never changes (previews, non-feedback hosts). Falls back to uploadTexture on any problem.
    rhi::TextureHandle open(const std::string& path, TextureUsage usage, bool pinned, std::string* err, TextureUploadInfo* info);
    // ITextureStreamer
    void bindFeedbackId(rhi::TextureHandle, u32 id) override;               // MaterialSystem
    void forget(rhi::TextureHandle) override;                               // before destroyTexture
    bool isStreamed(rhi::TextureHandle) const override;
    void setFeedbackAvailable(bool) override;                               // per frame, by the pass owner
    void onFeedback(const u32* minFootprintBits, u32 count, u64 frame) override;
    void recordFrame(rhi::IRenderContext&, u64 frame) override;             // render thread, prePass
    Stats stats() const;                                                     // resident/pool/pending/swaps/popins
};
```

All planner state is render-thread owned; the only cross-thread objects are the request queue, the completed queue and the ring accounting, each behind one plain `std::mutex` (plain mutexes, not a lock-free queue: see section 7).

**`open`.** Eligible and cache hit (`statCachedTexture` returns the header and offsets): create the texture with the initial window via blocking `createTexture` (tail seed; reads only `[initialFirst, N)` of the file), register the record, return. Cache miss, ineligible, or `!enabled`: `uploadTexture` as today; if the result is BC7 with >= 512 and legal tops, register it as `firstMip = 0` and let the planner trim. `info->bytes` reports the resident bytes (what `MaterialSystem` logs and sums), `info->width/height/mips` the FULL chain.

**Planner** (`recordFrame`, render thread, about O(streamed textures), budgeted at 0.2 ms for 4096):
1. `want[t]`: from this frame's feedback if seen, held for the last 8 frames (min over the window, so a one-frame occlusion does not downgrade); unseen: keep the current window until `holdSeconds`, then `tailFirst`'s initial window. No feedback source: `want = 0` for all.
2. **Pool**: `pool = poolMiB ? poolMiB : clamp(poolFraction * localBudget, 256 MiB, localBudget - nonTextureUsage - reserveMiB)` where `nonTextureUsage = localUsage - residentTextureBytes` (all from `videoMemory()` sampled every 0.5 s). Pressure: `localUsage > 0.92 * localBudget` multiplies the pool by 0.85 per second down to a floor of the tail sum plus 128 MiB, and recovers 2% per second after usage drops below 0.85. `supported == false` (D3D11, Null): the pool is `poolMiB` or 1 GiB.
3. **Fit**: smallest global `b >= 0` with `sum_t windowBytes(snap(want[t] + b)) + tail sum of unseen-trimmed <= pool`. Textures untouched by the bias keep `want`. Then spend leftover pool on unseen resident textures in LRU order (most recently seen first), up to their current window. Everything not funded is trimmed toward its initial window.
4. **Hysteresis**: an upgrade is requested at once; a downgrade only if the target has been lower for >= 30 frames or the pool is over (pressure overrides).
5. **Requests**: textures whose target differs from `firstMip`, ordered by (resident detail deficit = `firstMip - target` descending, then `lastSeenFrame`), capped at `uploadMiBPerFrame` of new bytes per frame and `maxSwapsPerFrame` swaps. Trims (target > `firstMip`) are GPU-only (`keep` = the surviving mips, zero uploads) and are issued first when over pool.
6. **Worker** (per request): read mips `[target, min(firstMip, N))` with `readCachedMips` (one ranged read, `[offset(target), offset(firstMip))`), `stage(desc)` the new backing, reserve ring space (512-aligned, row-padded to `layout().rowPitch`) and copy the rows in. Push to the completed queue with the generation. Failure of any step (file changed, OOM, ring full) -> the texture is marked Pinned or retried after 2 s (OOM: back off like `giAccumRecreateBackoffTicks_`).
7. **Adopt** (`recordFrame`, at most `maxSwapsPerFrame`): drop results whose generation is stale; otherwise `ctx.streamTextureMips(h, backing, keep, ringBuffer, uploads)`. The record's `firstMip` changes only when it returns true. Ring space is released when `frame + kFrameCount + 1` retires.
8. **Counted pop-ins** (`stats().popIns`): a swap where the new top mip is >= 2 levels finer than before while the texture is seen; the metric the visual test watches.

Memory accounting is exact (sum of `windowBytes` + a fixed 64 KB per texture allowance); `reportVramUsage` (:859) gains `textures {resident} / {pool}` in its category line and a new `[Streaming] textures: ...` line that prints on change, like the other VRAM lines.

### 2.6 Integration

- **`rhi::ITextureStreamer`** (interface only, in `TextureStreaming.hpp`) is what `render.pbr` and `render.voxi` see; neither depends on `Aver.Assets` (module.json: Voxi depends on Core, Formats, RHI, Denoise, PBR). `Runtime`/`sandbox` compose: `GameContent` owns the `assets::TextureStreamer` and calls `materials().setTextureStreamer(&streamer)` and `voxi.setTextureStreamer(&streamer)`.
- **`GameContent::resolveMaterialTexture`**: calls `streamer_.open(path, usage, /*pinned=*/false, ...)` when `streamer_.ready()`; otherwise `uploadTexture`. A second static `resolveMaterialTexturePinned` (same body, `pinned = true`) is what `editor::setPreviewTextureResolver` receives (`SandboxApp.cpp:1146`), because previews have no feedback pass. Both are `MaterialSystem::TextureResolver` function pointers; no resolver signature change.
- **`MaterialSystem`**: new `setTextureStreamer`; `textureId(handle)` allocates ids `[0, kMaterialTexIdCap = 3072)` lazily for every effective handle that is not a fallback, with a free list released two frames after `releaseSlotTextures` destroys the handle; `writeSlots` stores `constants.texIndex[slot] = id` (or `kUnboundTexture`) in the Entry that `constants(h)` returns, so raster b2 carries the ids; calls `streamer->bindFeedbackId(handle, id)` at assignment and `streamer->forget(handle)` immediately before `res_->destroyTexture`. `ownsBindingSet`/hash users see stable values (ids do not change on swaps). More than 3072 distinct material textures logs once and leaves the rest unbound on the ray path, as the table-full path does today (:3547).
- **`VoxiRenderer::residentTexture`** (:3544): if `materials_.textureId(h)` is bound, use that index (write `setBindlessTexture(rtTexTable_, id, h)` once, memoised) instead of `rtTexNext_`; other textures (decals, light shapes) allocate from `rtTexNext_` starting at `kMaterialTexIdCap`. `rtMaterialKey` (:3683) is unchanged.
- **Path tracer**: untouched. Its table (`PathTracer::residentTexture`, `PathTracer.cpp:257`) is an RHI bindless table and is rewritten by the RHI on swap. While a PT view is the primary renderer there is no feedback, so the streamer is in static mode (section 0.8).
- **Host frame**: nothing outside `prePass`. `Runtime/src/GameApp.cpp` and `sandbox/src/SandboxApp.cpp` (the documented duplication, `docs/RUNTIME-DEDUP.md`) construct, configure and shut down the streamer identically, before the first material resolves (beside `setTextureCacheDir`, `GameApp.cpp:1136`, `SandboxProject.cpp:162`).
- **Platform**: `bool readFileRange(const std::string& path, u64 offset, u64 size, std::vector<u8>& out)` in `FileSystem.hpp`, tracing like `readFileBytes` (`setFileTrace`), so `verify-game.ps1` still sees every open.

### 2.7 Settings, console, project keys, ladder

- **Project keys** (`OcProject.hpp/.cpp`, listed with `RENDER.TAA`, :94, :145, :313): `RENDER.TEXSTREAM` (-1 default/0/1), `RENDER.TEXPOOLMIB` (-1 auto, else MiB). Round-trip like the others (`appendKey`, `OcProject.cpp:368`).
- **Console** (`sandbox/src/EditorConsole.hpp`, the `voxi.rtCompactBlas` pattern, :780): `tex.stream` (bool), `tex.poolMiB`, `tex.bias`, `tex.uploadMiB`, `tex.feedback` (bool, false = static mode; the A/B for the shader cost), `tex.dump` (prints the 50 largest records), `tex.pinAll`. CLI: `--no-tex-stream`.
- **Ladder** (`applyManifestKnobs`, `ProjectRenderApply.hpp:87`): per tier pool and bias: Low 512 MiB / +1, Medium 1024 / 0, High 2048 / 0, Epic auto (25% of budget, cap 4096) / 0. `RENDER.TEXPOOLMIB` wins. `AVER_TEXTURE_COMPRESSION=0` disables streaming (no cache, nothing to read).
- **Defaults**: enabled in the editor and the runtime when the device reports `texturesStreamable()` and BC7 compression is on.

### 2.8 Editor surface

- Status bar (`SandboxShell.cpp:2609`): beside VRAM, `Tex 1.8/2.0 GiB, 12 loading`.
- A small "Texture Streaming" panel (`sandbox/src/TextureStreamPanel.cpp`, registered where the other tool panels are, `uiReg_.track("panel.texStream")`): the `Stats` plus a sortable table (name, full size, wanted mip, resident mip, bytes, last seen, state). Pin/unpin a row by double-click.
- No viewport heat-map in v1 (it needs a per-pixel output).

### 2.9 Known limits of v1 (deliberate)

- **Initial seed is blocking.** One fence wait per new texture, but of about 87 KB instead of megabytes. Stage 5 (deferred seed) makes the first upload ring-based and has `MaterialSystem` bind the slot's identity texture until `streamer->isReady(h)` (the same retry/touch machinery `retryFailed` uses). Gate for stage 5: AVER_HITCH_MS=100 on a streamed-in Caldera cell shows seed stalls above 20 ms.
- **First load of a never-cached texture** still decodes and encodes on the main thread (today's behaviour).
- **Pops are not hidden.** A swap is a hard change of the top mip. No residency-dependent LOD bias and no filter-shape change are used (section 7).
- **Terrain atlases** (up to 160 MiB RGBA8 each, `GameContent.cpp:1517`) stay fully resident and are counted in `residentTextureBytes` as pinned.

## 3. Graph nodes

None. Streaming is internal; the console variables are the user surface. (A `Texture Streaming Pool` node would be a settings node, and the settings-node family is not part of this wave.)

## 4. Tests

| Test | Where | Asserts |
|---|---|---|
| `TextureStreamMathTest` | new `tests/assets/` (CMake: add to root beside `tests/rhi`, :445; no GPU) | `windowBytes` against a hand sum for 4096/1500/256 sizes; allowed-mip mask (powers of two to 4x4, 1500 has none above 0); `wantedFirstMip` against literal footprints (fp * dim = 1 -> mip 0; 8 -> 3; 7.9 -> 2; bias +/-1; clamps); `.octex` mip offsets equal the file layout `saveCachedTexture` writes (write a real entry, read each mip by `readCachedMips`, compare byte-for-byte with `loadCachedTexture`); header mismatch and truncated file -> not ok. |
| `TexturePlannerTest` | same | With a fake device and fake feedback: fit chooses the smallest bias for a pool (literal numbers); unseen textures trim LRU; upgrades immediate, downgrades after 30 frames; pressure shrink/recover; no-feedback mode wants mip 0 under a global bias; generation drops stale results; ring accounting never reuses a region before `frame + 3`. |
| `TextureStreamRhiTest` | `tests/rhi/` (real backend, WARP, SKIPPED if none, `LineMeshTest` shape; runs on D3D12 and Vulkan) | Create a 512x512 BC7 texture with a 128 window, bind it in a set and a bindless table, `streamTextureMips` to a 256 window with `keep` + one upload, then a compute pass reads `gTexture.Load` and `SampleLevel(..., 0)` through BOTH the set and the table and writes a buffer: the mip 0 texels equal the uploaded bytes' decode, the kept mips equal the previous ones, `textureInfo(h)` reports 256x256x(mips). Handle value unchanged. Trim back to 128. Destroy during a pending swap: no crash. Per-mip `setSrv` refused. |
| `TextureFeedbackShaderTest` | `tests/render.voxi/` | Compile `material_prelude.hlsl` with `AVER_TEX_FEEDBACK` for D3D12 (DXC) and SPIR-V; dispatch a fullscreen draw over a plane with known UV scale; read the buffer: the recorded `fp` equals the analytic footprint within 3%; unbound id and `gAverFbOn = false` write nothing; the buffer is `0xFFFFFFFF` after the clear copy. |
| `MaterialTextureIdTest` | `tests/render.pbr/` | Ids unique and stable across dirty re-uploads; released two frames after destroy; `constants(h).texIndex` equals `textureId` for bound slots and `kUnboundTexture` for fallbacks; `bindFeedbackId`/`forget` called in order (fake streamer). |
| `FileRangeTest` | `tests/platform/` | `readFileRange` boundaries, past-EOF false, trace callback fires. |
| Existing | `tests/render.voxi`, `tests/save` etc. | `gpuMaterialIndex`/RT material table tests must still pass with ids from MaterialSystem; the module-guard audit (`scripts/module-guard-audit.py`) and `module-matrix.ps1` stay green. |

**Visual and timing verification the implementer must run and report (not claim)**, headed, 1440p, owner's `editor.ini` rule applies:
1. **Equivalence**: Sponza and Caldera, fixed camera, 300 frames settled, `tex.stream 0` vs default: mean absolute difference <= 1.0 / 255 over the frame and no pixel region (32x32) above 6 / 255 (`aver_inspect_image`). Repeat in raster mode and in ray-driven mode, on D3D12 and Vulkan.
2. **Memory**: resident texture MiB (the `[Streaming]` line) vs the sum of the existing `[Material] ... KB` lines on Caldera; expected a large reduction at the default pool; report both and the VRAM-by-category totals.
3. **Motion**: `--cam-fly` across Caldera with `AVER_HITCH_MS=100`: no frame above 100 ms attributable to streaming; `stats().popIns` per second; screenshots at +0.5 s, +2 s after a teleport to show the converge.
4. **Budget**: `tex.poolMiB 256` forces trimming; image stays plausible (global blur, no black), `tex.poolMiB 4096` restores.
5. **Failure modes**: delete a DDC `.octex` mid-run (pinned, one log line); `tex.feedback 0`; Vulkan with `AVER_TEXTURE_COMPRESSION=0`; run with the path tracer view on (static mode).
6. **Timing** with `--gpu-timing`, `--no-vsync`, GPU idle first (the owner plays cs2; `aver-perf-measuring-traps`): per-pass delta of the scene and ray-driven passes with `tex.feedback 0` vs 1, and the new `ScopedGpuStat` regions `TexStream.Record`, `TexStream.Feedback`.

## 5. Files to create or modify, and ownership

**Created (texture streaming owns outright)**
- `modules/rhi/include/aver/rhi/TextureStreaming.hpp`
- `modules/assets/include/aver/assets/TextureStreaming.hpp`, `modules/assets/src/TextureStreaming.cpp` (streamer, planner, worker), `modules/assets/src/TextureStreamPlanner.hpp/.cpp` (pure planner and math, testable without a device)
- `modules/render.voxi/src/VoxiTexFeedback.cpp` + `VoxiTexFeedback.hpp` (feedback buffers, copy-out, clear, readback ring, glue to `ITextureStreamer`)
- `sandbox/src/TextureStreamPanel.cpp/.hpp`
- `tests/assets/` (CMake + `TextureStreamMathTest.cpp`, `TexturePlannerTest.cpp`), `tests/rhi/src/TextureStreamRhiTest.cpp`, `tests/render.voxi/src/TextureFeedbackShaderTest.cpp`, `tests/render.pbr/src/MaterialTextureIdTest.cpp`, `tests/platform/src/FileRangeTest.cpp`
- `docs/rendering/TEXTURE_STREAMING.md` (user guide: settings, console, stats, limits)

**Modified (texture streaming's edits; additive unless stated)**
- `modules/rhi/include/aver/rhi/RHIResources.hpp`: `textureStager`, `bufferMapped`, `texturesStreamable` at the end of `IResourceFactory`; `streamTextureMips` at the end of `IRenderContext`. `RHI.hpp`: `DeviceCaps::fragmentUav`.
- `modules/rhi.d3d12/src/D3D12Device.cpp`: stager, `streamTextureMips`, `srvTex` in `RhiBindingSet`, versioned `RhiBindlessTable` (`createBindlessTextureTable` :7820, `setBindlessTexture` :7858, `D3D12RenderContext::setBindlessTable` :9019), `bufferMapped`.
- `modules/rhi.vulkan/src/VulkanResourceFactory.cpp`, `VulkanRenderContext.cpp`, `VulkanCommon.hpp`: stager, `streamTextureMips`, set and table rewrite, `bufferMapped`, caps.
- `modules/platform/include/aver/platform/FileSystem.hpp` + its `.cpp`: `readFileRange`.
- `modules/assets/include/aver/assets/TextureCache.hpp`, `src/TextureCache.cpp`: `CachedTextureInfo`, `statCachedTexture`, `readCachedMips`, exported `bc7MipBytes`/`windowBytes`. `TextureUpload.cpp`: no logic change; `uploadCachedTexture` gains no streaming knowledge. `modules/assets/CMakeLists.txt`: new sources.
- `modules/render.pbr/shaders/material_prelude.hlsl`: `averFbRecord`, call sites in `averSampleMaps`, `averBlendLayers`, `averSampleSlot`. `modules/render.pbr/include/aver/pbr/MaterialSystem.hpp`, `src/MaterialSystem.cpp`: ids, streamer hooks, `texIndex` in Entry constants.
- `modules/render.voxi/shaders/voxi_rt.hlsli` (`averRtSampleSlot`), `voxi.hlsl` (primary-hit flag around :2099 and its split twin; pixel-shader entry sets `gAverFbOn`), `modules/render.voxi/src/VoxiRenderer.cpp` (`giTableKinds` slot, `residentTexture`, the three `prePass` calls, `reportVramUsage` category), `VoxiRenderer.hpp`/`Voxi.hpp` (`setTextureStreamer`, `kMaterialTexIdCap`).
- `Runtime/src/GameContent.cpp`/`include/aver/game/GameContent.hpp` (streamer ownership, resolvers), `Runtime/src/GameApp.cpp`, `sandbox/src/SandboxApp.cpp` (+ `SandboxProject.cpp` for the cache-dir pairing), `sandbox/src/EditorConsole.hpp`, `sandbox/src/SandboxShell.cpp` (status bar).
- `modules/formats/include/aver/formats/OcProject.hpp`, `src/OcProject.cpp`; `modules/render.voxi/include/aver/voxi/ProjectRenderApply.hpp`.
- Docs, append-only: `docs/rendering/VRAM.md` (texture category), `docs/MATERIALS.md` (ids, streaming), `docs/formats/FORMAT_SPECS.md` (a note that the DDC "OCTX" v1 container is not the cooked `.octex` of section 6), `docs/MODULES.md` if a dependency line changes (none expected).

**Shared-file rules with the other wave-4 features**
- `modules/render.voxi/src/VoxiRenderer.cpp`, `voxi.hlsl`, `VoxiRenderer.hpp`: also touched by geometry (cluster draw/culling) and many-light (light table, UAV/SRV slots). Streaming's edits are single-purpose blocks listed above; any new Voxi table-0 slot is **appended after the highest existing one** and the `static_assert` in `giTableKinds` is rewritten by whoever lands second to name all trailing slots. The many-light feature must not reuse a UAV slot index that this doc left as a symbolic `kTexFeedbackUavSlot`.
- `modules/render.pbr/include/aver/pbr/MaterialGpu.hpp` (the `MaterialConstants` layout) is **not touched** here; the material-instances feature owns it. If it changes the struct, `texIndex[8]` must stay and keep its meaning.
- `MaterialSystem.hpp/.cpp`: shared with materials. Streaming adds members and calls inside `writeSlots`, `releaseSlotTextures`, `update` only; whoever merges second keeps both.
- `RHIResources.hpp`/`RHI.hpp`/backends: geometry may add buffer/BLAS entries; streaming appends at the class ends and never reorders. `D3D12Device.cpp` and `VulkanCommon.hpp` are single large files: keep the texture-streaming code in clearly marked `// ---- texture streaming ----` blocks.
- `modules/trifactor`: untouched.

## 6. Performance budget and how to measure it

| Item | Budget | How |
|---|---|---|
| Feedback write, raster + ray-driven | <= 0.10 ms at 1440p, Epic RT | `--gpu-timing` per-pass A/B, `tex.feedback 0` vs 1; first suspect if over: the racy pre-read, then the decimation factor |
| Feedback copy-out + clear | <= 0.02 ms | `TexStream.Feedback` stat |
| `recordFrame` GPU (copies, barriers) | <= 0.3 ms at 8 MiB/frame | `TexStream.Record` stat |
| Planner CPU | <= 0.2 ms for 4096 textures | `HitchMarks`-style CPU span |
| Swap descriptor rewrite | <= 0.05 ms (a few hundred sets) | CPU span |
| Worker | 1 thread, <= 1 core busy, never blocks render | task manager, `AVER_HITCH_MS=100` |
| Upload | 8 MiB/frame default (about 480 MB/s at 60 fps) | `stats()` |
| Bindless D3D12 copy | one 4096-descriptor copy per version change | CPU span; zero on still frames |
| Memory | resident textures within pool +/- one swap (a transient new + old backing of one texture) | `[Streaming]` line vs `videoMemory()` |
| Frame hitch | none above 100 ms attributable to streaming | `AVER_HITCH_MS=100` flights |
| Convergence | a teleport settles to wanted mips in <= 2 s at default rates | screenshot series |

## 7. Patent check (LIVE, 2026-10-11)

Searched Google Patents and fetched the records. Texture streaming itself (mip chains loaded by distance/visibility) is decades old and unpatentable as such; the live risk is in hardware feedback and in specific filtering/transfer techniques. Findings, with what this design does not do:

- **US 10,388,058 B2**, Microsoft Technology Licensing, "Texture residency hardware enhancements for graphics processors", filed 2017-05-30 (priority 2017-02-16), granted 2019-08-20, listed Active, expires 2037-09-15 (Google's assumption). Disclosure: a residency map in the GPU giving the resident LOD plus a fractional smoothing term, and residency sampling without a texel fetch. I could not read the claims (the fetched text omitted them), so the scope is unconfirmed. Design-around: no hardware residency map, no residency sampler, no fractional-LOD value derived from residency; residency is CPU state and the GPU only ever samples an ordinary complete mip chain.
- **US 12,430,838 B2** ("Read sampler feedback technology") and **US 12,412,332 B2** ("Native sampler feedback technology"), Intel; Active per Google. Hardware sampler feedback units and mip-region feedback maps. Design-around: no sampler feedback; footprint is computed by ordinary shader arithmetic and an atomic into a buffer.
- **US 12,524,950 B2**, AMD (ATI Technologies ULC / AMD), granted 2026-01-13, Active, adjusted expiry 2044-01-04 per Google. Claim 1 (read from the record): delaying the lower-to-higher mip transition by biasing trilinear LOD according to whether the higher mip is loaded, then nonlinear filter weights from the adjusted LOD. Design-around, and a hard rule for the implementer: **no LOD bias, filter weighting or blend that depends on whether a higher mip is resident.** Pops are left unhidden in v1; any future pop smoothing must be a separate patent check (a time-based fade of the swap itself is not claimed there but is not verified).
- **US 12,461,751 B2**, Intel, "Lock free high throughput resource streaming", granted 2025-11-04, Active, expiry 2044-02-05 per Google. Claims unread. Design-around: the worker/render handoff uses `std::mutex`-guarded queues, and nothing here advertises or relies on lock-free transfer of graphics resources between concurrent threads.
- **Microsoft US 8,587,598 family** (tile-based texture management, shader-supplied LOD clamp with clamp feedback; the number came from a search summary and was NOT verified; filed 2010, so likely expiring around 2030). Design-around: whole-mip windows, no tiles, no per-sample LOD clamp, no shader feedback on clamping.

Uncertain: claims of 10,388,058 and 12,461,751 were not read, and no assignee-filtered search for Epic or NVIDIA texture-streaming patents was run beyond a general query that returned none naming Epic. Owner decision requested before ship (question 5).

## 8. Risks and open questions for the owner

1. **Ids move into MaterialSystem and `texIndex` becomes meaningful on raster.** Smallest change that avoids a `MaterialConstants` layout change, but it makes the b2 block carry a number it never did. The material-instances feature must keep `texIndex[8]`. Alternative: widen the struct (a three-mirror edit) and give raster its own array.
2. **Packaged builds have no usable DDC.** The cache key includes the absolute source path and mtime (`cacheKey`, `TextureCache.cpp:103`), so a cache built on the dev machine does not hit on another. Streaming still works in a package (first run: today's decode + encode, then partial reads), but cooked per-platform textures (`FORMAT_SPECS.md` section 6, with its `STRT` streaming table) are the right fix and a separate feature. Ask: should streaming wait for cooking in the packaged runtime, or ship with first-run baking?
3. **D3D12 bindless versioning costs 8192 descriptors** (staging + a second range) and touches code the Vulkan path already has. If heap pressure matters, drop `kRtTextureCapacity`'s unused tail (a lower capacity is baked into every ray pipeline).
4. **Feedback by 1-in-16 pixel decimation** can miss a thin object (a wire, a railing) for a few frames; the 8-frame min-hold and the rotation cover it, but a 2 px-wide object could stay at the initial mip until the camera moves. If seen in review, raise the sampling to 1 in 4 for textures with fewer than N hits, or add a second, per-instance-bounds-based floor.
5. **Patent risk.** Two claim sets were unread; the AMD claim 1 constraint (no residency-dependent LOD bias) is binding. Owner decision: accept the design-arounds, or commission a counsel read of 10,388,058 and 12,461,751 before shipping.
6. **Blocking tail seed** per new texture (2.9). Acceptable at level load; mid-play material streaming (Caldera cells) may hitch until stage 5. Measure first.
7. **Stale DDC entries.** A DDC `.octex` produced by an older encoder version is not served (the key carries the version) and is rebaked on miss; a hit verified only by header and byte counts, so a corrupt-but-consistent file is trusted. Same as today.
8. **Fragment-stage atomics on Vulkan devices without `fragmentStoresAndAtomics`** fall to static mode (everything wants mip 0 under the global bias). Acceptable? The alternative is a compute-only feedback pass, which needs a G-buffer of (texture id, UV) that does not exist.
9. **Pool default (25% of budget, floor 256 MiB)** is a guess; the implementer reports resident MiB on Caldera and NeonDistrict and proposes the default.
10. **Editor previews and the actor preview** open textures pinned and therefore uncounted by the planner's trimming but counted in its totals; two copies of the same file can exist (main and preview `MaterialSystem` caches), as today.

## 9. Implementation order (each step builds and tests)

1. Platform `readFileRange`, cache `statCachedTexture`/`readCachedMips`/window math, `TextureStreamMathTest`.
2. Planner (pure) + `TexturePlannerTest`.
3. RHI: stager, `bufferMapped`, `streamTextureMips`, descriptor rewrite, D3D12 bindless versioning; `TextureStreamRhiTest` on D3D12, then Vulkan.
4. MaterialSystem ids + Voxi `residentTexture` change (behaviour-neutral: run the existing suites and an equivalence screenshot with streaming still off).
5. Feedback: shader, UAV slot, copy-out/clear/readback, `TextureFeedbackShaderTest`.
6. Streamer worker + `open` + host wiring + project keys + console + panel; verification 4.1-4.6.
7. Optional stage 5: deferred seed, if the hitch gate trips.
