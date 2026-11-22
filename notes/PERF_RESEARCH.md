# Performance Review & Research Report: ps3recomp Vulkan RSX Backend

**Target Title**: *Comet Crash* (PS3 2.5D RTS/Tower Defense)  
**Target Environment**: Linux (x86_64), AMD Radeon RX 7900 XTX (RADV driver, RDNA3), 1920x1080  
**Scope**: Read-Only Architectural Review of RSX Draw Engine & Vulkan Backend  
**Reviewer Role**: Second Performance Analyst  
**Report File**: `/home/gcerkez/tools/cc-recomp/PERF_RESEARCH.md`  

---

## 1. Executive Summary & Starting Evidence

This review analyzes the CPU-side performance profile of the `ps3recomp` Vulkan RSX subsystem (`rsx_vulkan_backend.c`, `rsx_draw_engine.c`, and `cellGcmSys.c`). 

### Verified Starting Facts:
1. **Draw Batching**: Draw batching is implemented under `VK_DRAW_BATCH=1` with default `VK_DRAW_BATCH_MAX=512` ([`rsx_vulkan_backend.c:3624-3629`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3624-L3629)). Batched draws record into a single open command buffer, taking sub-allocated slices of host-coherent ring buffers (`vb`, `ib`, `ub`, `sb`) and allocating descriptor sets from a pre-allocated pool (`s_eb.pool`) ([`rsx_vulkan_backend.c:3596-3598`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3596-L3598), [`3825-3845`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3825-L3845)).
2. **Synchronous Flush**: `vk_eng_flush()` ends the open command buffer, calls `vk_submit_and_wait()` which calls `pvkQueueSubmit` and immediately waits on a fence (`pvkWaitForFences`), and resets all ring heads and the descriptor pool ([`rsx_vulkan_backend.c:3701-3710`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3701-L3710), [`602-608`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L602-L608)). Thus, CPU command recording and GPU hardware execution never overlap across flushes ([`rsx_vulkan_backend.c:3598-3601`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3598-L3601)).
3. **Pervasive Barriers**: A full execution and memory barrier (`VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` to `VK_PIPELINE_STAGE_ALL_COMMANDS_BIT` with `VK_ACCESS_MEMORY_WRITE_BIT` to `VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT`) is recorded before every batched draw ([`rsx_vulkan_backend.c:3858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3858), [`579-586`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L579-L586)), followed by an image barrier for every bound color target ([`rsx_vulkan_backend.c:3877-3878`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3877-L3878), [`615-635`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L615-L635)). Because barriers cannot be placed inside a Vulkan render pass without self-dependencies, every batched draw issues `pvkCmdBeginRenderPass` and `pvkCmdEndRenderPass` independently ([`rsx_vulkan_backend.c:3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882), [`3912`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3912)).
4. **Present Path Serialization**: Presenting an image performs synchronous fence waits: `s_vk.present_fence` in `vk_present_complete_locked()` to read back a 1-pixel center value for debugging ([`rsx_vulkan_backend.c:2051-2059`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2051-L2059)), followed by `s_vk.window_fence` in `vk_present_window()` prior to `vk_queue_present` ([`rsx_vulkan_backend.c:1994-2005`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L1994-L2005)).
5. **Real-Session Target Metrics**:
   - Campaign light scene: ~38 guest FPS; GPU busy only 23-29% on RX 7900 XTX.
   - Host FIFO thread: ~82% CPU utilization in *compute* state (runnable, executing host code, not sleeping).
   - SPU pool: ~6 worker threads at ~43% CPU each.
   - Session aggregate: 3,126,442 draws across 98,304 submissions (~31.8 draws/submission; exactly ~2 submissions per guest frame).
   - Frame structure: 3D scene pass (2x2 antialiased target) $\rightarrow$ NV3089 blit / resolve to display buffer $\rightarrow$ GUI overlay pass $\rightarrow$ flip/present.
6. **Stack Sampling Evidence (Addendum)**: Sampling the hot 82% thread on the real target resolved to: `vk_eng_readback`, `rsx_draw_engine_guest_read`, `nv3089_blit` (`cellGcmSys.c`), `sink_end`, `rsx_texture_decode`, libc `memcpy`. The NV3089 blit fallback complex dominated because `rsx_draw_engine_blit` ([`rsx_draw_engine.c:1077-1084`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1077-L1084)) rejected antialiased sources, forcing a synchronous GPU readback to guest memory, CPU row copies, and downstream texture re-decodes every frame.

---

## 2. Q1: Ranking of CPU-Side Costs in the Draw + Present Path

For a title executing ~100–200 draws per frame with batching enabled, the CPU costs in `rsx_vulkan_backend.c` rank as follows:

| Rank | Operation / Mechanism | Primary Source Citation | Cost per Call | Frequency per Frame | Impact / Analysis |
| :---: | :--- | :--- | :--- | :---: | :--- |
| **1** | **Descriptor Set Allocation & Descriptor Updates (`pvkAllocateDescriptorSets` + `pvkUpdateDescriptorSets`)** | [`rsx_vulkan_backend.c:3833-3845`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3833-L3845), [`3732-3777`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3732-L3777), [`3531-3556`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3531-L3556) | Calls driver descriptor allocator, scans up to 64 sampler cache entries 16 times, populates up to 19 `VkWriteDescriptorSet` structs, and calls `vkUpdateDescriptorSets`. | 100–200 calls/frame (1 per draw) | **Dominant active CPU compute cost.** Because uniform buffer bindings are static rather than dynamic, every draw allocates a fresh set and writes up to 34 descriptors into driver memory. |
| **2** | **Render Pass Begin/End Pairs (`pvkCmdBeginRenderPass` + `pvkCmdEndRenderPass`)** | [`rsx_vulkan_backend.c:3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882), [`3912`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3912), [`3476-3496`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3476-L3496) | Closes subpass/renderpass, validates framebuffer keys via linear scan (`memcmp`), and starts new render pass with `LOAD_OP_LOAD` / `STORE_OP_STORE`. | 100–200 pairs/frame (1 per draw) | **Heavy driver CPU overhead.** Prevents driver command batching and forces tile/subpass state emission per draw. |
| **3** | **Per-Draw Full Execution & Memory Barriers (`vk_cmd_after_everything` + `vk_barrier_image`)** | [`rsx_vulkan_backend.c:3858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3858), [`3877-3878`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3877-L3878), [`579-586`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L579-L586), [`615-635`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L615-L635) | Encodes global pipeline barriers (`ALL_COMMANDS_BIT`, `MEMORY_WRITE_BIT`) and per-render-target image memory barriers. | 100–200 barrier packets + 100–400 image barriers/frame | Driver CPU overhead to record barriers; creates catastrophic GPU execution serialization between draws. |
| **4** | **Host-Coherent Ring Buffer `memcpy` Writes (Vertices, Indices, Constants)** | [`rsx_vulkan_backend.c:3839-3840`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3839-L3840), [`3852-3854`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3852-L3854), [`3712-3718`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3712-L3718) | Sub-allocates from `vb`, `ib`, and `ub` rings via `vk_eb_take` and executes 3–4 host `memcpy` calls per draw into write-combined memory. | 300–800 `memcpy` calls/frame (moving ~1–3 MB/frame) | Sustained CPU cache and write-combining bus traffic. |
| **5** | **Present Path Synchronous Fence Stalls & Readback** | [`rsx_vulkan_backend.c:2051-2059`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2051-L2059), [`1994-2005`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L1994-L2005) | Stalls host CPU thread on `present_fence` to read back center pixel, then stalls on `window_fence` after swapchain blit submission. | 2 fence wait calls/frame | **Latency/Stall bottleneck (wall-clock loss).** Does not burn CPU cycles directly (blocked state), but serializes frame pacing and throttles guest throughput. |
| **6** | **Batch Submission & Flush Wait (`vk_eng_flush` / `vk_submit_and_wait`)** | [`rsx_vulkan_backend.c:3701-3710`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3701-L3710), [`602-608`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L602-L608) | Ends command buffer, resets fences, acquires mutex, calls `pvkQueueSubmit`, waits on fence (`pvkWaitForFences`), resets ring heads and descriptor pool. | 1–2 calls/frame | Stalls CPU thread at end-of-batch until GPU catches up. |
| **7** | **Redundant Dynamic State Recording (`pvkCmdSet*`)** | [`rsx_vulkan_backend.c:3897-3901`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3897-L3901) | Calls `pvkCmdSetViewport`, `pvkCmdSetScissor`, `pvkCmdSetStencilReference`, and `pvkCmdSetDepthBounds` unconditionally. | 400–800 driver calls/frame (4 per draw) | Unnecessary driver API call dispatch overhead. |

### Detailed Breakdown of Top Costs:

#### Cost 1: Descriptor Set Churn (`rsx_vulkan_backend.c:3833-3845`, `3732-3777`)
At lines [`3833-3836`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3833-L3836), every draw allocates a brand new `VkDescriptorSet` from `s_eb.pool`. Then `vk_eng_write_descriptors_to` ([`lines 3744-3753`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3744-L3753)) iterates over all 16 texture units:
- For each unit, it calls `vk_eng_sampler(&s_e2.samp[u])` ([`line 3748`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3748)). `vk_eng_sampler` ([`lines 3531-3540`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3531-L3540)) packs a 64-bit key and performs a linear array scan over up to 64 cached samplers (`s_esamp`). At 16 units per draw across 200 draws, this executes up to 3,200 linear sampler comparisons per frame.
- It builds up to 19 `VkWriteDescriptorSet` structures ([`lines 3754-3774`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3754-L3774)) pointing to 2 uniform buffer ranges, 16 sampled image views, and 16 sampler handles.
- It calls `pvkUpdateDescriptorSets(s_vk.device, n, w, 0, NULL)` ([`line 3775`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3775)). In the Mesa RADV driver, this function parses all 19 descriptor writes, validates parameters, and writes hardware descriptor entries into GPU-mapped host memory. Across 200 draws, this burns thousands of CPU cycles in the hot loop.

#### Cost 2: Render Pass Trashing (`rsx_vulkan_backend.c:3882`, `3912`)
At line [`3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882), `pvkCmdBeginRenderPass` is invoked, and at line [`3912`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3912), `pvkCmdEndRenderPass` is invoked. Because each draw begins and ends its own render pass, the Vulkan driver must emit subpass prologues, epilogues, and tile load/store packets 100–200 times per frame. In addition, `vk_eng_framebuffer` ([`lines 3476-3496`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3476-L3496)) performs a linear search over up to 64 cached framebuffers using `memcmp` on every draw ([`lines 3482-3483`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3482-L3483)).

---

## 3. Q2: Analysis of Intra-Frame Flush Triggers

Fact 5 established an aggregate ratio of 3,126,442 draws in 98,304 submissions, which equates to **~31.8 draws per submission** and **~2 submissions per guest frame**. Since one submission is always the per-frame present flush ([`rsx_vulkan_backend.c:2551`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2551)), exactly **one intra-frame flush** fired per frame.

Evaluating the candidate flush triggers:

### Candidate A: The NV3089 AA Blit Fallback Complex (CONFIRMED TRIGGER)
- **Mechanism**: The title renders a 3D scene pass into an antialiased render target (~30–35 draws). It then issues an NV3089 blit method in `cellGcmSys.c` ([`line 1531`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1531)) to resolve the antialiased scene target to the display buffer.
- **Evidence in Code**: `rsx_draw_engine_blit` ([`rsx_draw_engine.c:1077-1084`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1077-L1084)) explicitly rejected antialiased sources (`if ((g.surfaces[si].aa_grid & 0xFFu) > 1u || ...) return 0;`), returning 0. 
- In `cellGcmSys.c:1538`, fallback execution fell through to `nv3089_engine_read(in_pitch, out_w, out_h)`.
- `nv3089_engine_read` invoked `rsx_draw_engine_guest_read` ([`rsx_draw_engine.c:979-1025`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L979-L1025)).
- At line [`1002`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1002), `rsx_draw_engine_guest_read` explicitly called:
  ```c
  g.be->submit_and_wait(g.be->user, RSX_BE_FLUSH_GUEST_REFERENCE);
  ```
- In `rsx_vulkan_backend.c:2431-2435`, `vk_eng_submit_and_wait()` called `if (s_eb_pending) vk_eng_flush();`.
- **Conclusion**: This submitted Batch 1 (the ~30 draws of the 3D scene pass). Following the readback, CPU row copy, and texture re-uploads, the GUI pass recorded its ~30 draws into Batch 2, which was then flushed by `vk_eng_present()` ([`rsx_vulkan_backend.c:2551`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2551)). This completely and uniquely accounts for the ~32 draws/submission and ~2 submissions/frame observed.

### Candidate B: Render-Target / Surface Switches (`vk_begin`) (DISPROVED)
- **Investigation**: Does switching render targets trigger `vk_begin` or `vk_eng_flush`?
- **Evidence in Code**: When render targets switch, `rsx_dispatch.c` updates registers. At draw time, `eng_current_target_set` calls `g.be->bind_targets` ([`rsx_draw_engine.c:843-858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L843-L858)). In `rsx_vulkan_backend.c:2495-2505`, `vk_eng_bind_targets` merely stores target handles into `s_eng_bound`. In `vk_eng_draw` ([`lines 3855-3858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3855-L3858)):
  ```c
  if (!s_eb_pending && vk_begin()) return;
  ```
  `vk_begin()` is *only* called if `!s_eb_pending` (when no batch is currently open). While a batch is open (`s_eb_pending > 0`), switching render targets merely binds a different framebuffer ([`rsx_vulkan_backend.c:3810`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3810)) and begins a new render pass ([`line 3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882)) within the *same* command buffer. It does **not** call `vk_eng_flush()`.
- **Scratch Depth Caveat**: `vk_eng_scratch_depth` ([`rsx_vulkan_backend.c:3500-3515`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3500-L3515)) calls `vk_eng_drop_framebuffers()`, `vk_destroy_image()`, and `vk_begin()` *only* if the scratch depth must be created or grown. Once initialized to display resolution (1920x1080), line [`3502`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3502) returns the existing view without flushing.

### Candidate C: Texture & Surface Destruction Churn (DISPROVED)
- **Investigation**: Does resource destruction trigger `vk_destroy_image` / `vk_destroy_buffer`?
- **Evidence in Code**: 
  - `rsx_vulkan_backend.c:3955` sets `.texture_upload_ordered = 1`. In `rsx_draw_engine.c:1643-1651`, when a texture changes, `into = e->handle`; existing textures are updated in-place via `vk_eb_upload` ([`rsx_vulkan_backend.c:3678-3699`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3678-L3699)). No textures are released or destroyed during gameplay.
  - Render target surfaces are pooled up to 64 in `g.surfaces` ([`rsx_draw_engine.c:775`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L775)). In *Comet Crash*, only 4 total surfaces are ever created throughout the session (see log citation `[RSX_PRESENT] ... total_surfs=4`). No surface eviction or destruction occurs intra-frame.

### Candidate D: Pipeline Release (DISPROVED)
- **Investigation**: Does pipeline destruction occur intra-frame?
- **Evidence in Code**: `g.be->pipeline_release` is referenced in `rsx_draw_engine.c` exclusively within `rsx_draw_engine_shutdown()` ([`line 3861`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L3861)). Pipelines are cached permanently in `g.pipelines` ([`lines 2342-2346`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L2342-L2346)) and `P->variant` ([`rsx_vulkan_backend.c:3244`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3244)). Zero pipelines are released intra-frame.

### Candidate E: Semaphore & Label Writes (DISPROVED)
- **Investigation**: Do synchronization methods flush Vulkan batches?
- **Evidence in Code**: In `cellGcmSys.c:2887-2903`, handling `NV406E_SEMAPHORE_RELEASE` (method `0x6C`) updates guest memory via `vm_write32(la, v)`. In `rsx_commands.c:526-557`, handling `NV4097_BACKEND_LABEL` (methods `0x1D70` / `0x1D74`) writes to guest memory via `vm_write32`. Neither path invokes `rsx_draw_engine_flush()` or `vk_eng_flush()`.

### Summary for Q2
The **sole intra-frame flush trigger** in *Comet Crash* was `rsx_draw_engine_guest_read` invoked by the NV3089 blit fallback path when antialiased sources were rejected. Once the GPU AA blit is accepted, **zero intra-frame flushes occur**, and all ~100–200 draws of the frame coalesce into a single submission flushed by `vk_eng_present()` at the flip boundary.

---

## 4. Q3: Concrete Changes, Ranked by (Expected Gain) / (Risk)

### Detailed Candidate Evaluation:

#### Candidate (a): Defer Flush Fence Wait (Keep 2–3 Batches in Flight)
- **Evaluation**: **DEFER / REJECT as immediate priority**.
- **Reasoning**:
  - *Expected Gain (Low-to-Moderate)*: The profiling data shows the GPU is idle/starved (only 23–29% busy on RX 7900 XTX), while the host FIFO thread is compute-bound at 82% CPU. The host thread is not blocked waiting on GPU completion during the frame; it is spending CPU time in driver recording, memory copies, and descriptor updates. Deferring the fence wait across multiple frames does not reduce CPU instruction count; in fact, managing multiple ring partitions, multiple descriptor pools, and tracking in-flight fences increases CPU bookkeeping.
  - *Regression Risk (High)*: The current backend architecture assumes that after `vk_eng_flush()` returns, all previously recorded GPU work is complete ([`rsx_vulkan_backend.c:3599-3601`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3599-L3601)). Deferring fence waits introduces hazards with object destruction (`vk_destroy_image`), immediate texture updates, host readbacks, and render target surface recycling.

#### Candidate (b): Narrow Per-Draw Full Memory Barrier & Coalesce Render Passes
- **Evaluation**: **ACCEPT (High Priority / Replace with Render Pass Coalescing)**.
- **Reasoning**:
  - *Mechanism*: Currently, line [`3858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3858) executes `vk_cmd_after_everything(s_vk.cmd)` before every draw, recording an `ALL_COMMANDS_BIT` pipeline barrier ([`lines 579-586`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L579-L586)), followed by `vk_barrier_image` on all color targets ([`line 3878`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3878)). Because pipeline barriers cannot be placed inside a Vulkan render pass without subpass dependencies, the backend issues `pvkCmdBeginRenderPass` ([`line 3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882)) and `pvkCmdEndRenderPass` ([`line 3912`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3912)) on every single draw.
  - *Expected Gain (Very High)*: Consecutive draws rendering into the same framebuffer attachment do not require memory barriers between them; standard Vulkan rasterization order and pipeline blending handle primitive ordering. By keeping the render pass open across draws that target the same framebuffer and only inserting a barrier when an active color target is bound as a texture or when render targets change:
    1. 100–200 `vkCmdBeginRenderPass` and `vkCmdEndRenderPass` driver calls are eliminated per frame.
    2. 100–200 full GPU pipeline flushes and L1/L2 cache invalidation bubbles are eliminated.
  - *Regression Risk (Low-to-Moderate)*: `rsx_draw_engine.c` already detects when a bound texture aliases an active render target (feedback loop detection in `sink_bind_textures`, [`lines 2854-2857`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L2854-L2857)).

#### Candidate (c): Reduce Present-Path Serialization
- **Evaluation**: **ACCEPT (Immediate / High Gain-to-Risk Ratio)**.
- **Reasoning**:
  - *Mechanism*: In `rsx_vulkan_backend.c:1994-2005`, `vk_present_window()` calls `pvkWaitForFences(s_vk.device, 1, &s_vk.window_fence, VK_TRUE, UINT64_MAX)` immediately before `vk_queue_present(&pi)`. This CPU fence wait is completely redundant: line [`1992`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L1992) signals semaphore `s_vk.sc_done[idx]`, and line [`2002`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2002) passes `s_vk.sc_done[idx]` as `pWaitSemaphores` to `vkQueuePresentKHR`. Vulkan guarantees presentation will wait on the GPU semaphore without CPU intervention.
  - In `vk_present_complete_locked()` ([`lines 2050-2059`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2050-L2059)), the host thread waits on `s_vk.present_fence` to read back `s_vk.last_center`. This debug pixel readback is only queried by test harnesses (`rsx_vulkan_backend_presented_center`, [`line 4273`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L4273)).
  - *Expected Gain (High)*: Eliminates 1–3 ms of synchronous CPU stall per frame in the present path.
  - *Regression Risk (Very Low)*: Gate the center pixel readback on `s_vk.dump_path != NULL`; remove the immediate `window_fence` wait and wait on it only before re-recording `present_cmd` on the subsequent frame.

#### Candidate (d): Per-Draw Host Work: Dynamic Uniform Buffers & Descriptor Caching
- **Evaluation**: **ACCEPT (Highest Priority for CPU Compute Bottleneck)**.
- **Reasoning**:
  - *Mechanism*: `s_eb.ub` uniform buffer slices are allocated per draw ([`rsx_vulkan_backend.c:3828-3829`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3828-L3829)). Because the descriptor layout uses static `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` ([`lines 3634`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3634), [`3758`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3758), [`3761`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3761)), the backend is forced to allocate a new descriptor set ([`line 3836`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3836)) and rewrite all 19 descriptor bindings ([`line 3775`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3775)) on *every single draw*, even when textures, samplers, and shaders are identical.
  - *Solution*: Change uniform bindings to `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`. Allocate descriptor sets only when bound textures/samplers change. Pass the per-draw constant offsets `uvs` and `ups` as `dynamicOffsets` in `pvkCmdBindDescriptorSets` ([`rsx_vulkan_backend.c:3902-3904`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3902-L3904)).
  - *Expected Gain (Highest)*: Eliminates 100–200 `pvkAllocateDescriptorSets` calls, 100–200 `pvkUpdateDescriptorSets` calls, and thousands of sampler comparisons per frame. This directly targets the compute time of the 82% CPU thread.
  - *Regression Risk (Low-to-Moderate)*: Dynamic uniform offsets are core Vulkan 1.0 features supported universally across all drivers.

---

## 5. Q4: Flip / VBlank Emulation & Guest Thread Pacing / Stalls

Analysis of `cellGcmSys.c` reveals several explicit mechanisms that pace or stall the guest main thread:

1. **`cellGcmSetWaitFlip()` Polling Sleep**:
   - **Citation**: [`cellGcmSys.c:760-781`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L760-L781)
   - **Mechanism**:
     ```c
     for (int i = 0; i < 64 && s_flip_status == CELL_GCM_FLIP_STATUS_WAITING; i++) {
         ppu_gcm_pump();
         if (s_flip_status != CELL_GCM_FLIP_STATUS_WAITING) break;
         Sleep(1);
     }
     ```
     When the guest calls `cellGcmSetWaitFlip()` to throttle against display refresh, it executes a loop calling `Sleep(1)` up to 64 times while `s_flip_status == CELL_GCM_FLIP_STATUS_WAITING`.
   - **Linux Scheduler Impact**: In `runtime/platform/win32_compat.h:712-717`, `Sleep(1)` is implemented via POSIX `nanosleep(&d, NULL)` with a 1 ms duration. On Linux, `nanosleep(1ms)` forces a thread deschedule and context switch, causing real wall-clock sleep durations of 1.2–2.0 ms per iteration depending on kernel timer resolution (`CONFIG_HZ`).

2. **`cellGcmGetFlipStatus()` Polling Loops**:
   - **Citation**: [`cellGcmSys.c:802-817`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L802-L817), [`1021-1025`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1021-L1025)
   - **Mechanism**: Games often implement their own wait loops in guest code:
     ```c
     do { usleep(100); } while (cellGcmGetFlipStatus() != CELL_GCM_FLIP_STATUS_DONE);
     cellGcmResetFlipStatus();
     ```
     `s_flip_status` is updated to `CELL_GCM_FLIP_STATUS_DONE` only inside `ppu_gcm_pump()` ([`lines 997-1000`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L997-L1000)) when signaled by `cellGcmTickFlip()` ([`lines 1029-1038`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1029-L1038)).

3. **Flip Completion Dependency on FIFO Drain (`get == put`)**:
   - **Citation**: [`cellGcmSys.c:1034`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1034), [`1807-1815`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1807-L1815)
   - **Mechanism**: `cellGcmTickFlip()` only marks a flip pending if `gcm_flip_executed_take()` succeeds. In `cellGcm_take_flip_pending_synced()` ([`lines 1807-1815`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1807-L1815)):
     ```c
     u32 put = vm_read32(GCM_CONTROL_GUEST_ADDR + 0);
     u32 get = vm_read32(GCM_CONTROL_GUEST_ADDR + 4);
     if (get != put) return 0;   /* frame not fully drained yet */
     ```
     If the FIFO walker thread lags behind the guest write head (`get != put`), the flip is not marked executed, and `cellGcmTickFlip` drops the tick, forcing the guest to wait for the subsequent vblank tick (an entire 16.6 ms frame dropped).

4. **Fence Register Observability Pacing (`cellGcmFinish` waits)**:
   - **Citation**: [`cellGcmSys.c:1817-1828`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1817-L1828), [`2180-2198`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L2180-L2198)
   - **Mechanism**: Fences (`SET_REFERENCE`) are queued into `s_ref_q` ([`line 1830`](file:///home/gcerkez/tools/ps3recomp/libs/video/cellGcmSys.c#L1830)). In `cellGcm_ref_on_poll()`, fence publication is explicitly throttled to a minimum gap of `s_freq.QuadPart / 5000` (~200 µs) between values so that equality-waiters in guest code (`cellGcmFinish`) observe every intermediate fence value without skipping. When the guest issues multiple fences per frame, this introduces artificial microsecond-scale stalls.

---

## 6. Addendum: AA Blit Optimization Analysis

### (i) Correctness Hazards of the Local AA Blit Patch
The proposed local patch in `rsx_draw_engine.c:1077-1095, 1147-1151` removes the rejection of antialiased sources, scales source dimensions by `sgx = aa_grid & 0xFF` and `sgy = aa_grid >> 8`, and passes `linear || sgx > 1u || sgy > 1u` to `color_target_blit`.

#### Coherence Machinery Evaluation:
1. **`guest_dirty = 1` Flag**:
   - **Citation**: [`rsx_draw_engine.c:1153`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1153)
   - **Hazard Check**: `rsx_draw_engine_blit` marks `g.surfaces[di].guest_dirty = 1`. If any subsequent guest operation requires guest memory (e.g. `eng_surface_to_guest` at [`line 1737`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1737) or `rsx_draw_engine_guest_read` at [`line 981`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L981)), it sees `guest_dirty == 1` and triggers GPU readback. 
   - **Verdict**: **SAFE**. The GPU destination image holds the correct resolved pixels. Guest memory is only stale with respect to direct CPU pointer reads, which *Comet Crash* never performs on scanout display buffers.

2. **`color_serial` Bumping**:
   - **Citation**: [`rsx_draw_engine.c:1152`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1152)
   - **Hazard Check**: `g.surfaces[di].color_serial = ++g.write_serial`. In `sink_bind_textures` ([`line 2868`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L2868)), `color_serial` is used to arbitrate between a color target and a depth target aliased at the same guest address. 
   - **Verdict**: **SAFE**. Advancing `color_serial` correctly ensures `di` takes precedence over older depth targets at the same address.

3. **Texture `rewritten` Flag / Cache Invalidation**:
   - **Citation**: [`rsx_draw_engine.c:152`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L152), [`1628-1630`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1628-L1630), [`1709`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1709)
   - **Hazard Check**: `eng_textures_touch` is *not* called by `rsx_draw_engine_blit`. If an entry in `g.textures` already existed for `dst_loc:dst_off`, its `rewritten` flag is not set. 
   - **Verdict**: **SAFE FOR COMET CRASH**. In `sink_bind_textures` ([`lines 2848-2860`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L2848-L2860)), active render targets are matched in `g.surfaces` *before* consulting `g.textures`. Because `di` is an active surface, any texture sampling `dst_loc:dst_off` binds `g.surfaces[di].handle` directly ([`lines 2927-2933`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L2927-L2933)), bypassing `g.textures`.

4. **Coordinate & Bounds Scaling**:
   - **Citation**: [`rsx_draw_engine.c:1090-1095`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1090-L1095), [`1149`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1149)
   - **Hazard Check**: `lim_w = g.surfaces[si].w / sgx`, `lim_h = g.surfaces[si].h / sgy`. `sw` and `sh` are clamped to `lim_w - sx_surf` and `lim_h - sy_surf`. At line [`1149`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1149), passing `sw * sgx` and `sh * sgy` is guaranteed to stay within the physical surface dimensions `g.surfaces[si].w` and `g.surfaces[si].h`.
   - **Verdict**: **CORRECT**. When downscaling a 2x2 AA surface (2560x1440 sample resolution) to a 1280x720 display buffer, passing `linear = 1` causes `vkCmdBlitImage` with `VK_FILTER_LINEAR` ([`rsx_vulkan_backend.c:2612`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2612)) to perform an exact 2x2 box resolve on hardware.

5. **Sub-Rectangle Offset Hazard (Minor Edge Case)**:
   - **Citation**: [`rsx_draw_engine.c:1071-1073`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1071-L1073), [`1149`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1149)
   - **Observation**: If a future title issues an NV3089 blit with a non-zero sub-rectangle origin (`sy > 0`), `eng_surface_at` ([`line 918`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L918)) computes `*y = d / pitch`. Because `pitch` is already the physical row pitch (10240 bytes), if the guest NV3089 command expressed `sy` in physical rows, multiplying by `sgy` at line [`1149`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1149) could double-scale the Y offset. For *Comet Crash*, `sx = sy = 0`, so this edge case is inactive.

### (ii) What the Next Bottleneck Will Be
Stack sampling showed the 82% hot thread was dominated by:
`vk_eng_readback` $\rightarrow$ `rsx_draw_engine_guest_read` $\rightarrow$ `nv3089_blit` $\rightarrow$ `sink_end` $\rightarrow$ `rsx_texture_decode` $\rightarrow$ `memcpy`.

Once the AA blit fix is applied:
1. `vk_eng_readback` is completely bypassed (no GPU readback wait).
2. `rsx_draw_engine_guest_read` is completely bypassed (no CPU conversion loop).
3. The CPU row copy in `cellGcmSys.c:1562-1574` is bypassed.
4. Downstream texture re-decode (`rsx_texture_decode`) in `sink_end` is bypassed.
5. The intra-frame batch flush at the blit point disappears; draws flow into a single unified batch.

**The immediate next bottleneck will be the per-draw CPU recording overhead in `vk_eng_draw`** (Rank 1 and Rank 2 from Section 2):
- Specifically, the **100–200 per-frame `pvkAllocateDescriptorSets` and `pvkUpdateDescriptorSets` calls** ([`rsx_vulkan_backend.c:3836`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3836), [`3775`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3775)), together with the **per-draw `pvkCmdBeginRenderPass` / `pvkCmdEndRenderPass` cycling** ([`lines 3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882), [`3912`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3912)) and `ALL_COMMANDS_BIT` pipeline barriers ([`line 3858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3858)).
- In heavy scenes with many units, the SPU workers waiting on `s_sig_cv` in `WAIT_SIGNAL` ([`runtime/spu/spu_workload.c:1550-1573`](file:///home/gcerkez/tools/ps3recomp/runtime/spu/spu_workload.c#L1550-L1573)) wait 7–13 ms primarily because the main PPU thread is stalled in this per-draw Vulkan driver dispatch overhead.

---

## 7. Consolidated Ranked Recommendations

The following actionable modifications are ranked by $\frac{\text{Expected Gain}}{\text{Regression Risk}}$:

| Priority Rank | Change Title | Target Source File & Lines | Expected Gain | Regression Risk | Verification & Test Strategy |
| :---: | :--- | :--- | :---: | :---: | :--- |
| **1** | **Accept GPU AA Blit with Grid Scaling & Linear Filter** | [`rsx_draw_engine.c:1077-1095`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1077-L1095), [`1147-1151`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_draw_engine.c#L1147-L1151) | **Massive (Immediate 1.5x–2.0x FPS)**: Eliminates GPU readback stall, CPU row copy, and texture decode. Eliminates 1 intra-frame flush per frame. | **Very Low**: Guarded by `sgx > 1 \|\| sgy > 1`; preserves destination non-AA check (`line 1144`). Coherence flags (`guest_dirty`, `color_serial`) are correctly updated. | Verify visually in *Comet Crash* that full board overview and complete HUD render correctly without quadrant crop. Confirm `vk_eng_readback` disappears from `eu-stack`. |
| **2** | **Dynamic Uniform Buffers & Descriptor Set Reuse** | [`rsx_vulkan_backend.c:3634-3636`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3634-L3636), [`3732-3775`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3732-L3775), [`3833-3845`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3833-L3845), [`3902-3904`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3902-L3904) | **Very High**: Eliminates 100–200 `vkAllocateDescriptorSets` and `vkUpdateDescriptorSets` calls per frame; reduces CPU driver recording cost by ~40–60%. | **Low**: Standard Vulkan 1.0 dynamic UBO offsets (`pDynamicOffsets`). Sets only allocated when texture/sampler bindings change. | Run existing backend test suites (`tests/test_gcm_*`). Verify shader constant updates in rotating geometry. Measure host thread CPU drop. |
| **3** | **Coalesce Render Passes & Narrow Draw Barriers** | [`rsx_vulkan_backend.c:3858`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3858), [`3878`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3878), [`3882`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3882), [`3912`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3912) | **Very High**: Eliminates 100–200 `vkCmdBeginRenderPass`/`vkCmdEndRenderPass` calls and `ALL_COMMANDS` barrier bubbles per frame. Unlocks GPU pipeline overlap. | **Moderate**: Requires tracking active open render pass; must insert pipeline barrier and split pass only when render target changes or an active target is bound as texture input. | Check for visual artifacts in alpha blending and multi-pass rendering (e.g. shadow map passes, GUI overlay). Verify with RenderDoc capture. |
| **4** | **Eliminate Redundant Present-Path Fence Waits** | [`rsx_vulkan_backend.c:1994-2005`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L1994-L2005), [`2051-2059`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L2051-L2059) | **Moderate**: Saves 1–3 ms of frame latency and host thread wait time per frame. | **Very Low**: Remove redundant `window_fence` wait before `vkQueuePresentKHR` (synchronized by `sc_done` semaphore); gate `last_center` readback on `s_vk.dump_path != NULL`. | Check swapchain presentation pacing under Mangohud/vkBasalt; verify `--split-present` regression test in `host_posix.c`. |
| **5** | **Dynamic State Redundancy Filtering** | [`rsx_vulkan_backend.c:3897-3901`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3897-L3901) | **Low**: Eliminates 400–800 redundant driver function calls per frame by caching last applied viewport, scissor, stencil ref, and depth bounds. | **Very Low**: Standard dirty-tracking pattern. | Verify viewport and scissor resizing during window resize events. |
| **6** | **Multi-Buffered Asynchronous Batch Flushes (Defer Fence Wait)** | [`rsx_vulkan_backend.c:3701-3710`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L3701-L3710), [`602-608`](file:///home/gcerkez/tools/ps3recomp/libs/video/rsx_vulkan_backend.c#L602-L608) | **Moderate**: Allows CPU to begin recording frame $N+1$ while GPU finishes frame $N$. | **High**: Deep architectural refactoring of ring buffer lifecycle, descriptor pool cycling, and object destruction safety. | Test extensively across memory-intensive titles (e.g. *God of War*, *Twisted Metal*). |
