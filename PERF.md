Data-Oriented Design for Real-Time Vulkan Ray Tracing (Compute Shader, GLSL)
Introduction

Real-time ray tracing on modern consumer GPUs (NVIDIA RTX and AMD RDNA) demands data-oriented design to fully exploit parallel compute capability. A data-oriented approach means organizing memory layout and algorithms to maximize GPU throughput and minimize divergence. This report explores key strategies for implementing a Vulkan/GLSL compute-shader ray tracer with high GPU utilization. We focus on:

Struct-of-Arrays vs Array-of-Structs (SoA vs AoS) memory layouts

GPU-friendly BVH/TLAS acceleration structures (for efficient ray intersections)

Wavefront-coherent scheduling (breaking the ray tracing pipeline into GPU-friendly kernels)

Memory access patterns that reduce warp divergence and improve cache usage

Coherent ray traversal and shading techniques in GLSL compute

We include comparisons, example layouts, and references to open-source projects and research. Emphasis is on real-time game use-cases – e.g. hybrid rendering where rasterization provides primary visibility (G-buffer) and compute shaders handle secondary rays
stackoverflow.com
– and ensuring methods are practical on GPUs with hardware ray-tracing cores disabled or not used.

SoA vs AoS: Struct-of-Arrays vs Array-of-Structs

Data layout heavily influences memory coalescing and cache efficiency on GPUs. In an Array-of-Structs (AoS), each element (e.g. a ray or triangle) is a struct with multiple fields, stored sequentially. In Struct-of-Arrays (SoA), each field is stored in a separate contiguous array of values. Figure 1 summarizes the difference:

Array of Structures (AoS)	Structure of Arrays (SoA)
Stores data as an array of composite structs.
Example: struct Sphere { Vec3 center; float radius; } spheres[N];	Stores each field in a separate array of same length.
Example: Vec3 center[N]; float radius[N];
github.com

Simple to index and use in code (natural mapping of objects).	Requires managing multiple parallel arrays (one per attribute).
Potential GPU issue: Threads access struct fields in a strided pattern, which can prevent fully coalesced memory reads
forums.developer.nvidia.com
. If struct size isn’t a power-of-two, accesses may span extra cache lines.	GPU benefit: Threads accessing the same field across many elements read contiguous memory, enabling coalesced 128-byte transactions (for 32 threads)
forums.developer.nvidia.com
. Each field can be fetched in vectorized loads.
Often loads more data than needed (e.g. reading an entire struct when only one field is used for a computation).	Can load just the needed fields. Reduces cache waste and leverages SIMD within warps (e.g. loading 4 floats at once via vec4 aligns with 16-byte boundaries
jacco.ompf2.com
).
Example: Two float3 vectors (24 bytes) per struct (ray origin & direction) mean warp accesses are misaligned – 24×32=768 bytes per 32-thread warp, which cannot map cleanly to 128-byte cache lines
forums.developer.nvidia.com
forums.developer.nvidia.com
.	Example: Six separate float arrays (origin.x, y, z and dir.x, y, z) allow each warp to read 32 floats (128 bytes) from one array in a single coalesced transaction
forums.developer.nvidia.com
. Alignment is achieved by using 4-byte elements (floats) or padding to 16 bytes as needed.

In practice, SoA layouts often yield better GPU performance. A recent optimized CUDA path tracer lists converting AoS to SoA as one of its most impactful optimizations
github.com
. By packing data per attribute, the memory access pattern becomes regular and cache-friendly. For instance, switching from an AoS Sphere struct to an SoA structure (separate arrays for centers and radii) improved caching so much that the working set fit almost entirely in L1 cache
github.com
github.com
. SoA can eliminate many divergent or unaligned accesses, thereby “making the GPU scream” in terms of memory throughput
forums.developer.nvidia.com
forums.developer.nvidia.com
.

That said, SoA can add complexity. Managing multiple arrays (especially for complex structures) and ensuring alignment requires careful implementation. In some cases (especially if all fields are always used together), AoS may be only marginally slower (one OptiX test found AoS ~2.5% slower than SoA) and is more convenient
forums.developer.nvidia.com
. An intermediate approach is padding or grouping fields to align with 16-byte or 128-byte boundaries. For example, storing a 3-component vector as a float4 (with an unused w) aligns each vector to 16 bytes, allowing a single 128-bit load for the vector
jacco.ompf2.com
. This trades a bit of memory for improved throughput. NVIDIA’s OptiX Prime (a ray casting library) uses AoS with two float3’s (origin, direction) per ray for convenience, but developers note that this format is not naturally 128-byte aligned for 64-thread wavefronts on AMD GPUs
forums.developer.nvidia.com
forums.developer.nvidia.com
. The recommendation is often to use SoA or pad to float4 to ensure coalesced accesses
forums.developer.nvidia.com
forums.developer.nvidia.com
.

Key takeaway: Organize ray and scene data in memory so that each GPU thread reads contiguous chunks of memory. Structure-of-Arrays is usually superior for feeding the GPU, enabling contiguous loads and better cache reuse, whereas Array-of-Structs can cause scattered accesses and wasted bandwidth on unaligned data
forums.developer.nvidia.com
.

GPU-Friendly BVH and TLAS Structures

Efficient ray tracing relies on acceleration structures (BVHs – Bounding Volume Hierarchies – and TLAS – Top-Level AS for instancing) that are optimized for GPU memory patterns. A naïve CPU-style pointer-heavy BVH is not GPU-friendly: chasing pointer references to child nodes leads to non-coalesced, random memory access across threads
raytracey.blogspot.com
. Instead, GPUs prefer “coherent, memory-aligned data structures such as indexable arrays”
raytracey.blogspot.com
. The strategy is to flatten the BVH into arrays and use indices instead of pointers, so traversal becomes cache-friendly and vectorizable.

Flat array of nodes: A common approach stores all BVH nodes in a single contiguous array (often in depth-first or breadth-first order). Child references are stored as integer indices (or offsets) into this array
raytracey.blogspot.com
. This way, a warp of threads traversing nearby parts of the tree will likely access nodes that are close in memory, leveraging spatial locality. Ray Tracey’s GPU path tracing tutorial emphasizes this: “BVH data (nodes, triangles, etc.) are stored in flat one-dimensional arrays... easily digested by CUDA”
raytracey.blogspot.com
. These arrays can even be bound as GPU textures (on older GPUs) to take advantage of texture caching
raytracey.blogspot.com
, though on modern architectures plain global memory is also cached.

Compact node format: To maximize traversal speed, BVH nodes are typically kept small and aligned. A common format is 32 bytes per node – exactly one cache line on many GPUs
raytracey.blogspot.com
. This 32-byte “cache-friendly BVH node” might contain: two child indices (or triangle range info for leaves), two 3D bounding box min/max vectors (quantized or partial precision), and a few bits for flags (leaf vs inner, etc.)
raytracey.blogspot.com
raytracey.blogspot.com
. By using a union or bit-packing, the same node struct can represent either an internal node or a leaf, distinguished by a flag bit
raytracey.blogspot.com
. For example, one design uses a union where for an inner node the struct holds leftIndex, rightIndex, and for a leaf the same bytes hold firstTriangleIndex, triangleCount (with a high bit flag set in triangleCount to indicate “leaf”)
raytracey.blogspot.com
raytracey.blogspot.com
. This ensures all nodes are uniform in size and stored in one array, which is ideal for GPU memory access patterns.

Triangle data: Triangles (or primitives) referenced by the BVH are also laid out in GPU-friendly form. An array of triangle vertex data can be accompanied by parallel arrays of precomputed geometry data (e.g. edges, normals) for faster intersection tests
raytracey.blogspot.com
raytracey.blogspot.com
. Many implementations precompute intersection coefficients or plane equations and store them in SoA form – trading a bit of memory for reducing per-ray computation
raytracey.blogspot.com
. This again follows the data-oriented mantra: do cheap memory fetches instead of expensive math if memory can keep up (on GPUs, arithmetic is plentiful but memory bandwidth is often the bottleneck
raytracey.blogspot.com
).

Top-Level Acceleration Structures (TLAS): In dynamic scenes or instances, a TLAS is a BVH over instances (objects), where each leaf points to a Bottom-Level BVH (BLAS) of a mesh. For a Vulkan compute shader ray tracer (not using the hardware RT pipeline), you can implement a TLAS similarly to a BVH: as an array of TLAS node structs with bounding boxes and child indices. Each TLAS leaf stores an instance index and maybe a pointer to the instance’s BLAS or an index into a BLAS list
jacco.ompf2.com
jacco.ompf2.com
. For example, Jacco Bikker’s BVH series describes maintaining an array of BVHInstance (with transform matrix and BLAS reference) and building a TLAS over those instances
jacco.ompf2.com
. The instance transform can be used to transform rays into object space before traversing the BLAS. In data-oriented fashion, all instance data is packed in arrays (instData in Jacco’s code) and the TLAS nodes in another array (tlasData)
jacco.ompf2.com
jacco.ompf2.com
. Updating or rebuilding the TLAS each frame (if instances move) can be done on the CPU or GPU; either way the updated tlasData buffer must be copied/uploaded each frame before ray queries
jacco.ompf2.com
.

Memory alignment and padding: When defining the BVH node structs in GLSL or similar, it’s important to respect alignment rules. For instance, a vec3 in GLSL or a float3 in C might be aligned to 16 bytes (padding with an unused component) in GPU memory
jacco.ompf2.com
. This means a naive struct with two float3’s and two ints might unexpectedly become 32 or 48 bytes due to padding. The solution is to either use explicit padding or switch to vec4 for alignment, or split the struct. Jacco’s GPU BVH notes encountered this with a 32-byte node containing two float3 (min and max) and two 32-bit packed fields – it had to ensure the float3 were exactly 12 bytes with the packing he expected
jacco.ompf2.com
. Always verify GPU-side struct sizes and use std140-like alignment rules or manually pad to ensure your buffer structs match between CPU and GPU.

Example – Data Setup: To illustrate, an example scene setup for a Vulkan compute ray tracer might do the following
jacco.ompf2.com
jacco.ompf2.com
:

Allocate GPU buffers for: triangle vertex data (triData), extra triangle data (triExData for precomputed normals, etc.), texture pixels (texData), BLAS nodes (bvhData), BLAS triangle index list (idxData mapping node leaves to triangle indices), instance list (instData), and TLAS nodes (tlasData). Each is filled from CPU-built data structures.

Copy all these buffers to the GPU once (BLAS and static data) or every frame as needed (TLAS if updated)
jacco.ompf2.com
. Then, bind them as SSBOs or shader storage buffers in the compute shader.

At runtime, the compute shader has access to all these arrays. Traversal functions take pointers (buffer offsets) to the BVH node array and triangle arrays, and use indices for navigation. For example, a ray traversal in GLSL might use an explicit stack (in local thread memory) of node indices. It will push child indices onto the stack (as integers) instead of pointers, then index into bvhNodeData array to fetch child node bounds
raytracey.blogspot.com
raytracey.blogspot.com
. Because these nodes are contiguous in memory and 32-byte aligned, neighboring threads likely fetch adjacent nodes, resulting in good memory coalescing across the warp.

Open-source projects like NVIDIA’s RTXMU and RadeonRays implement GPU-friendly BVHs with similar principles (flat arrays, compact nodes, indirection through indices). Even CPU ray tracers like Embree align their BVH nodes to 32 bytes for cache efficiency
pbr-book.org
jacco.ompf2.com
, and use packet traversal techniques that are analogous to what GPUs achieve with warp execution. The consensus is that a well-designed acceleration structure for GPU uses contiguous arrays and avoids pointer chasing, leveraging bulk memory operations and SIMD-friendly layouts
raytracey.blogspot.com
raytracey.blogspot.com
.

Wavefront-Coherent Work Dispatch (Megakernel vs. Multi-Stage)

A crucial strategy for GPU ray tracing is how the workload is scheduled: monolithic megakernel vs. wavefront (streaming) approach. A megakernel is a single giant shader that performs all steps of ray tracing (ray generation, BVH traversal, shading, next-ray generation, etc. in one loop). A wavefront approach breaks the algorithm into multiple kernels launched in sequence (or pipelined) – typically one kernel for each major stage of the ray tracing pipeline
jacco.ompf2.com
jacco.ompf2.com
.

Megakernel (single-kernel): This approach keeps all logic in one GPU kernel. It was natural in early GPU path tracers (e.g., small CUDA demos) but has pitfalls for complex scenes. Because GPUs execute threads in SIMT fashion (Single-Instruction, Multiple-Thread), diverging code paths cause some threads in a warp to idle. A megakernel tends to branch a lot – e.g., if some rays hit lights and terminate, others continue bouncing, or different materials require different shading calculations. This divergence means warps are under-utilized, hurting performance
research.nvidia.com
. Moreover, a huge kernel doing everything might use many registers (for material shading, traversal stacks, random sampling, etc.), which limits how many warps can reside on a SM (reducing occupancy and latency hiding)
research.nvidia.com
research.nvidia.com
. As NVIDIA researchers noted, “simply porting a large CPU program into an equally large GPU kernel is almost certain to perform badly” due to these factors
research.nvidia.com
. Real-world scenes with many material types especially suffer, as a monolithic shader must handle all materials and thus incurs either deep branches or lots of unused code per thread
research.nvidia.com
.

Wavefront Path Tracing (multi-kernel): The wavefront strategy, sometimes called streaming, addresses those issues by splitting the work. A classic design uses separate kernels for: (1) Ray Generation, (2) Extension/Intersection, (3) Shading, and (4) Shadow rays (light connections)
jacco.ompf2.com
jacco.ompf2.com
. These kernels run in a loop for each path depth. For example
jacco.ompf2.com
jacco.ompf2.com
:

Generate – launch N threads to generate primary ray directions/origins for N pixels. Store these rays in a large array (ray pool).

Intersect (Extend) – launch threads to read the ray pool and traverse the BVH, finding the nearest hit for each ray. Write out hit results (hit distance, material ID, primitive ID, etc.) to an intersection result array
jacco.ompf2.com
.

Shade – launch threads to read the hit results and perform shading computations. For each hit, determine if the path continues (e.g., bounce off a reflective surface or refract) or ends. Generate new rays for continuing paths and write them to a new ray buffer. Also, generate shadow rays for lights as needed and write to a separate buffer
jacco.ompf2.com
.

Shadow (Connect) – launch threads to traverse shadow rays through the BVH (these only need any-hit tests) and report whether each light sample is visible
jacco.ompf2.com
. This updates lighting contributions in the shading results.

After these four, you have a list of extension rays for the next bounce (from the Shade kernel)
jacco.ompf2.com
. The process repeats: feed those as the input to the next Intersect pass, and so on, until no rays remain or a max depth is reached
jacco.ompf2.com
. Finally, the accumulated color contributions are written to the image.

This streaming pipeline keeps threads more coherent. During each kernel, all threads execute the same type of work (no mixture of traversal vs shading in the same kernel)
jacco.ompf2.com
. For example, in the Intersect stage every thread is doing BVH traversal, which follows roughly the same instruction path (until hitting different geometry). In the Shade stage, every thread runs the shading code; divergence can occur if materials differ, but we no longer intermix traversal logic here – the heavy divergence between traversal and shading is eliminated
research.nvidia.com
research.nvidia.com
. This improves SIMT efficiency significantly in scenes with many material types
research.nvidia.com
.

Occupancy and load balancing: Wavefront methods also solve the diminishing parallelism problem. In a path tracer, as bounces increase, many rays terminate and a megakernel would experience “warp starvation” (some warps become inactive as threads terminate). The wavefront approach naturally compacts work: after each stage, you know exactly how many rays continue. You then launch the next kernel with exactly that many threads (or a bit more, rounding up to whole warps)
jacco.ompf2.com
. All warps in that next kernel launch will be fully active with useful work. In essence, you compact active rays between stages (often using an atomic counter or prefix sum to pack valid rays)
jacco.ompf2.com
jacco.ompf2.com
. Jacco’s implementation notes that by the time you loop back to the Intersect stage for secondary rays, you have a “compacted ray buffer, guaranteeing full occupancy when the kernel starts”
jacco.ompf2.com
. No threads are assigned to “missing” rays – any rays that terminated are simply not in the buffer, rather than sitting idle in a warp.

Reduced register pressure: Another benefit is kernel specialization. Each smaller kernel can be optimized in isolation. For example, the BVH intersection kernel can be kept lean – it might use a small stack, or even be entirely stackless, and use few registers
jacco.ompf2.com
. This means more warps can be in flight on the SMs, hiding memory latency better
research.nvidia.com
research.nvidia.com
. Meanwhile, the shading kernel can use more registers for BSDF sampling, without bloating the register usage of the traversal code. In other words, “each kernel can use all available GPU resources (cache, shared memory, registers) without taking into account other kernels”, and a heavy register user (shading) doesn’t force the lighter intersection code to run with lower occupancy
jacco.ompf2.com
. NVIDIA’s researchers found that separating material evaluation from traversal in this way was crucial for complex, real-world materials in production scenes
research.nvidia.com
research.nvidia.com
.

Trade-offs: The wavefront approach isn’t free. It incurs overheads: multiple kernel launches per frame (or per bounce) add a small scheduling cost
jacco.ompf2.com
, and storing intermediate results (ray lists, hit results, etc.) in global memory means more memory I/O
jacco.ompf2.com
research.nvidia.com
. In our example, we maintain several large buffers (rays, hits, shadow rays) that can total hundreds of MB for high resolutions
jacco.ompf2.com
jacco.ompf2.com
. However, GPUs are designed for throughput and can often tolerate this streaming of data
jacco.ompf2.com
jacco.ompf2.com
. Memory is abundant on modern GPUs (using a few hundred MB for ray buffers is usually acceptable on a 8–12GB card)
jacco.ompf2.com
, and the access patterns are mostly linear (good for caching and burst transfers). Additionally, using asynchronous compute or overlapping compute and memory operations can hide some of the buffer I/O cost. The consensus from both industry and research is that the gains in efficiency far outweigh these costs for non-trivial ray tracing scenarios
research.nvidia.com
jacco.ompf2.com
. Blender’s Cycles GPU engine, for instance, migrated from a megakernel to a wavefront (multi-kernel) design to better handle different light integrators and materials, seeing significant speedups on GPUs (as discussed in NVIDIA’s “Megakernels Considered Harmful” research
research.nvidia.com
).

Comparison Summary: The following table compares megakernel and wavefront approaches for a GPU ray tracer:

Scheduling Approach	Advantages	Drawbacks
Megakernel (One Kernel Does All)	– Simple control flow (all in one shader).
– No need to store intermediate data in global memory between stages.	– Severe warp divergence when threads follow different paths
research.nvidia.com
.
– High register usage (occupancy loss) if kernel handles many tasks
research.nvidia.com
.
– Difficult to optimize per task; one hot spot (e.g., complex material code) can bottleneck the entire kernel
research.nvidia.com
.
Wavefront (Multiple Kernels in Sequence)	– Maximized occupancy: Each kernel works on compacted sets of rays, so all threads do useful work
jacco.ompf2.com
.
– Better coherence: Threads in each stage run identical algorithms (improves SIMT efficiency)
jacco.ompf2.com
.
– Easier optimization: separate kernels for traversal, shading, etc., tailored for performance (e.g., intersection kernel uses few registers, enabling more warps)
jacco.ompf2.com
.
– Naturally pipelines different ray types (e.g., shadow vs primary) for specialized handling.	– Overhead of extra kernel launches and synchronization
jacco.ompf2.com
.
– Requires global memory buffers to pass data between kernels (ray pools, hit lists)
research.nvidia.com
.
– More complex to implement (managing buffers, counters, CPU-GPU coordination of kernel sequence)
jacco.ompf2.com
.

For real-time game use-cases, a full path tracer might not be necessary, but a wavefront-style approach can still be applied to, say, one-bounce reflections or shadows. For example, a game might cast one ray per pixel for glossy reflections: it could use one compute dispatch to generate rays from the G-buffer, another to intersect the scene, and a final one to shade/accumulate the results. This is essentially a truncated wavefront pipeline (no secondary bounces). The coherence benefits (all threads in one stage doing the same work) still apply, and the complexity is manageable. Indeed, research has shown that even for a single-bounce ray tracing (shadows, AO, etc.), grouping rays and processing them in stages improves GPU utilization compared to launching a giant kernel per pixel
reddit.com
jacco.ompf2.com
.

In summary, wavefront (streaming) scheduling is a natural fit for GPUs
jacco.ompf2.com
. It aligns with the GPU’s strength in throughput computing. By keeping a large “wave” of rays in flight and advancing them in lockstep through each stage, we keep the hardware busy and minimize wasted cycles on divergence. As Laine et al. titled their paper: “Megakernels Considered Harmful” for GPU ray tracing
research.nvidia.com
– the evidence overwhelmingly favors a multi-kernel, coherent approach for complex workloads.

Memory Access Patterns and Divergence

Optimizing memory access is paramount to achieve real-time performance. Even with an ideal data layout, poor access patterns or thread divergence can underutilize memory bandwidth. Here are strategies to maximize memory throughput and reduce divergence:

Coalesced Access: As mentioned earlier, coalescing means threads in the same warp (or wavefront) access contiguous memory addresses that can be served by as few memory transactions as possible. Always strive for patterns where thread i accesses array element i (or i + constant). For instance, when fetching ray data in SoA form, thread 0 loads origin_x[0], thread 1 loads origin_x[1], ..., thread 31 loads origin_x[31]. The hardware can combine that into one 128-byte fetch from global memory
forums.developer.nvidia.com
. If instead each thread loads a struct of 24 bytes, the addresses do not line up neatly with 128B segments, causing multiple loads and partially used cache lines
forums.developer.nvidia.com
forums.developer.nvidia.com
. Aligning data sizes to the warp width is critical. Using float4 (16 bytes) instead of float3 (12 bytes) for vectors is a common trick to ensure alignment
jacco.ompf2.com
. In a wavefront path tracer, Ray and Hit structures are often padded to 16 bytes for this reason
jacco.ompf2.com
.

Memory Alignment and Padding: Align frequently accessed arrays to at least 128 bytes. Vulkan’s memory allocations are typically aligned well, but when using SSBOs or structured buffers, consider adding padding in structs so that the overall struct size is a multiple of 16 or 32 bytes. This way, each struct starts at a cache-friendly boundary. As an example, storing ray origins and directions as four floats each (XYZ + padding) means each ray is 32 bytes; a group of 4 rays is 128 bytes. A warp of 32 threads can load 8 such rays in 8 transactions, etc. If alignment is off, the GPU might need 9 or 10 transactions for the same data, wasting bandwidth.

Favor Sequental Access: Step through arrays linearly whenever possible, rather than strided or random patterns. BVH traversal by nature is somewhat random (jumps to child nodes), but our flattened BVH mitigates this by making nearby nodes contiguous. Additionally, when rays are coherent (see next section), nearby threads often traverse similar BVH regions, resulting in localized memory accesses. One should also utilize ray sorting or scheduling to improve memory locality (discussed below under Coherent Traversal). Another example: when outputting results (like writing to a pixel framebuffer or an accumulation buffer), write in a linear order or in tiles to ensure good caching. Writing color results by thread ID (which maps to pixel index) is generally fine since threads are usually launched in screen-order.

Leverage Caching (L1/L2): Modern GPUs have on-chip caches that can significantly speed up repeated accesses. The NVIDIA RTX architecture, for example, has an L1 cache per SM and a large L2 cache. If your memory access pattern has locality (temporal or spatial), the caches will reduce DRAM bandwidth use. Data-oriented design helps here: by compacting active rays, you improve locality (processing a smaller “working set” of rays at a time). Also, by using SoA, you might bring only the needed array into cache. Profiling of a CUDA path tracer showed an “almost perfect cache hit rate” after optimizing memory layout
github.com
. This came from structuring data such that once a ray’s data or a BVH node is loaded, many threads use it quickly (before it evicts from cache). Tip: Group memory accesses together. For example, in a BVH traversal, test the two child bounding boxes first (fetch their data in one go), then decide which to traverse
stackoverflow.com
stackoverflow.com
. This way you read two AABBs (perhaps 2×32 bytes) and keep them in registers, rather than branching and coming back later to fetch the second child’s data from memory. Even better, if using SIMD instructions (or warp-level parallelism), load both child nodes’ bounds in parallel. Some CPU techniques (packet traversal, SSE/AVX intersection of 4 boxes at once) have GPU analogues: on GPU you might have each thread handle one ray, but you can still load 128-bit at a time which often grabs multiple floats from memory.

Minimize Memory Divergence: Memory divergence occurs when threads in a warp access memory in very different locations (causing multiple cache line fetches). SoA helps as noted, but you should also avoid per-thread irregular access when possible. For example, if each thread needs to fetch a material from a list but materials are scattered, you can mitigate this by sorting rays by material ID before shading (so that warps tend to fetch only a few materials at a time). This is a form of ray classification or sorting, which can dramatically improve cache reuse. A 2023 study on ray classification found that grouping rays by similar characteristics (origin sector, direction, etc.) reduced BVH traversal steps by ~42% on average and improved total intersection time ~15%
meistdan.github.io
– essentially because rays in the same group visited many of the same nodes, benefiting from cached nodes.

Use Shared Memory for Reuse: If an algorithm allows, you can use shared memory (tile-local memory) to hold data that multiple threads in a workgroup will reuse. Classic example: if you cast many shadow rays towards a light, and many rays hit the same area of a large triangle, one might cache that triangle’s data in shared memory. However, in ray tracing this is less common because ray coherence is usually not high except for primary rays. Still, in tasks like reflection blurring or ambient occlusion, where many rays start from the same pixel or area, a workgroup could cooperatively load common data. Another use: while building BVHs on GPU, shared memory is used to sort and partition triangles quickly. For traversal, shared memory might hold a stack of tasks for persistent threads (an advanced strategy beyond scope).

Avoid Unnecessary Memory Traffic: This sounds obvious, but it’s easy to accidentally introduce extra copies or suboptimal structures in Vulkan compute. For example, if you have data in a Vulkan buffer that the shader can access directly, avoid copying it to another buffer unless needed. Use pointer (device address) or index indirection to refer to existing data instead of duplicating it. Also prefer 32-bit indices over 64-bit for device memory pointers when possible, to cut down bandwidth (e.g., use a uint index into a array instead of a 64-bit device address for each node – that halves the data size for child references).

In practice, one should profile and monitor memory throughput. Tools like NVIDIA Nsight or AMD Radeon GPU Profiler can show if your kernel is memory-bound (low ALU utilization, many memory stalls). If so, applying the above patterns can often raise occupancy and throughput. The goal is to feed the GPU with data as efficiently as possible: contiguous, aligned, and without making different threads wait on each other’s memory accesses.

Coherent Ray Traversal and Shading Strategies in GLSL

Coherence in ray traversal and shading refers to making rays behave as similarly as possible, to minimize divergence and maximize data reuse. Several strategies can help achieve this, even for secondary rays which tend to be incoherent:

Front-to-Back Traversal Order: Ensure your BVH traversal checks the nearer child first, so that if a hit is found, you can terminate early and skip far nodes
stackoverflow.com
stackoverflow.com
. This is a standard practice (and in Vulkan’s ray tracing API, the hardware does this automatically using a “traveler” that sorts two child distances). In a custom compute shader, you should implement this logic. As one optimization expert pointed out, traversing in ray order (front-to-back) can significantly cut down unnecessary intersections – they saw ~15% speedup just by doing this in a CPU tracer
stackoverflow.com
stackoverflow.com
. For GPUs, this not only saves computation but also reduces memory accesses (skips loading distant nodes once a hit is found closer) and keeps divergence lower (threads in a warp are likely to agree on which child is closer, if rays are somewhat coherent). Be mindful to update the ray’s t_max (maximum distance) when a hit is found, so that other threads can also benefit by skipping further-away nodes
stackoverflow.com
stackoverflow.com
. Front-to-back traversal is essentially a form of on-the-fly ray coherence: it makes each ray’s own traversal more efficient and in warps where rays travel through the same region, they will prune the BVH similarly.

Ray Batching and Sorting: Group rays that are originating from close points or going in similar directions. This is easier for primary rays (they originate from the camera – you can trace them in screen-space order which already groups neighboring directions) and for shadow rays (often groups of shadow rays go towards the same light). For diffuse bounce rays which go in random directions, some research suggests sorting them by direction octants or by origin cells (space partitioning) after they are generated, before casting them
meistdan.github.io
. Doing a full sort every bounce might be too costly for real-time use, but even a coarse classification (splitting rays into a few bins) can improve coherence. For instance, you could bucket secondary rays into 4–8 direction ranges (e.g., using a Morton code on direction vector or octahedral mapping) and then trace each batch sequentially. Each batch will have rays that are more likely to traverse similar BVH regions, meaning warps in those dispatches experience less divergence. This comes at the cost of some scheduling complexity and buffer shuffling, so it’s a trade-off – more beneficial when ray counts are high and incoherent (e.g., many AO rays).

Stream Compaction: This goes hand-in-hand with wavefront scheduling: after each kernel, compact the “alive” rays. In GLSL compute, you might do this with an atomic counter or prefix sum as rays are produced in the Shade stage. The result is that memory buffers (ray lists) are always dense – warps run over a contiguous range of active rays. This eliminates gaps where threads would have no work
jacco.ompf2.com
. The benefit is most pronounced after several bounces when many rays have terminated. Compaction ensures that remaining rays still utilize full warps. Any threads that would have been idle are simply not launched in the next pass, improving overall efficiency.

Branchless or Minimally Branching Shading: Shading in a ray tracer can introduce major divergence – e.g., a switch(materialType) in a shader means threads with different materials will follow different branches. One strategy is to minimize divergent branches in shading code. This can be done by moving branches “outside” the kernel (e.g., wavefront sorting by material as mentioned, so one batch shades only one material at a time), or by using mathematical trickery to do work in a branchless way. The CUDA path tracer example achieved “warp efficiency” by eliminating divergent branching in material sampling
github.com
github.com
. They likely employed techniques such as: using bit masks and mix operations to blend results from different BRDF models instead of if/else, or structuring the shader so that all threads execute a unified uber-shader (covering multiple materials) with certain terms zeroed out for some threads. While branchless code can execute extra operations (for the “wrong” material in some threads), it avoids warps serializing divergent branches. On modern GPUs with fast ALUs, sometimes doing a bit of extra math is cheaper than a divergent branch. GLSL doesn’t have function pointers or virtual functions, so implementing polymorphic materials often ends up in a big switch. To avoid divergence, you could split materials into categories and have separate shading kernels per category in a wavefront fashion (if one type of material is particularly heavy), or use a single shade kernel that handles multiple types in one go but with conditional moves instead of branches.

Temporal Coherence and Reuse: Although not purely a “data layout” strategy, in real-time applications you can exploit temporal coherence to improve performance. For example, reusing rays or reprojecting results from previous frames can reduce the number of rays you need to trace anew. Techniques like ReSTIR (for global illumination) or temporal denoising reduce per-frame ray counts, easing the load on the ray tracing system. A lower ray count can mean you can afford more coherence-improving steps (like sorting) on those rays. If, say, only 1/4 of pixels shoot a new ray on a given frame (others reuse last frame’s result), those new rays can be more carefully orchestrated for coherence.

Use of Hardware Features: Vulkan’s ray tracing extensions (RT cores on RTX, or hardware BVH traversal on RDNA2+) inherently handle some coherence optimizations under the hood – the hardware units traverse very efficiently and manage diverging rays at the HW level. If you use pure compute shaders (no RT cores), you’re doing this in software, but you might still leverage hardware features like subgroup operations. Subgroup (warp-level) intrinsics in GLSL can let threads exchange data or vote on conditions. You could use these to have a warp collectively decide on traversal steps or to share a common computation. For instance, a warp could collectively compute an intersection for a set of rays with the same triangle (though such coincidences are rare except for primary rays in certain cases). More practically, subgroup ops can be used for compaction (e.g., ballot to get a mask of active rays in a warp, then compress) or for parallel reduction (finding the closest hit among threads – but again, rays diverge so that’s tricky).

Explicit SIMD Traversal (Packet Tracing): On CPUs, packet tracing processes a bundle of rays together with SIMD instructions; on GPUs, SIMT is essentially doing that at the warp level. For coherent rays (like primary camera rays), you can treat a warp as a “packet” – all 32 rays traverse together. If they diverge, one approach is persistent packets (some threads idle while others catch up). GPU hardware will anyway mask off threads that don’t follow a branch. There has been research on cooperative packet traversal on GPU where threads in a warp collaborate on one ray’s traversal to improve memory locality
dl.acm.org
, but that’s more experimental and not widely used in real-time graphics. Generally, one ray per thread is maintained for flexibility. However, primary rays in a pinhole camera are extremely coherent – all starting from the eye and going in similar directions to neighboring pixels. These are trivial to schedule coherently (no need for special tricks aside from maybe tiling the image to match cache). The real challenge is secondary rays which the above strategies (sorting, compaction, wavefront) attempt to tame.

To illustrate coherent shading in a practical sense: imagine a scene with some diffuse and some reflective objects. A naive shader might do:

if(matType == DIFFUSE) { /* sample random hemisphere, etc. */ }
else if(matType == MIRROR) { /* reflect ray direction */ }


If a warp has a mix of diffuse and mirror hits, half the threads will diverge at any given branch. A more coherent approach could be: after intersection, split the hit list into two lists – diffuse hits and mirror hits – using stream compaction or sorting by material ID. Then invoke two separate shade kernels, one for each material type (or each list)
jacco.ompf2.com
. In each of those kernels, all threads execute the same branch (so no divergence). This is an explicit wavefront separation by material. It might or might not be worth the overhead depending on scene and material distribution (if there are many types, you can’t practically make one kernel per type each bounce). But it highlights the principle: process similar tasks together. In a game, you might at least separate opaque vs transparent rays, or direct lighting vs indirect, etc., if those have very different costs.

GLSL specifics: Writing a ray tracer in GLSL compute means working within certain constraints: no recursion (use loops and your own stack), no dynamic memory (use fixed-size arrays in shared or private memory for stacks), and limited ways to sync threads (only within a local workgroup, not globally, unless you end the dispatch). This typically means each ray is handled entirely by one thread from start to finish (for that bounce) in the compute shader. You will implement BVH traversal as a while loop, possibly using an array as an explicit stack
github.com
. Ensure this stack is in local memory (per-thread) or registers, not in global memory, to avoid slow memory accesses for push/pop. The example from the CUDA tracer used a small fixed-size array stack[16] in device function to traverse up to 16 levels deep
github.com
. That eliminates recursion and keeps the stack in fast memory. In GLSL, you can do similarly with a int stack[64]; int stackPtr = 0; (size 64 is usually safe for BVH depth) living in the shader function. This is a data-oriented tweak as well – by managing your own stack, you can control memory placement and avoid using deeper system stacks or function calls. It also lets you potentially merge traversal across rays if you ever attempt packet traversal (but again, not common in compute shaders).

One more GLSL tip: use invariant calculations and move them out of inner loops. For instance, if you can compute something per ray once and reuse in traversal, do so. Compute shader implementations often move ray data into registers (e.g., precompute ray inverse direction and sign bits for slab tests) before the traversal loop, so that each node intersection test is fast
stackoverflow.com
stackoverflow.com
. This reduces divergence in the sense that all threads do this setup similarly, and then their inner loops are simpler.

Finally, robustness: ensure your traversal algorithm handles all rays consistently (e.g., treat misses the same way) so you don’t get rare divergent long-running threads (which can cause GPU timeouts if one warp takes significantly longer). Use a fixed maximum depth or Russian roulette to cap path length – this not only is standard for path tracing but also maintains a bound on how much work any thread will do, which is important in real-time settings.

References and Open-Source Projects

Throughout this report, we’ve cited several references that provide more detail and examples:

Karim Sayed’s CUDA Ray Tracing – An optimized “Ray Tracing in One Weekend” implementation
github.com
github.com
, demonstrating the impact of SoA layouts, explicit stacks, and other GPU optimizations. It achieved >200× speedup over a naive port by applying these techniques
github.com
. Although in CUDA, many lessons transfer to Vulkan GLSL. The project’s README and blog post detail each optimization step, including memory alignment and eliminating divergent material branches
github.com
.

Jacco Bikker’s Blog – especially “Wavefront Path Tracing”
jacco.ompf2.com
jacco.ompf2.com
and “How to Build a BVH – To the GPU”
raytracey.blogspot.com
jacco.ompf2.com
. Bikker’s posts walk through converting a CPU path tracer to GPU (first in OpenCL, which is analogous to Vulkan compute). He discusses data setup, alignment issues, and the switch to a streaming approach. The wavefront article gives a pragmatic view of implementing multiple kernels and addresses common concerns (like kernel launch overhead and buffer size) with measured reasoning
jacco.ompf2.com
jacco.ompf2.com
. It’s a highly recommended read for anyone building a GPU path tracer from scratch.

“Megakernels Considered Harmful” (Laine et al., NVIDIA 2013) – a research paper comparing one-big-kernel vs. wavefront path tracing in detail
research.nvidia.com
research.nvidia.com
. It provides both rationale and empirical performance data. They show wavefront outperforms megakernel in complex scenes due to better SIMT utilization. Also, they talk about memory layout of path state and queue management optimizations
research.nvidia.com
research.nvidia.com
, which are insightful when implementing your own path state buffers.

Ray Tracey’s GPU Path Tracing Tutorials – A series of blog posts (by Arjan van Meerten) that implement a GPU BVH and path tracer in CUDA
raytracey.blogspot.com
raytracey.blogspot.com
. They are slightly older (Fermi/Kepler era) but still relevant. They show how to pack a BVH into 32-byte nodes, use a union to handle leaves, and even store data in textures for caching
raytracey.blogspot.com
raytracey.blogspot.com
. The tutorial code (on GitHub) is a goldmine of data-oriented GPU coding patterns, including how to handle triangle data, avoid pointer chasing, and perform traversal with a stack in CUDA C. It also demonstrates saving and reusing the BVH across runs (important for iteration when developing).

GPUOpen and NVIDIA Developer resources: AMD’s GPUOpen blog and NVIDIA’s developer blogs/forums contain posts on best practices. For example, NVIDIA’s forums have discussions on SoA vs AoS in OptiX
forums.developer.nvidia.com
forums.developer.nvidia.com
, where even NVIDIA engineers note that making data vectorized (float4) can help, but also caution to consider cache behavior (sometimes AoS is “good enough” if it keeps related data together in the same cache line
forums.developer.nvidia.com
). The thread we cited
forums.developer.nvidia.com
forums.developer.nvidia.com
is enlightening as it shows a developer reasoning through coalescing for millions of rays.

Academic research on ray scheduling: Beyond the above, papers on ray reordering, persistent threads, and stackless traversal can provide deeper insights. For example, “Understanding the Efficiency of Ray Traversal on GPUs” by Aila and Laine (2009) influenced many designs (it introduced the idea of short-stack and packet heuristics on GPU). More recently, “Ray Classification” (2020s) and others revisit how to dynamically improve coherence
meistdan.github.io
. While these may be beyond what a real-time engine can do each frame, knowing the trends can inspire simplified versions in your engine (like grouping rays by rough direction).

Vulkan RT Extensions: Although our focus is compute shaders, Vulkan’s ray tracing extension (RT cores) is worth mentioning. It offloads BVH traversal and triangle intersection to hardware, but the data structures (BLAS/TLAS) still need to be built efficiently. If using it, the data-oriented thinking still applies when preparing geometry for the acceleration structures (e.g., linearizing geometry, aligning vertex buffers, etc.). Khronos’s tutorials note that the Vulkan ray tracing pipeline is a “coherent ray tracing framework” internally
khronos.org
– meaning it is designed to keep rays coherent and use cache-friendly BVH formats. In a way, the hardware is doing for you what we aim to do in a compute shader implementation.

In closing, implementing a real-time ray tracer with Vulkan compute and GLSL is a challenging but solvable task with the right strategies. By organizing data as structure-of-arrays, flattening BVHs into GPU-friendly form, scheduling work in coherent wavefronts, aligning memory accesses, and grouping similar rays together, you can maximize GPU compute utilization. These techniques allow even path tracing algorithms to run interactively
github.com
github.com
. The result is a system that takes full advantage of the parallelism of modern GPUs – achieving impressive real-time ray tracing effects in games and simulations.