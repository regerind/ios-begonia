// begonia: draws and dispatches whose counts come from a buffer (drawPrimitives:indirectBuffer:..., drawIndexedPrimitives:
// ...indirectBuffer:..., dispatchThreadgroupsWithIndirectBuffer:...). The runtime has them; the bridge did not, and a
// program that asked died on the unknown selector (metal_game_test, y48). Games' engines draw so (GPU culling, particle
// systems). Included at the end of NativeMetalObjects.rs by tools/build_metal2vulkan_linux.sh, beside its helpers.

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_draw_indirect(encoder: *mut NmbEncoder, primitive: u64, indirect: *mut NmbBuffer, offset: u64) -> bool {
    guard(false, || {
        render(encoder)?
            .draw_primitives_indirect(raw::<PrimitiveType>(primitive, "primitive")?, &get(indirect)?.0, offset)
            .map_err(e("indirect draw"))?;
        Ok(true)
    })
}

#[unsafe(no_mangle)]
pub extern "C" fn nmb_render_draw_indexed_indirect(encoder: *mut NmbEncoder, primitive: u64, index_type: u64, indices: *mut NmbBuffer,
                                                   index_offset: u64, indirect: *mut NmbBuffer, offset: u64) -> bool {
    guard(false, || {
        render(encoder)?
            .draw_indexed_primitives_indirect(raw::<PrimitiveType>(primitive, "primitive")?, raw::<IndexType>(index_type, "index type")?,
                                              &get(indices)?.0, index_offset, &get(indirect)?.0, offset)
            .map_err(e("indexed indirect draw"))?;
        Ok(true)
    })
}

/// # Safety
/// `per_group` holds three values.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn nmb_compute_dispatch_indirect(encoder: *mut NmbEncoder, indirect: *mut NmbBuffer, offset: u64,
                                                       per_group: *const u64) -> bool {
    guard(false, || {
        let g = triple(per_group)?;
        compute(encoder)?
            .dispatch_threadgroups_indirect(&get(indirect)?.0, offset, Size::new(g[0], g[1], g[2]))
            .map_err(e("indirect dispatch"))?;
        Ok(true)
    })
}
