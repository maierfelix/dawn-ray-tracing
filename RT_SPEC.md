# WebGPU Ray-Tracing

## Objects

### GPURayTracingAccelerationContainer

Used to encapsulate geometries or geometry instances, based on the assigned level.

#### Methods:

##### destroy
Destroys the acceleration container.

##### updateInstance
Updates the state of an instance inside a Top-Level container.

| Type | Description |
| :--- | :--- |
| Number | The instance id
| [GPURayTracingAccelerationInstanceDescriptor](#GPURayTracingAccelerationInstanceDescriptor) | The Bottom-Level container with the geometry

### GPURayTracingShaderBindingTable

Used to group shaders together which later can be dynamically invoked.

#### Methods:

##### destroy
Destroys the shader binding table.

### GPURayTracingPipeline

Ray Tracing Pipeline.

#### Methods:

*None*

### GPUDevice

This object gets extended with the following methods:

#### Methods:

##### createRayTracingAccelerationContainer:
Returns a new `GPURayTracingAccelerationContainer`.

| Type |
| :--- |
| [GPURayTracingAccelerationContainerDescriptor](#GPURayTracingAccelerationContainerDescriptor)

##### createRayTracingShaderBindingTable:
Returns a new `GPURayTracingShaderBindingTable`.

| Type |
| :--- |
| [GPURayTracingShaderBindingTableDescriptor](#GPURayTracingShaderBindingTableDescriptor)

##### createRayTracingPipeline:
Returns a new `GPURayTracingPipeline`.

| Type |
| :--- |
| [GPURayTracingPipelineDescriptor](#GPURayTracingPipelineDescriptor)

### GPURayTracingPassEncoder

Used to record a Ray-Tracing pass.

#### Methods:

##### insertDebugMarker

| Type | Description |
| :--- | :--- |
| String | Group label for the marker

##### popDebugGroup

*No arguments*

##### pushDebugGroup

| Type | Description |
| :--- | :--- |
| String | Debug group to push

##### setPipeline

| Type | Description |
| :--- | :--- |
| [GPURaytracingPipeline](#GPURaytracingPipeline) | The pipeline to use in this pass

##### setBindGroup

| Type | Description |
| :--- | :--- |
| Number | The bind group index
| [GPUBindGroup](https://gpuweb.github.io/gpuweb/#bind-groups) | The bind group to use in this pass
| [Number] | Array of dynamic offsets

##### traceRays

| Type | Description |
| :--- | :--- |
| Number | The SBT Ray-Generation Group offset
| Number | The SBT Ray-Hit Group offset
| Number | The SBT Ray-Hit Miss offset
| Number | The width of the ray trace query dimensions
| Number | The height of the ray trace query dimensions
| *Number* | The depth of the ray trace query dimensions. Defaults to *1*

##### endPass

*No arguments*

### GPUCommandEncoder

This object gets extended with the following methods:

#### Methods:

##### beginRayTracingPass:
Begin a new Ray-Tracing pass.

| Type |
| :--- |
| [GPURayTracingPassDescriptor](#GPURayTracingPassDescriptor)

##### buildRayTracingAccelerationContainer:
Build a `GPURayTracingAccelerationContainer`. When building a top-level container, all linked bottom-level containers (referenced through [GPURayTracingAccelerationInstanceDescriptor.geometryContainer](#GPURayTracingAccelerationInstanceDescriptor)) must have finished building in a former pass.

| Type | Description |
| :--- | :--- |
| [GPURayTracingAccelerationContainer](#GPURayTracingAccelerationContainer) | The acceleration container to build

##### copyRayTracingAccelerationContainer:
Copies a `GPURayTracingAccelerationContainer` into another `GPURayTracingAccelerationContainer`. The *source* and the *destination* containers must be built before.

| Type | Description |
| :--- | :--- |
| [GPURayTracingAccelerationContainer](#GPURayTracingAccelerationContainer) | The **source** acceleration container to copy from
| [GPURayTracingAccelerationContainer](#GPURayTracingAccelerationContainer) | The **destination** acceleration container to copy into

##### updateRayTracingAccelerationContainer:
Updates an acceleration container. The container must be built and created with the [GPURayTracingAccelerationContainerFlag](#GPURayTracingAccelerationContainerFlag) `ALLOW_UPDATE` flag.

| Type | Description |
| :--- | :--- |
| [GPURayTracingAccelerationContainer](#GPURayTracingAccelerationContainer) | The acceleration container to update

## Bitmasks

### GPURayTracingAccelerationGeometryFlag

| Name | Description |
| :--- | :--- |
| NONE | Invokes Any-Hit and Closest-Hit shaders. Any-Hit shaders may get invoked multiple times for the same triangle
| OPAQUE | No Any-Hit Shader will be invoked for this geometry
| ALLOW_ANY_HIT | Any-Hit Shader will be invoked only once when hitting a triangle

### GPURayTracingAccelerationInstanceFlag

| Name | Description |
| :--- | :--- |
| NONE |
| TRIANGLE_CULL_DISABLE | Disables face culling for this geometry instance
| TRIANGLE_FRONT_COUNTERCLOCKWISE | Indicates that the front face of the triangle for culling purposes is the face that is counter clockwise in object space relative to the ray origin
| FORCE_OPAQUE | Interprets the flags of all referenced geometry containers to be opaque. This behavior can be overridden with the `traceNV` ray flag
| FORCE_NO_OPAQUE | Inverse behavior of `FORCE_OPAQUE`

### GPURayTracingAccelerationContainerFlag

| Name | Description |
| :--- | :--- |
| NONE |
| ALLOW_UPDATE | Allows updating the container's state with `updateAccelerationContainer`
| PREFER_FAST_TRACE | Hint to prefer fast ray tracing for this container
| PREFER_FAST_BUILD | Hint to prefer faster build times for this container
| LOW_MEMORY | Indicates that the container should use less memory, but might causes slower build times

### GPUShaderStage

This bitmask gets extended with the following additional properties:

| Name | Description |
| :--- | :--- |
| RAY_GENERATION | Ray Generation shader (`.rgen`, `raygen`), used to generate primary rays
| RAY_CLOSEST_HIT | Ray Closest-Hit shader (`.rchit`, `closest`), called for the closest surface relative to the ray
| RAY_ANY_HIT | Ray Any-Hit shader (`.rahit`, `anyhit`), called whenever a ray hit occurs
| RAY_MISS | Ray Miss shader (`.rmiss`, `miss`), called whenever a traced ray didn't hit a surface at all
| RAY_INTERSECTION | Ray Intersection shader (`.rint`) - Instead of the default triangle-based intersection, a custom ray-geometry intersection shader can be defined.

### GPUBufferUsage

This bitmask gets extended with the following additional properties:

| Name | Description |
| :--- | :--- |
| RAY_TRACING | Unused yet

## Enums

### GPURayTracingAccelerationGeometryType

| Name | Description |
| :--- | :--- |
| triangles | Set of vertices and optionally indices, used for triangle based geometry
| aabbs | Set of axis-aligned bounding boxes, used for procedural geometry

### GPURayTracingAccelerationContainerLevel

| Name | Description |
| :--- | :--- |
| bottom | Bottom-Level Container, encapsulates geometry
| top | Top-Level Container, encapsulates geometry instances

### GPURayTracingShaderBindingTableGroupType

| Name | Description |
| :--- | :--- |
| general | `.rgen`, `.rmiss`
| triangles-hit-group | `.rchit`, `rahit`
| procedural-hit-group | `.rint`

## Descriptors

### GPUBindGroupBinding

This descriptor gets extended with the following properties:

| Name | Type | Description |
| :--- | :--- | :--- |
| *accelerationContainer* | [GPURayTracingAccelerationContainer](#GPURayTracingAccelerationContainer) | Acceleration container to bind

### GPUTransform3D

| Name | Type | Description |
| :--- | :--- | :--- |
| *x* | Number | X coordinate
| *y* | Number | Y coordinate
| *z* | Number | Z coordinate

### GPURayTracingAccelerationInstanceTransformDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| *translation* | [GPUTransform3D](#GPUTransform3D) | Translation property
| *rotation* | [GPUTransform3D](#GPUTransform3D) | Rotation property
| *scale* | [GPUTransform3D](#GPUTransform3D) | Scale property

### GPURayTracingAccelerationGeometryVertexDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| buffer | [GPUBuffer](https://gpuweb.github.io/gpuweb/#GPUBuffer) | a `GPUBuffer` containing the vertices to be used
| format | [GPUVertexFormat](https://gpuweb.github.io/gpuweb/#enumdef-gpuvertexformat) | The format of the vertices
| stride | Number | Byte stride between each vertex
| *offset* | Number | Starting byte offset in the buffer
| count | Number | Amount of vertices

### GPURayTracingAccelerationGeometryIndexDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| buffer | [GPUBuffer](https://gpuweb.github.io/gpuweb/#GPUBuffer) | a `GPUBuffer` containing the indices to be used
| format | [GPUIndexFormat](https://gpuweb.github.io/gpuweb/#enumdef-gpuindexformat) | The format of the indices
| *offset* | Number| Starting byte offset in the buffer
| count | Number| Amount of indices

### GPURayTracingAccelerationGeometryAABBDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| buffer | [GPUBuffer](https://gpuweb.github.io/gpuweb/#GPUBuffer) | a `GPUBuffer` containing the AABBs to be used
| stride | Number | The stride between each AABB
| *offset* | Number| Starting byte offset in the buffer
| count | Number| Amount of AABBs

### GPURayTracingAccelerationGeometryDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| *flags* | [GPURayTracingAccelerationGeometryFlag](#GPURayTracingAccelerationGeometryFlag) | Flags for this geometry
| *type* | [GPURayTracingAccelerationGeometryType](#GPURayTracingAccelerationGeometryType) | Type of this geometry
| *vertex* | [GPURayTracingAccelerationGeometryVertexDescriptor](#GPURayTracingAccelerationGeometryVertexDescriptor) | Vertex descriptor
| *index* | [GPURayTracingAccelerationGeometryIndexDescriptor](#GPURayTracingAccelerationGeometryIndexDescriptor) | Index descriptor
| *aabb* | [GPURayTracingAccelerationGeometryAABBDescriptor](#GPURayTracingAccelerationGeometryAABBDescriptor) | AABB descriptor

### GPURayTracingAccelerationInstanceDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| *flags* | [GPURayTracingAccelerationInstanceFlag](#GPURayTracingAccelerationInstanceFlag) | Flags for this instance
| *mask* | Number (1 Byte) | Ignore mask (see `traceNV`)
| *instanceId* | Number (24 Bit) | Used to identify an instance in a shader (see `gl_InstanceCustomIndexNV`)instance
| *instanceOffset* | Number (24 Bit) | Unused yet
| *transform* | [GPURayTracingAccelerationInstanceTransformDescriptor](#GPURayTracingAccelerationInstanceTransformDescriptor) | Transform properties of this instance
| *transformMatrix* | Float32Array | Low-Level alternative to *transform* - A 3x4 row-major transform matrix
| geometryContainer | [GPURayTracingAccelerationContainer](#GPURayTracingAccelerationContainer) | Geometry container to be used by this instance

### GPURayTracingAccelerationContainerDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| *flags* | [GPURayTracingAccelerationContainerLevel](#GPURayTracingAccelerationContainerLevel) | Flags for the container
| *level* | [GPURayTracingAccelerationContainerLevel](#GPURayTracingAccelerationContainerLevel) | Level of the container
| *geometries* | [[GPURayTracingAccelerationGeometryDescriptor](#GPURayTracingAccelerationGeometryDescriptor)] | Geometry to encapsulate by this container
| *instances* | [[GPURayTracingAccelerationInstanceDescriptor](#GPURayTracingAccelerationInstanceDescriptor)] | Geometry instances to encapsulate by this container

### GPURayTracingShaderBindingTableStagesDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| stage | [GPUShaderStage](https://gpuweb.github.io/gpuweb/#gpushaderstage) | Ray-Tracing shader stage
| module | [GPUShaderModule](https://gpuweb.github.io/gpuweb/#shader-module) | Shader module containing the shader

### GPURayTracingShaderBindingTableGroupsDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| type | [GPURayTracingShaderBindingTableGroupType](#GPURayTracingShaderBindingTableGroupType) | Group type
| *generalIndex* | Number | Index of a general group stage (`.rgen`, `.rmiss`)
| *closestHitIndex* | Number | Index of a closest-hit stage (`.rchit`)
| *anyHitIndex* | Number | Index of a any-hit stage (`.rahit`)
| *intersectionIndex* | Number | Index of a intersection stage (`.rint`)

Each index defaults to `-1`, indicating that the group is unused. To enable a group, use the index of the respective shader defined in *stages*.

### GPURayTracingShaderBindingTableDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| stages | [[GPURayTracingShaderBindingTableStagesDescriptor](#GPURayTracingShaderBindingTableStagesDescriptor)] | Ray-Tracing stages
| groups | [[GPURayTracingShaderBindingTableGroupsDescriptor](#GPURayTracingShaderBindingTableGroupsDescriptor)] | Ray-Tracing groups

### GPURayTracingStateDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| shaderBindingTable | [GPURayTracingShaderBindingTable](#GPURayTracingShaderBindingTable) | The shader binding table to use
| *maxRecursionDepth* | Number | The maximum allowed Ray trace recursion in shaders. Defaults to *1*

### GPURayTracingPipelineDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| *layout* | [GPUPipelineLayout](https://gpuweb.github.io/gpuweb/#gpupipelinelayout) | Pipeline layout to use
| rayTracingState | [GPURayTracingStateDescriptor](#GPURayTracingStateDescriptor) | Ray-Tracing state for this pipeline

### GPURayTracingPassDescriptor

| Name | Type | Description |
| :--- | :--- | :--- |
| *label* | String | Debug label
