// begonia: a pipeline's reflection through the C ABI (MTLRenderPipelineReflection, MTLComputePipelineReflection).
//
// The front end answered every `reflection:` out-parameter with nil, and an application that asks for one and gets
// nil gives up: Freeform aborts at launch ("Failed to create pipeline reflection object"), because it finds its
// uniform buffers and its sampler by name in vertexArguments/fragmentArguments. The runtime knows the bindings (the
// functions' AIR, metal2vulkan::reflection); this hands them over as a flat array the front end makes Metal's
// objects of. Included at the end of NativeMetalObjects.rs by tools/build_metal2vulkan_linux.sh, so it shares that
// file's helpers (guard, get, e) and descriptor conversions (render_descriptor, compute_descriptor).

/// One binding, as `MTLArgument`/`MTLBinding` answer it. Raw values are Metal's own (MTLBindingType,
/// MTLBindingAccess, MTLDataType, MTLTextureType).
#[repr(C)]
pub struct NmbBinding {
    pub name: [c_char; 96],
    pub binding_type: u32,
    pub access: u32,
    pub index: u32,
    /// 1 used (active), 2 an argument buffer, 4 a buffer, 8 a texture, 16 threadgroup memory, 32 a depth texture
    pub flags: u32,
    pub buffer_alignment: u64,
    pub buffer_data_size: u64,
    pub buffer_data_type: u32,
    pub texture_type: u32,
    pub texture_data_type: u32,
    pub array_length: u32,
    pub threadgroup_alignment: u64,
    pub threadgroup_size: u64,
}

fn begonia_binding(b: &metal2vulkan::reflection::Binding) -> NmbBinding {
    let mut out = NmbBinding {
        name: [0; 96],
        binding_type: b.binding_type().as_raw(),
        access: b.access().as_raw(),
        index: b.index(),
        flags: (b.is_used() as u32) | ((b.is_argument() as u32) << 1),
        buffer_alignment: 0,
        buffer_data_size: 0,
        buffer_data_type: 0,
        texture_type: 0,
        texture_data_type: 0,
        array_length: 0,
        threadgroup_alignment: 0,
        threadgroup_size: 0,
    };
    for (i, c) in b.name().bytes().take(95).enumerate() {
        out.name[i] = c as c_char;
    }
    if let Some(buffer) = b.buffer() {
        out.flags |= 4;
        out.buffer_alignment = buffer.buffer_alignment();
        out.buffer_data_size = buffer.buffer_data_size();
        out.buffer_data_type = buffer.buffer_data_type().as_raw();
    }
    if let Some(texture) = b.texture() {
        out.flags |= 8 | if texture.is_depth_texture() { 32 } else { 0 };
        out.texture_type = texture.texture_type().as_raw();
        out.texture_data_type = texture.texture_data_type().as_raw();
        out.array_length = texture.array_length();
    }
    if let Some(memory) = b.threadgroup() {
        out.flags |= 16;
        out.threadgroup_alignment = memory.threadgroup_memory_alignment();
        out.threadgroup_size = memory.threadgroup_memory_data_size();
    }
    out
}

/// The stages' bindings one after the other into `out` (at most `capacity`), each stage's count into `counts`.
/// Answers how many there are in all: a caller whose array was too small asks again with a bigger one.
unsafe fn begonia_write(stages: &[&[metal2vulkan::reflection::Binding]], out: *mut NmbBinding, capacity: u64,
                        counts: *mut u64) -> i64 {
    let mut n = 0u64;
    for (stage, bindings) in stages.iter().enumerate() {
        if !counts.is_null() {
            unsafe { *counts.add(stage) = bindings.len() as u64 };
        }
        for binding in bindings.iter() {
            if n < capacity && !out.is_null() {
                unsafe { out.add(n as usize).write(begonia_binding(binding)) };
            }
            n += 1;
        }
    }
    n as i64
}

/// # Safety
/// `descriptor` is a valid descriptor; `out` holds `capacity` records (or is null); `counts` holds five (or is null).
/// The stages, in order: vertex, fragment, tile, object, mesh. -1: no reflection (nmb_last_error says why).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_render_pipeline_reflection(descriptor: *const NmbRenderPipelineDescriptor, out: *mut NmbBinding,
                                                        capacity: u64, counts: *mut u64) -> i64 {
    guard(-1, || {
        let reflection = render_descriptor(descriptor)?
            .reflection(&metal2vulkan::Budget::standard())
            .map_err(e("render pipeline reflection"))?;
        Ok(unsafe {
            begonia_write(&[reflection.vertex_bindings(), reflection.fragment_bindings(), reflection.tile_bindings(),
                            reflection.object_bindings(), reflection.mesh_bindings()], out, capacity, counts)
        })
    })
}

/// # Safety
/// As `nmb_render_pipeline_reflection`, with one stage (the kernel's): `counts` holds one (or is null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_compute_pipeline_reflection(descriptor: *const NmbComputePipelineDescriptor, out: *mut NmbBinding,
                                                         capacity: u64, counts: *mut u64) -> i64 {
    guard(-1, || {
        let reflection = compute_descriptor(descriptor)?
            .reflection(&metal2vulkan::Budget::standard())
            .map_err(e("compute pipeline reflection"))?;
        Ok(unsafe { begonia_write(&[reflection.bindings()], out, capacity, counts) })
    })
}
