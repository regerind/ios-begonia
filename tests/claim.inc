/* begonia: a Metal buffer's uniforms are read at "its offset + a constant", the offset being a push-constant word with
 * its low two bits cleared. Nothing says the offset is a multiple of 16, so each float is a load of its own: the 14-tap
 * blur has 22 loads and is bound by them (21 load cycles for 10 of arithmetic). Metal asks for an offset aligned to the
 * data's type, and 16 is what CoreAnimation uses, so the loads of a fragment shader are told so here and neighbours
 * join into one load of 16 bytes. The words used this way are remembered: a draw whose word is not a multiple of 16
 * says so (PANVK_NO_UBO_ALIGN16 turns the whole thing off). */
static bool
begonia_claim_ubo_align(nir_builder *b, nir_intrinsic_instr *in, void *data)
{
   uint64_t *words = data;
   (void)b;
   if (in->intrinsic != nir_intrinsic_load_ubo)
      return false;
   nir_def *off = in->src[1].ssa;
   uint32_t c = 0;
   nir_alu_instr *alu = nir_def_instr(off)->type == nir_instr_type_alu
                           ? nir_instr_as_alu(nir_def_instr(off)) : NULL;
   if (alu && alu->op == nir_op_iadd) {
      unsigned k = nir_src_is_const(alu->src[1].src) ? 1 : 0;
      if (!nir_src_is_const(alu->src[k].src))
         return false;
      c = nir_src_as_uint(alu->src[k].src);
      off = alu->src[1 - k].src.ssa;
      alu = nir_def_instr(off)->type == nir_instr_type_alu
               ? nir_instr_as_alu(nir_def_instr(off)) : NULL;
   }
   if (!alu || alu->op != nir_op_iand)
      return false;
   unsigned k = nir_src_is_const(alu->src[1].src) ? 1 : 0;
   if (!nir_src_is_const(alu->src[k].src) ||
       (uint32_t)nir_src_as_uint(alu->src[k].src) != 0xfffffffcu)
      return false;
   nir_def *base = alu->src[1 - k].src.ssa;
   if (nir_def_instr(base)->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *pc = nir_instr_as_intrinsic(nir_def_instr(base));
   if (pc->intrinsic != nir_intrinsic_load_push_constant ||
       !nir_src_is_const(pc->src[0]) || base->bit_size != 32)
      return false;
   uint32_t at = nir_intrinsic_base(pc) + nir_src_as_uint(pc->src[0]) +
                 4 * alu->src[1 - k].swizzle[0];
   if ((at & 3) || at / 4 >= 64 || nir_intrinsic_align_mul(in) >= 16)
      return false;
   nir_intrinsic_set_align(in, 16, c % 16);
   *words |= 1ull << (at / 4);
   return true;
}

static uint64_t
begonia_ubo_align16(nir_shader *nir)
{
   uint64_t words = 0;
   if (nir->info.stage != MESA_SHADER_FRAGMENT || getenv("PANVK_NO_UBO_ALIGN16"))
      return 0;
   /* the offsets in the shape the test above reads */
   for (unsigned i = 0; i < 4; i++) {
      NIR_PASS(_, nir, nir_opt_algebraic);
      NIR_PASS(_, nir, nir_opt_constant_folding);
      NIR_PASS(_, nir, nir_opt_copy_prop);
      NIR_PASS(_, nir, nir_opt_cse);
      NIR_PASS(_, nir, nir_opt_dce);
   }
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, begonia_claim_ubo_align,
            nir_metadata_all, &words);
   return words;
}
