
/* begonia: a pipeline's reflection (userland/metal/bridge/begonia_reflection.rs). Raw values are Metal's own.
 * (Appended after the header's own declarations, so with its own C linkage for the Objective-C++ front end.) */
#ifdef __cplusplus
extern "C" {
#endif
struct NmbBinding {
    char name[96];
    uint32_t binding_type, access, index;
    uint32_t flags; /* 1 used, 2 an argument buffer, 4 a buffer, 8 a texture, 16 threadgroup memory, 32 a depth texture */
    uint64_t buffer_alignment, buffer_data_size;
    uint32_t buffer_data_type, texture_type, texture_data_type, array_length;
    uint64_t threadgroup_alignment, threadgroup_size;
    uint32_t struct_first, struct_count; /* the buffer's struct: a run of NmbStructMember (UINT32_MAX: none) */
};
/* A struct is a run of members; a member that is a struct, or an array of structs, names its run (child_*). */
struct NmbStructMember {
    char name[64];
    uint64_t offset;
    uint32_t data_type, argument_index;
    uint32_t child_first, child_count;
    uint32_t flags; /* 1 a struct, 2 an array */
    uint32_t array_length;
    uint64_t array_stride;
    uint32_t array_element_type, array_argument_index_stride;
};
/* Stages in order: vertex, fragment, tile, object, mesh (counts[5]); the compute one has one stage (counts[1]).
 * Answers the number of bindings in all (only capacity are written; member_count says how many members) or -1. */
int64_t nmb_render_pipeline_reflection2(const struct NmbRenderPipelineDescriptor *, struct NmbBinding *, uint64_t capacity,
                                        uint64_t *counts, struct NmbStructMember *, uint64_t member_capacity, uint64_t *member_count);
int64_t nmb_compute_pipeline_reflection2(const struct NmbComputePipelineDescriptor *, struct NmbBinding *, uint64_t capacity,
                                         uint64_t *counts, struct NmbStructMember *, uint64_t member_capacity, uint64_t *member_count);
#ifdef __cplusplus
}
#endif
/* begonia: draws and dispatches whose counts come from a buffer (userland/metal/bridge/begonia_indirect.rs) */
#ifdef __cplusplus
extern "C" {
#endif
bool nmb_render_draw_indirect(NmbEncoder *, uint64_t primitive, NmbBuffer *indirect, uint64_t offset);
bool nmb_render_draw_indexed_indirect(NmbEncoder *, uint64_t primitive, uint64_t index_type, NmbBuffer *indices, uint64_t index_offset,
                                      NmbBuffer *indirect, uint64_t offset);
bool nmb_compute_dispatch_indirect(NmbEncoder *, NmbBuffer *indirect, uint64_t offset, const uint64_t per_group[3]);
#ifdef __cplusplus
}
#endif
/* begonia: argument buffers (userland/metal/bridge/begonia_arguments.rs) */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct NmbArgumentEncoder NmbArgumentEncoder;
NmbArgumentEncoder *nmb_argument_encoder_new(NmbFunction *, uint32_t buffer_index);
NmbArgumentEncoder *nmb_argument_encoder_nested(NmbArgumentEncoder *, uint32_t index);
void nmb_argument_encoder_release(NmbArgumentEncoder *);
uint64_t nmb_argument_encoder_encoded_length(const NmbArgumentEncoder *);
uint64_t nmb_argument_encoder_alignment(const NmbArgumentEncoder *);
/* element UINT64_MAX: setArgumentBuffer:offset:, else setArgumentBuffer:startOffset:arrayElement: */
bool nmb_argument_encoder_set_target(NmbArgumentEncoder *, NmbBuffer *, uint64_t offset, uint64_t element);
bool nmb_argument_encoder_set_buffer(NmbArgumentEncoder *, NmbBuffer *, uint64_t offset, uint32_t index);
bool nmb_argument_encoder_set_texture(NmbArgumentEncoder *, NmbTexture *, uint32_t index);
bool nmb_argument_encoder_set_sampler(NmbArgumentEncoder *, NmbSampler *, uint32_t index);
void *nmb_argument_encoder_constant_data(NmbArgumentEncoder *, uint32_t index);
#ifdef __cplusplus
}
#endif
