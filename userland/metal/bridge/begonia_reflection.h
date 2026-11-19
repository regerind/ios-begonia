
/* begonia: a pipeline's reflection (userland/metal/bridge/begonia_reflection.rs). Raw values are Metal's own. */
struct NmbBinding {
    char name[96];
    uint32_t binding_type, access, index;
    uint32_t flags; /* 1 used, 2 an argument buffer, 4 a buffer, 8 a texture, 16 threadgroup memory, 32 a depth texture */
    uint64_t buffer_alignment, buffer_data_size;
    uint32_t buffer_data_type, texture_type, texture_data_type, array_length;
    uint64_t threadgroup_alignment, threadgroup_size;
};
/* Stages in order: vertex, fragment, tile, object, mesh (counts[5]); the compute one has one stage (counts[1]).
 * Answers the number of bindings in all (only capacity are written) or -1 (nmb_last_error). */
int64_t nmb_render_pipeline_reflection(const struct NmbRenderPipelineDescriptor *, struct NmbBinding *, uint64_t capacity, uint64_t *counts);
int64_t nmb_compute_pipeline_reflection(const struct NmbComputePipelineDescriptor *, struct NmbBinding *, uint64_t capacity, uint64_t *counts);
