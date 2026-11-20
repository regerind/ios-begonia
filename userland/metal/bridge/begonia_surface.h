
/* begonia: a no-copy buffer over an IOSurface's pages, which the GPU reaches in place through the transport's surface
 * descriptors (userland/metal/bridge/begonia_surface.rs). */
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*NmbSurfaceFdCreate)(uint32_t surface, uint64_t size, uint64_t *token);
typedef int (*NmbSurfaceFdRelease)(uint64_t token);
NmbBuffer *nmb_buffer_new_no_copy_surface(NmbDevice *device, void *pointer, uint64_t length, uint64_t options,
                                          void (*release)(void *context, void *pointer, uint64_t length), void *context,
                                          uint32_t surface, NmbSurfaceFdCreate fd_create, NmbSurfaceFdRelease fd_release);
bool nmb_surface_descriptor_counts(uint64_t *out);
#ifdef __cplusplus
}
#endif
