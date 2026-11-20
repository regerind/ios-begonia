/* bistat: offline Bifrost v7 (Mali-G76) statistics for a fragment SPIR-V module. Descriptors are lowered crudely
 * (binding = index); the result is for counting instructions and cycles, not for running. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nir.h"
#include "nir_builder.h"
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/compiler/pan_nir.h"
#include "panfrost/model/pan_model.h"

static int slots(const struct glsl_type *type, bool bindless) { (void)bindless; return glsl_count_attribute_slots(type, false); }

static bool lower_desc(nir_builder *b, nir_instr *instr, void *data)
{
   (void)data;
   if (instr->type == nir_instr_type_tex) {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      bool p = false;
      for (int which = 0; which < 2; which++) {
         nir_tex_src_type t = which ? nir_tex_src_sampler_deref : nir_tex_src_texture_deref;
         int i = nir_tex_instr_src_index(tex, t);
         if (i < 0) continue;
         nir_variable *var = nir_deref_instr_get_variable(nir_src_as_deref(tex->src[i].src));
         unsigned idx = var ? var->data.binding : 0;
         if (which) tex->sampler_index = idx; else tex->texture_index = idx;
         nir_tex_instr_remove_src(tex, i);
         p = true;
      }
      return p;
   }
   if (instr->type != nir_instr_type_intrinsic) return false;
   nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
   b->cursor = nir_before_instr(instr);
   if (in->intrinsic == nir_intrinsic_vulkan_resource_index) {
      nir_def *v = nir_imm_ivec2(b, nir_intrinsic_binding(in), 0);
      nir_def_replace(&in->def, v);
      return true;
   }
   if (in->intrinsic == nir_intrinsic_load_vulkan_descriptor) {
      nir_def_replace(&in->def, in->src[0].ssa);
      return true;
   }
   return false;
}

static bool lower_push(nir_builder *b, nir_intrinsic_instr *in, void *data)
{
   (void)data;
   if (in->intrinsic != nir_intrinsic_load_push_constant) return false;
   b->cursor = nir_before_instr(&in->instr);
   nir_def *off = nir_iadd_imm(b, in->src[0].ssa, nir_intrinsic_base(in));
   nir_def *v = nir_load_ubo(b, in->def.num_components, in->def.bit_size, nir_imm_int(b, 15), off,
                             .align_mul = 4, .align_offset = 0, .range_base = 0, .range = ~0);
   nir_def_replace(&in->def, v);
   return true;
}

#include "claim.inc"

int main(int argc, char **argv)
{
   const uint64_t gpu = 0x72110000;
   if (argc < 2) return 1;
   FILE *f = fopen(argv[1], "rb");
   if (!f) return 1;
   static uint32_t words[1 << 20];
   size_t n = fread(words, 4, 1 << 20, f);
   fclose(f);
   uint32_t specv = argc > 2 ? strtoul(argv[2], NULL, 0) : 0;
   static uint32_t specdata[64]; struct nir_spirv_specialization_entry ent[64]; struct nir_spirv_specialization specs = {argc > 2 ? 64 : 0, ent};
   for (int i = 0; i < 64; i++) { specdata[i] = specv; ent[i].id = i; ent[i].size = 4; ent[i].data = (uint8_t *)&specdata[i]; ent[i].defined_on_module = false; }
   glsl_type_singleton_init_or_ref();
   const nir_shader_compiler_options *options = pan_get_nir_shader_compiler_options(7, MESA_SHADER_FRAGMENT, false);
   struct spirv_to_nir_options so = {
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .shared_addr_format = nir_address_format_32bit_offset,
      .min_ubo_alignment = 16, .min_ssbo_alignment = 16,
   };
   nir_shader *nir = spirv_to_nir(words, n, &specs, MESA_SHADER_FRAGMENT, "main", &so, options);
   if (!nir) { fprintf(stderr, "spirv_to_nir failed\n"); return 2; }
   NIR_PASS(_, nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS(_, nir, nir_lower_returns);
   NIR_PASS(_, nir, nir_inline_functions);
   NIR_PASS(_, nir, nir_opt_copy_prop);
   NIR_PASS(_, nir, nir_opt_deref);
   nir_remove_non_entrypoints(nir);
   NIR_PASS(_, nir, nir_lower_variable_initializers, ~0);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_split_per_member_structs);
   NIR_PASS(_, nir, nir_lower_io_vars_to_temporaries, nir_shader_get_entrypoint(nir), nir_var_shader_out);
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_split_var_copies);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_vars_to_ssa);
   NIR_PASS(_, nir, nir_opt_copy_prop_vars);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, nir_opt_dce);
   NIR_PASS(_, nir, nir_opt_dead_cf);
   NIR_PASS(_, nir, nir_lower_system_values);
   NIR_PASS(_, nir, nir_lower_wpos_center);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   pan_preprocess_nir(nir, gpu);
   NIR_PASS(_, nir, nir_shader_instructions_pass, lower_desc, nir_metadata_control_flow, NULL);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_ubo, nir_address_format_32bit_index_offset);
   NIR_PASS(_, nir, nir_lower_explicit_io, nir_var_mem_push_const, nir_address_format_32bit_offset);
   NIR_PASS(_, nir, nir_lower_global_vars_to_local);
   NIR_PASS(_, nir, nir_lower_var_copies);
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out, slots, nir_lower_io_use_interpolated_input_intrinsics);
   NIR_PASS(_, nir, nir_opt_constant_folding);
   NIR_PASS(_, nir, pan_nir_lower_fs_outputs, false);
   if (getenv("BISTAT_ALIGN16")) fprintf(stderr, "claimed %llx ", (unsigned long long)begonia_ubo_align16(nir));
   NIR_PASS(_, nir, nir_shader_intrinsics_pass, lower_push, nir_metadata_control_flow, NULL);
   struct pan_compile_inputs input = {.gpu_id = gpu};
   input.fau.promote_immediates = true;
   input.fau.pushable_ubos = getenv("BISTAT_PUSH") ? strtoul(getenv("BISTAT_PUSH"), NULL, 0) : 0x8000;
   struct pan_shader_info info;
   memset(&info, 0, sizeof info);
   pan_postprocess_nir(nir, &input, &info);
   if (getenv("BISTAT_NIR")) nir_print_shader(nir, stdout);
   struct util_dynarray binary = UTIL_DYNARRAY_INIT;
   pan_shader_compile(nir, &input, &binary, &info);
   printf("binary %u bytes\n", (unsigned)binary.size);
   if (getenv("BISTAT_DIS")) pan_disassemble(stdout, binary.data, binary.size, gpu, false);
   return 0;
}
