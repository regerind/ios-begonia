// begonia: a pipeline's reflection through the C ABI (MTLRenderPipelineReflection, MTLComputePipelineReflection).
//
// The front end answered every `reflection:` out-parameter with nil, and an application that asks for one and gets
// nil gives up: Freeform aborted at launch ("Failed to create pipeline reflection object"), because it finds its
// uniform buffers and its sampler by name in vertexArguments/fragmentArguments. And an engine that fills its uniform
// buffers member by member reads the buffers' struct layout (bufferStructType): SceneKit bound nothing for its
// per-node transforms while that was nil (y39), so every vertex of its box fell on one point. The runtime knows the
// bindings and their types (the functions' AIR, metal2vulkan::reflection); this hands them over as flat arrays the front
// end makes Metal's objects of. Included at the end of NativeMetalObjects.rs by tools/build_metal2vulkan_linux.sh, so it
// shares that file's helpers (guard, get, e) and descriptor conversions (render_descriptor, compute_descriptor).

use metal2vulkan::reflection::{Binding as BegoniaBinding, StructType as BegoniaStructType};

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
    /// the buffer's struct: its members are `struct_count` records from `struct_first` (u32::MAX: none)
    pub struct_first: u32,
    pub struct_count: u32,
}

/// One member of a struct (`MTLStructMember`); a struct is a run of these. A member that is a struct, or an array of
/// structs, names the run of that struct (`child_first`, `child_count`; u32::MAX: none).
#[repr(C)]
pub struct NmbStructMember {
    pub name: [c_char; 64],
    pub offset: u64,
    pub data_type: u32,
    pub argument_index: u32,
    pub child_first: u32,
    pub child_count: u32,
    /// 1 a struct, 2 an array
    pub flags: u32,
    pub array_length: u32,
    pub array_stride: u64,
    pub array_element_type: u32,
    pub array_argument_index_stride: u32,
}

fn begonia_name<const N: usize>(name: &str) -> [c_char; N] {
    let mut out = [0 as c_char; N];
    for (i, c) in name.bytes().take(N - 1).enumerate() {
        out[i] = c as c_char;
    }
    out
}

/// The struct's members as a run at the end of `members` (the runs of its nested structs after it); its first and count.
fn begonia_struct(st: &BegoniaStructType, members: &mut Vec<NmbStructMember>, depth: u32) -> (u32, u32) {
    let first = members.len();
    let list = st.members();
    for m in list {
        members.push(NmbStructMember {
            name: begonia_name(m.name()),
            offset: m.offset(),
            data_type: m.data_type().as_raw(),
            argument_index: m.argument_index(),
            child_first: u32::MAX,
            child_count: 0,
            flags: 0,
            array_length: 0,
            array_stride: 0,
            array_element_type: 0,
            array_argument_index_stride: 0,
        });
    }
    if depth < 8 {
        for (i, m) in list.iter().enumerate() {
            if let Some(s) = m.struct_type() {
                let (f, c) = begonia_struct(s, members, depth + 1);
                let r = &mut members[first + i];
                r.flags |= 1;
                r.child_first = f;
                r.child_count = c;
            }
            if let Some(a) = m.array_type() {
                let (f, c) = match a.element_struct_type() {
                    Some(s) => begonia_struct(s, members, depth + 1),
                    None => (u32::MAX, 0),
                };
                let r = &mut members[first + i];
                r.flags |= 2;
                r.array_length = a.array_length();
                r.array_stride = a.stride();
                r.array_element_type = a.element_type().as_raw();
                r.array_argument_index_stride = a.argument_index_stride();
                r.child_first = f;
                r.child_count = c;
            }
        }
    }
    (first as u32, list.len() as u32)
}

fn begonia_binding(b: &BegoniaBinding, members: &mut Vec<NmbStructMember>) -> NmbBinding {
    let mut out = NmbBinding {
        name: begonia_name(b.name()),
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
        struct_first: u32::MAX,
        struct_count: 0,
    };
    if let Some(buffer) = b.buffer() {
        out.flags |= 4;
        out.buffer_alignment = buffer.buffer_alignment();
        out.buffer_data_size = buffer.buffer_data_size();
        out.buffer_data_type = buffer.buffer_data_type().as_raw();
        if let Some(st) = buffer.buffer_struct_type() {
            let (f, c) = begonia_struct(st, members, 0);
            out.struct_first = f;
            out.struct_count = c;
        }
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

/// The stages' bindings one after the other into `out` (at most `capacity`), each stage's count into `counts`, their
/// structs' members into `members` (at most `member_capacity`; how many in all into `member_count`). Answers how many
/// bindings there are in all: a caller whose arrays were too small asks again with bigger ones.
unsafe fn begonia_write(stages: &[&[BegoniaBinding]], out: *mut NmbBinding, capacity: u64, counts: *mut u64,
                        members_out: *mut NmbStructMember, member_capacity: u64, member_count: *mut u64) -> i64 {
    let mut n = 0u64;
    let mut members = Vec::new();
    for (stage, bindings) in stages.iter().enumerate() {
        if !counts.is_null() {
            unsafe { *counts.add(stage) = bindings.len() as u64 };
        }
        for binding in bindings.iter() {
            let record = begonia_binding(binding, &mut members);
            if n < capacity && !out.is_null() {
                unsafe { out.add(n as usize).write(record) };
            }
            n += 1;
        }
    }
    if !member_count.is_null() {
        unsafe { *member_count = members.len() as u64 };
    }
    if !members_out.is_null() {
        for (i, m) in members.into_iter().enumerate() {
            if (i as u64) >= member_capacity {
                break;
            }
            unsafe { members_out.add(i).write(m) };
        }
    }
    n as i64
}

/// # Safety
/// `descriptor` is a valid descriptor; `out` holds `capacity` records (or is null); `counts` holds five (or is null);
/// `members` holds `member_capacity` records (or is null), `member_count` one (or is null).
/// The stages, in order: vertex, fragment, tile, object, mesh. -1: no reflection (nmb_last_error says why).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_render_pipeline_reflection2(descriptor: *const NmbRenderPipelineDescriptor, out: *mut NmbBinding,
                                                         capacity: u64, counts: *mut u64, members: *mut NmbStructMember,
                                                         member_capacity: u64, member_count: *mut u64) -> i64 {
    guard(-1, || {
        let reflection = render_descriptor(descriptor)?
            .reflection(&metal2vulkan::Budget::standard())
            .map_err(e("render pipeline reflection"))?;
        Ok(unsafe {
            begonia_write(&[reflection.vertex_bindings(), reflection.fragment_bindings(), reflection.tile_bindings(),
                            reflection.object_bindings(), reflection.mesh_bindings()], out, capacity, counts, members,
                          member_capacity, member_count)
        })
    })
}

/// # Safety
/// As `nmb_render_pipeline_reflection2`, with one stage (the kernel's): `counts` holds one (or is null).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_compute_pipeline_reflection2(descriptor: *const NmbComputePipelineDescriptor, out: *mut NmbBinding,
                                                          capacity: u64, counts: *mut u64, members: *mut NmbStructMember,
                                                          member_capacity: u64, member_count: *mut u64) -> i64 {
    guard(-1, || {
        let reflection = compute_descriptor(descriptor)?
            .reflection(&metal2vulkan::Budget::standard())
            .map_err(e("compute pipeline reflection"))?;
        Ok(unsafe { begonia_write(&[reflection.bindings()], out, capacity, counts, members, member_capacity, member_count) })
    })
}
