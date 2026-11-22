# 057 — RSX Vulkan GPU Compute Offload & SPU Optimizations

## 1. Overview

This document records the architectural improvements to move rendering compute directly onto the GPU via Vulkan compute shaders and eliminate CPU bottlenecks in hot SPU pipelines.

---

## 2. RSX Vulkan GPU Compute Offload

Previously, vertex attribute unpacking, index expansion, and framebuffer resolves were handled on the host CPU or via heavy synchronization, consuming significant CPU time during draw submission.

### 2.1 Compute Pipelines Added
- **`rsx_vk_vertex_decode.comp`**:
  Offloads RSX vertex attribute decode (float16/float32 unpacking, normalization, format conversions) to GPU compute dispatches. Input guest buffer pointers are bound directly, and vertices are written into Vulkan vertex buffers ready for draw commands.
- **`rsx_vk_index_expand.comp`**:
  Expands RSX index formats (16-bit / 32-bit index buffers, primitive restart) on the GPU, avoiding CPU index traversal.
- **`rsx_vk_tex_decode.comp`**:
  Performs on-GPU texture format decoding and swizzling.
- **`rsx_vk_depth_color.comp`**:
  Handles depth/color resolves and format conversions via compute shaders instead of slow CPU round-trips.

### 2.2 Host Integration
- Integrated into `libs/video/rsx_draw_engine.c` and `libs/video/rsx_vulkan_backend.c`.
- **Runtime Knobs**:
  - `PS3RECOMP_VK_GPU_VERTEX=1`: Enable GPU vertex compute dispatch (default: 1).
  - `PS3RECOMP_VK_DEPTH_COLOR=1`: Enable GPU compute depth/color resolve (default: 1).
  - `PS3RECOMP_VK_COALESCE_RP=1`: Coalesce adjacent compatible render passes (default: 1).
  - `PS3RECOMP_VK_DYN_DESC=1`: Dynamic descriptor indexing for textures/buffers (default: 1).

---

## 3. SPU Float Flush Helpers (`spu_helpers.h`)

### 3.1 Problem
The SPU single-precision floating-point emulation helpers (`spu__x8_fa`, `spu__x8_fm`, `spu__x8_fma`, `spu__x4_*`) checked validity via `spu__x8_plain0`. In games with inactive or zeroed vector lanes containing denormals/subnormals, the check rejected inputs and entered slow software fallback loops (`spu__fma_lane`), dropping framerates.

### 3.2 Solution
- Added `spu__x8_flush` and `spu__x4_flush`:
  Lanes with zero exponents are flushed immediately using SIMD mask operations (`_mm_andnot_si128`), allowing `plain0` to avoid unnecessary scalar fallbacks.
- Eliminated compiler warnings (`-Wmisleading-indentation` in `spu_bit_right`, `-Wmaybe-uninitialized` in `spu__fma_lane`).
- **Verification**:
  - `test_spu_helpers`: 72/72 tests passed.
  - `test_spu_float_exact`: 446,840,832 lanes verified bit-identical (0 mismatches).
  - `test_spu_float_vector_scalar`: 64,714,984 lanes verified bit-identical (0 mismatches).

---

## 4. SPU Job 187280 (Dragon & Entity Instanced Mesh 3D Transform & Culling)

### 4.1 Optimizations
- **Fast 3D Transform (`j_xform_elem_fast`)**:
  Vectorized 16-byte aligned instance transform using AVX2 SIMD intrinsics.
- **Group Scale Hoisting**:
  Hoisted scale factor calculations outside inner per-vertex transform loops.
- **Hoisted Triangle Culling State Memcpy**:
  Replaced per-triangle 2 KB `memcpy(G, gstate, sizeof G)` with single hoists in `j_cull_chunk` and `j_cull_parallel`.
- **Thread-Local Scratch Buffers**:
  Eliminated hot-path dynamic memory allocations (`aligned_alloc`/`free`) in favor of thread-local scratch buffers (`tl_gsnap`, `tl_scratch`, `tl_decision`, `tl_stash`).
- **Verification**:
  - Standalone harness: 3,033 / 3,033 cases passed.
  - In-game `J187280_VERIFY=1`: 80,000+ dispatches bit-identical with 0 mismatches.

---

## 5. SPU Job 187F00 (Sprite Depth Sorting)

### 5.1 Optimizations
- **Phase 1 Vectorization**:
  Calculates sprite depth keys using 4-wide AVX2 hardware FMA (`spu_fm`, `spu_fma`, `spu_fa`).
- **Phase 2 Integer Sort Keys**:
  Converted capped gnome sort to compare integer sort keys (`j_depth_key`), replacing thousands of repetitive `memset` and `spu_fcgt` floating-point operations with single-cycle integer comparisons.
- **Verification**:
  - In-game `CC_JOB187F00_VERIFY=1`: 120,000+ dispatches verified bit-identical with 0 declined and 0 mismatches.

---

## 6. SPU Job 188300 (Combat Target Scan)

### 6.1 Optimizations
- **Candidate Filter Early-Out**:
  Pre-tests the candidate selection bitmask (`r71 = spu_and(r5, r42)`). When zero, skips >30 heavy vector instructions (distance calculation, reciprocal square root, Newton-Raphson refinement).
- **Lazy Invariant Hoisting**:
  Invariants (`r63, r64, r60, r58, r57, r46`) are computed lazily on the first active candidate iteration, ensuring cells with 0 candidates exit immediately without altering register file state.
- **Verification**:
  - In-game `CC_JOB188300_VERIFY=1`: Over 1,600,000 scans compared against the lifted code across all 128 registers with 0 mismatches.
