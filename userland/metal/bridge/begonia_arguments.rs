// begonia: argument buffers (MTLArgumentEncoder). Engines bind their resources through them -- a buffer of buffers',
// textures' and samplers' handles that a shader reads -- and the runtime has them (metal2vulkan_runtime::ArgumentEncoder);
// the bridge did not, and -[MTLFunction newArgumentEncoderWithBufferIndex:] was an unknown selector (metal_game_test,
// y50). Included at the end of NativeMetalObjects.rs by tools/build_metal2vulkan_linux.sh, beside its helpers.

pub struct NmbArgumentEncoder(metal2vulkan_runtime::ArgumentEncoder);

fn begonia_optional<'a, T>(handle: *const T) -> Result<Option<&'a T>, String> {
    if handle.is_null() { Ok(None) } else { get(handle).map(Some) }
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_new(function: *mut NmbFunction, index: u32) -> *mut NmbArgumentEncoder {
    guard(std::ptr::null_mut(), || {
        let encoder = get(function)?.0.new_argument_encoder_with_buffer_index(index).map_err(e("argument encoder"))?;
        Ok(boxed(NmbArgumentEncoder(encoder)))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_nested(encoder: *mut NmbArgumentEncoder, index: u32) -> *mut NmbArgumentEncoder {
    guard(std::ptr::null_mut(), || {
        let nested = get(encoder)?.0.new_argument_encoder_for_buffer_at_index(index).map_err(e("nested argument encoder"))?;
        Ok(boxed(NmbArgumentEncoder(nested)))
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_release(encoder: *mut NmbArgumentEncoder) {
    release(encoder)
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_encoded_length(encoder: *const NmbArgumentEncoder) -> u64 {
    guard(0, || Ok(get(encoder)?.0.encoded_length()))
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_alignment(encoder: *const NmbArgumentEncoder) -> u64 {
    guard(0, || Ok(get(encoder)?.0.alignment()))
}

/// `element` u64::MAX: setArgumentBuffer:offset:, else setArgumentBuffer:startOffset:arrayElement:.
#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_set_target(encoder: *mut NmbArgumentEncoder, buffer: *mut NmbBuffer, offset: u64, element: u64) -> bool {
    guard(false, || {
        let buffer = begonia_optional(buffer)?.map(|b| &b.0);
        let encoder = &mut get_mut(encoder)?.0;
        if element == u64::MAX {
            encoder.set_argument_buffer(buffer, offset)
        } else {
            encoder.set_argument_buffer_start_offset_array_element(buffer, offset, element)
        }
        .map_err(e("argument buffer"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_set_buffer(encoder: *mut NmbArgumentEncoder, buffer: *mut NmbBuffer, offset: u64, index: u32) -> bool {
    guard(false, || {
        get(encoder)?.0.set_buffer(begonia_optional(buffer)?.map(|b| &b.0), offset, index).map_err(e("argument buffer's buffer"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_set_texture(encoder: *mut NmbArgumentEncoder, texture: *mut NmbTexture, index: u32) -> bool {
    guard(false, || {
        get(encoder)?.0.set_texture(begonia_optional(texture)?.map(|t| &t.0), index).map_err(e("argument buffer's texture"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_set_sampler(encoder: *mut NmbArgumentEncoder, sampler: *mut NmbSampler, index: u32) -> bool {
    guard(false, || {
        get(encoder)?.0.set_sampler_state(begonia_optional(sampler)?.map(|s| &s.0), index).map_err(e("argument buffer's sampler"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_argument_encoder_constant_data(encoder: *mut NmbArgumentEncoder, index: u32) -> *mut c_void {
    guard(std::ptr::null_mut(), || Ok(get(encoder)?.0.constant_data(index).map_err(e("argument buffer's constant"))?.as_ptr() as *mut c_void))
}
