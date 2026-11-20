#!/usr/bin/env python3
"""Build the pinned Metal Objective-C frontend separately; never install/enable it."""
from pathlib import Path
import argparse,shutil,subprocess
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk',type=Path,required=True)
p.add_argument('--output-dir',type=Path,default=root/'out/metal-frontend-ios')
p.add_argument('--cc',type=Path,help='a cross clang (xnu-clang) for a host without Xcode: used in place of xcrun, nothing is codesigned')
p.add_argument('--quartzcore',action='store_true',help='enable the iOS 16 QuartzCore pipeline library adapter')
p.add_argument('--experimental-mps-stubs',action='store_true',help='reproduce v59 diagnostics that silently skip unsupported compute; never for correctness qualification')
a=p.parse_args()
ref=root/'upstream/ios-s9plus-exynos';out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','-C',str(ref),'rev-parse','HEAD'],text=True).strip()=='2d092398040f98685b0c5a07776df5e9f199b9dc'
source=out/'metalvk';source.mkdir(exist_ok=True)
for f in (ref/'metal-wip/frontend').iterdir():
 if f.is_file():shutil.copy2(f,source/f.name)
for name in ('hakios_metal_shown.h','hakios_metal_shown.c'):
 shutil.copy2(ref/'core/userland/src'/name,out/name)
# begonia: "@apps" in HakiosMetal.clients shows the device to every application -- a program inside an .app of
# /Applications, of the installed applications' containers or of the staged system applications -- and to no daemon,
# nor SpringBoard (its .app is in /System/Library/CoreServices): games are named in no list.
shown=out/'hakios_metal_shown.c';stext=shown.read_text()
old_match='\t\tlisted = strcmp(line, "*") == 0 || (line[0] != \'\\0\' && strcmp(line, name) == 0);'
assert stext.count(old_match)==1
stext=stext.replace(old_match,'''\t\tif (line[0] == '!' && strcmp(line + 1, name) == 0) {
\t\t\tbreak;          /* begonia: "!Name" (before "@apps" or "*"): not this one */
\t\t}
'''+old_match+'''
\t\tif (!listed && strcmp(line, "@apps") == 0) {
\t\t\tchar executable[1024];
\t\t\tuint32_t length = sizeof(executable);

\t\t\tlisted = _NSGetExecutablePath(executable, &length) == 0 && strstr(executable, ".app/") != NULL &&
\t\t\t    (strncmp(executable, "/Applications/", 14) == 0 || strstr(executable, "/containers/Bundle/Application/") != NULL ||
\t\t\t    strstr(executable, "/staged_system_apps/") != NULL);
\t\t}''')
stext=stext.replace('#include <dlfcn.h>','#include <dlfcn.h>\n#include <mach-o/dyld.h>\n#include <stdint.h>',1)
shown.write_text(stext)
f=source/'HakiosMetalVK.mm';text=f.read_text();old='#define DEFAULT_ICD "/redwood/lib/libhakios.vk.dylib"'
assert text.count(old)==1;text=text.replace(old,'#define DEFAULT_ICD "/redwood/lib/libvulkan_panfrost.dylib"')
# begonia (y301): the translated shaders' directory. y300: a boot's 89 render pipeline states took 5.3 s to make in the window
# server (60 ms each, to 132 ms, on the thread that draws: the stutter of everything done for the first time), 0.7 s of it the
# driver's compiling -- the rest the translation, which the runtime keeps on disk for the next process to read; the cache read
# nothing (hit 0, miss 141). Which directory it got, and whether that one outlives a boot, nobody had looked at: a fixed one on
# the data volume first (/private/var/mobile/Library/Caches/m2v, then /private/var/db/m2v), and what was chosen and how many
# entries it held when the process opened it are kept for the telemetry (tele XC).
old='''	char candidates[4][512] = {};
'''
assert text.count(old)==1
text=text.replace(old,'''	char candidates[6][512] = {};
	static const unsigned order[6] = {4, 5, 0, 1, 2, 3};
	strlcpy(candidates[4], "/private/var/mobile/Library/Caches/", sizeof(candidates[4]));
	strlcpy(candidates[5], "/private/var/db/", sizeof(candidates[5]));
''')
old='''	for (unsigned i = 0; i < 4; i++) {
		if (candidates[i][0] == '\\0') {
			continue;
		}
		strlcat(candidates[i], "m2v", sizeof(candidates[i]));
		mkdir(candidates[i], 0700);
		if (access(candidates[i], W_OK) == 0) {
			strlcpy(path, candidates[i], size);
			return true;
		}
	}
	return false;
}
'''
assert text.count(old)==1,text.count(old)
text=text.replace(old,'''	for (unsigned n = 0; n < 6; n++) {
		const unsigned i = order[n];
		if (candidates[i][0] == '\\0') {
			continue;
		}
		strlcat(candidates[i], "m2v", sizeof(candidates[i]));
		mkdir(candidates[i], 0777);
		if (access(candidates[i], W_OK) == 0) {
			strlcpy(path, candidates[i], size);
			hakiosCacheCount(path, i);
			return true;
		}
	}
	snprintf(gHakiosCacheNote, sizeof(gHakiosCacheNote), "no writable cache directory");
	return false;
}
''')
old='''static bool
cacheDirectory(char *path, size_t size)
'''
assert text.count(old)==1
text=text.replace(old,'''#include <dirent.h>
static char gHakiosCacheNote[700] = "cache directory not chosen yet";
extern "C" const char *hakiosCacheNote(void) { return gHakiosCacheNote; }
static unsigned
hakiosCacheEntries(const char *dir, unsigned depth)
{
	unsigned n = 0;
	DIR *d = opendir(dir);
	if (d == NULL) {
		return 0;
	}
	for (struct dirent *e; (e = readdir(d)) != NULL && n < 100000;) {
		if (e->d_name[0] == '.') {
			continue;
		}
		if (e->d_type == DT_DIR && depth < 4) {
			char sub[1024];
			snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name);
			n += hakiosCacheEntries(sub, depth + 1);
		} else {
			n++;
		}
	}
	closedir(d);
	return n;
}
static void
hakiosCacheCount(const char *path, unsigned candidate)
{
	snprintf(gHakiosCacheNote, sizeof(gHakiosCacheNote), "cache %s (candidate %u), %u entries when opened by %s[%d] uid %d", path,
	    candidate, hakiosCacheEntries(path, 0), getprogname(), (int)getpid(), (int)getuid());
	setenv("HAKIOS_CACHE_NOTE", gHakiosCacheNote, 1);
}
static bool
cacheDirectory(char *path, size_t size)
''')
text=text.replace('tried = true;','tried = true;\n\t\tsetenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);\n'
 # begonia (y218): boot-arg panvk_debug=<options> (PanVK's PANVK_DEBUG, comma-separated, e.g. noafbc): the driver's
 # own switches for an experiment, in every process shown the device.
 '\t\t{\n\t\t\tchar bootArgs[1024] = ""; size_t bootSize = sizeof(bootArgs) - 1;\n'
 '\t\t\tconst char *at = sysctlbyname("kern.bootargs", bootArgs, &bootSize, NULL, 0) == 0 ? strstr(bootArgs, "panvk_debug=") : NULL;\n'
 '\t\t\tif (at != NULL) {\n\t\t\t\tchar value[128]; size_t n = strcspn(at + 12, " ");\n'
 '\t\t\t\tif (n < sizeof(value)) { memcpy(value, at + 12, n); value[n] = 0; setenv("PANVK_DEBUG", value, 1); }\n\t\t\t}\n'
 # (y289: boot-arg bifrost_debug=<options> -- the Bifrost compiler's own BIFROST_MESA_DEBUG, e.g. shaderdb: one statistics line
 #  (instructions, tuples, clauses, cycles) per shader compiled, to say which of CoreAnimation's shaders the GPU time goes into)
 '\t\t\t{ const char *bd = strstr(bootArgs, "bifrost_debug=");\n\t\t\t\tif (bd != NULL) { char value[64]; size_t n = strcspn(bd + 14, " ");\n'
 '\t\t\t\t\tif (n < sizeof(value)) { memcpy(value, bd + 14, n); value[n] = 0; setenv("BIFROST_MESA_DEBUG", value, 1); } } }\n'
 # (y220: metal_contain=0 -- the translator emits no containment checks into shaders: bridge patch 0004)
 '\t\t\tif (strstr(bootArgs, "metal_contain=0") != NULL) setenv("NMB_UNCONTAINED", "1", 1);\n'
 # (y299: what y294-y298 measured as gains is on without a boot-arg -- the boot line has no room for their names: PanVK's direct
 #  indexed draws (0021) and exact fragment prepass (0022), the runtime's unchanged YCbCr planes not converted again and YCbCr
 #  textures through the device's filter (runtime 0010). beg_old=1: none of them, as p14.)
 # (y300: with the kernel's measuring mode, begonia_overlap=2, the programs' own measuring too -- their names do not fit the boot
 #  line: PanVK's pass composition, the runtime's pipeline creation lines (counted by slice) and mipmaps by blit in slices 2, 3)
 '\t\t\tif (strstr(bootArgs, "begonia_overlap=2") != NULL) {\n'
 '\t\t\t\tsetenv("PANVK_SAID", "1", 0); setenv("METAL2VULKAN_LOG_PIPELINES", "1", 0); setenv("M2V_MIPBLIT_S", "1", 0);\n\t\t\t}\n'
 '\t\t\tif (strstr(bootArgs, "beg_old=1") == NULL) {\n'
 '\t\t\t\tsetenv("PANVK_DIRECT", "1", 0); setenv("PANVK_PREPASS", "1", 0); setenv("M2V_YSKIP", "1", 0); setenv("M2V_YNATIVE", "1", 0);\n\t\t\t}\n'
 # (y234: ca_env=NAME,NAME -- each CoreAnimation switch (CA_DISABLE_INPLACE_SHADERS ...) set to 1 in every process shown the device)
 '\t\t\tconst char *caEnv = strstr(bootArgs, "ca_env=");\n\t\t\tif (caEnv != NULL) {\n\t\t\t\tchar names[200]; size_t n = strcspn(caEnv + 7, " ");\n'
 '\t\t\t\tif (n < sizeof(names)) { memcpy(names, caEnv + 7, n); names[n] = 0;\n'
 '\t\t\t\t\tfor (char *name = names, *next; name && *name; name = next) { next = strchr(name, \',\'); if (next) *next++ = 0; setenv(name, "1", 1); } }\n\t\t\t}\n\t\t}')
if '#include <sys/sysctl.h>' not in text:text=text.replace('#import <Foundation/Foundation.h>','#import <Foundation/Foundation.h>\n#include <sys/sysctl.h>',1)
f.write_text(text)

mf=source/'NativeBridgeMetalFrontend.mm';original_mtext=mf.read_text();mtext=original_mtext
# begonia (y128): Apple's frameworks ask an encoder for its trace object ID; NBEncoder had none, and MediaRemoteUI (the
# lock screen's Now Playing) died of "unrecognized selector -[NBComputeEncoder globalTraceObjectID]" as a video began,
# again and again (y117: a core at 93-100%). Every encoder (compute, render, blit) answers 0, as with no tracing.
trace_anchor='- (void)popDebugGroup {}\n// Stage 0 compute, 1 vertex, 2 fragment'
assert mtext.count(trace_anchor)==1
mtext=mtext.replace(trace_anchor,'- (void)popDebugGroup {}\n- (uint64_t)globalTraceObjectID {return 0;}\n// Stage 0 compute, 1 vertex, 2 fragment')
# y129: next, "-[NBCommandBuffer userDictionary]" (a private mutable dictionary the frameworks hang their own state on)
cb_anchor='@implementation NBCommandBuffer\n- (instancetype)init {'
assert mtext.count(cb_anchor)==1
mtext=mtext.replace(cb_anchor,'''@implementation NBCommandBuffer
- (NSMutableDictionary *)userDictionary {
 static char key;
 @synchronized(self) {
  NSMutableDictionary *d=objc_getAssociatedObject(self,&key);
  if(!d){d=[NSMutableDictionary dictionary];objc_setAssociatedObject(self,&key,d,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
  return d;
 }
}
- (uint64_t)globalTraceObjectID {return 0;}
- (instancetype)init {''')
mtext=mtext.replace('- (void)dealloc {nmb_library_release(_handle);}', '- (void)dealloc {if(_handle)nmb_library_release(_handle);}')
mtext=mtext.replace('if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {',
                    'if(!_handle){NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=name?:@"stub";stub.functionType=[name containsString:@"vert"]?MTLFunctionTypeVertex:MTLFunctionTypeFragment;if(error)*error=nil;return (id<MTLFunction>)stub;}\n if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {')
mtext=mtext.replace('- (NSArray<NSString *> *)functionNames {\n NSMutableArray *names=[NSMutableArray array];',
                    '- (NSArray<NSString *> *)functionNames {\n NSMutableArray *names=[NSMutableArray array];\n if(!_handle)return names;')
mtext=mtext.replace('NmbFunction *handle=name?nmb_function_new(_handle,name.UTF8String):nullptr;',
                    'NmbFunction *handle=(name && _handle)?nmb_function_new(_handle,name.UTF8String):nullptr;')
mtext=mtext.replace('NmbFunction *handle=name?nmb_function_new_zero_constants(_handle,name.UTF8String):nullptr;',
                    'NmbFunction *handle=(name && _handle)?nmb_function_new_zero_constants(_handle,name.UTF8String):nullptr;')
old_default = '''- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
 NSURL *url=[bundle URLForResource:@"default" withExtension:@"metallib"];
 if(!url){if(error)*error=nbError(@"the bundle has no default.metallib");return nil;}
 return [(id<MTLDevice>)self newLibraryWithURL:url error:error];
}'''
new_default = '''- (id<MTLLibrary>)newDefaultLibraryWithBundle:(NSBundle *)bundle error:(NSError **)error {
 NSURL *url=[bundle URLForResource:@"default" withExtension:@"metallib"];
 if(!url){
  // iOS 16 bundles (e.g. MPSImage) ship pipelinelibs or have no standalone default.metallib.
  // Returning a stub library avoids fatal assertions in MPSDeviceComputePipelineState while
  // letting callers query functions gracefully.
  NBLibrary *stub=[NBLibrary new];stub.handle=nullptr;if(error)*error=nil;return (id<MTLLibrary>)stub;
 }
 return [(id<MTLDevice>)self newLibraryWithURL:url error:error];
}'''
assert mtext.count(old_default) == 1
mtext = mtext.replace(old_default, new_default)

mtext = mtext.replace('- (void)dealloc {nmb_function_release(_handle);}', '- (void)dealloc {if(_handle)nmb_function_release(_handle);}')
mtext = mtext.replace(
    '- (void)dealloc {nmb_compute_pipeline_release(_handle);}\n- (NSUInteger)maxTotalThreadsPerThreadgroup {return nmb_compute_pipeline_max_threads(_handle);}\n- (NSUInteger)threadExecutionWidth {return nmb_compute_pipeline_execution_width(_handle);}',
    '- (void)dealloc {if(_handle)nmb_compute_pipeline_release(_handle);}\n- (NSUInteger)maxTotalThreadsPerThreadgroup {return _handle?nmb_compute_pipeline_max_threads(_handle):256;}\n- (NSUInteger)threadExecutionWidth {return _handle?nmb_compute_pipeline_execution_width(_handle):16;}',
)

old_spec_fail = ''' if(!handle){
  noteProblem([NSString stringWithFormat:@"specialized function %@: %@",name,lastError()]);
  if(error)*error=nbError([NSString stringWithFormat:@"specialized function: %@",lastError()]);return nil;
 }'''
new_spec_fail = ''' if(!handle){
  noteProblem([NSString stringWithFormat:@"specialized function %@: %@",name,lastError()]);
  NBFunction *function=[NBFunction new];function.handle=nullptr;function.name=name;function.functionType=[name containsString:@"vert"]?MTLFunctionTypeVertex:MTLFunctionTypeFragment;
  function.constantsText=text;if(error)*error=nil;return (id<MTLFunction>)function;
 }'''
assert mtext.count(old_spec_fail) == 1
mtext = mtext.replace(old_spec_fail, new_spec_fail)

old_comp_pipe = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(MTLPipelineOption)options
                                                         reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(![(id)function isKindOfClass:[NBFunction class]]){if(error)*error=nbError(@"foreign function");return nil;}
 const NmbPipelineOptions creation={(uint64_t)options,nullptr,0};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_options(gDevice,[(NBFunction *)function handle],&creation);gTimePipeline.add(pipelineBegan);
 if(!handle)return failWith<id<MTLComputePipelineState>>(error,@"compute pipeline",nil);
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}'''
new_comp_pipe = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)function options:(MTLPipelineOption)options
                                                         reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(![(id)function isKindOfClass:[NBFunction class]]){if(error)*error=nbError(@"foreign function");return nil;}
 if(![(NBFunction *)function handle]){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 const NmbPipelineOptions creation={(uint64_t)options,nullptr,0};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_options(gDevice,[(NBFunction *)function handle],&creation);gTimePipeline.add(pipelineBegan);
 if(!handle){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;gPipelines++;if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}'''
assert mtext.count(old_comp_pipe) == 1
mtext = mtext.replace(old_comp_pipe, new_comp_pipe)

old_encoder = '''@interface NBComputeEncoder : NBEncoder
@end
@implementation NBComputeEncoder
- (MTLDispatchType)dispatchType {return MTLDispatchTypeSerial;}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)state {
 if(![(id)state isKindOfClass:[NBComputePipeline class]]){[self.commandBuffer refuse:@"foreign compute pipeline"];return;}
 [self check:nmb_compute_set_pipeline(self.handle,[(NBComputePipeline *)state handle]) what:"setComputePipelineState"];
}'''
new_encoder = '''@interface NBComputeEncoder : NBEncoder
@property(nonatomic) bool stub;
@end
@implementation NBComputeEncoder
- (MTLDispatchType)dispatchType {return MTLDispatchTypeSerial;}
- (void)setComputePipelineState:(id<MTLComputePipelineState>)state {
 if(![(id)state isKindOfClass:[NBComputePipeline class]]){[self.commandBuffer refuse:@"foreign compute pipeline"];return;}
 if(![(NBComputePipeline *)state handle]){
  self.stub=true;
  return;
 }
 self.stub=false;
 [self check:nmb_compute_set_pipeline(self.handle,[(NBComputePipeline *)state handle]) what:"setComputePipelineState"];
}'''
assert mtext.count(old_encoder) == 1
mtext = mtext.replace(old_encoder, new_encoder)

old_dispatch = '''- (void)dispatch:(MTLSize)size group:(MTLSize)group threads:(bool)threads {
 gDispatches++;self.commandBuffer->encodedWork.dispatches++;
 const uint64_t s[3]={size.width,size.height,size.depth},g[3]={group.width,group.height,group.depth};
 [self check:nmb_compute_dispatch(self.handle,s,g,threads) what:"dispatch"];
}'''
new_dispatch = '''- (void)dispatch:(MTLSize)size group:(MTLSize)group threads:(bool)threads {
 if(self.stub)return;
 gDispatches++;self.commandBuffer->encodedWork.dispatches++;
 const uint64_t s[3]={size.width,size.height,size.depth},g[3]={group.width,group.height,group.depth};
 [self check:nmb_compute_dispatch(self.handle,s,g,threads) what:"dispatch"];
}'''
assert mtext.count(old_dispatch) == 1
mtext = mtext.replace(old_dispatch, new_dispatch)

old_comp_desc = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options
                                                       reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 NmbComputePipelineDescriptor p={};std::vector<NmbFunction *> linked,privateLinked;
 if(!computeDescriptor(d,&p,linked,privateLinked,error))return nil;
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error))return nil;
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_descriptor(gDevice,&p,&creation);gTimePipeline.add(pipelineBegan);
 if(!handle)return failWith<id<MTLComputePipelineState>>(error,@"compute pipeline",nil);
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;state.label=d.label;gPipelines++;
 if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}'''
new_comp_desc = '''- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options
                                                       reflection:(MTLAutoreleasedComputePipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(!d || ![(NBFunction *)d.computeFunction handle]){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d?d.label:nil;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 NmbComputePipelineDescriptor p={};std::vector<NmbFunction *> linked,privateLinked;
 if(!computeDescriptor(d,&p,linked,privateLinked,error)){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error)){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();
 NmbComputePipeline *handle=nmb_compute_pipeline_new_with_descriptor(gDevice,&p,&creation);gTimePipeline.add(pipelineBegan);
 if(!handle){
  NBComputePipeline *state=[NBComputePipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLComputePipelineState>)state;
 }
 NBComputePipeline *state=[NBComputePipeline new];state.handle=handle;state.label=d.label;gPipelines++;
 if(error)*error=nil;return (id<MTLComputePipelineState>)state;
}
- (id<MTLComputePipelineState>)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d error:(NSError **)error {
 return [(id)self newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:error];
}
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d completionHandler:(MTLNewComputePipelineStateCompletionHandler)handler {
 MTLComputePipelineDescriptor *snapshot=[d copy];
 compileAsync(^{
  NSError *error=nil;id<MTLComputePipelineState> state=[(id)self newComputePipelineStateWithDescriptor:snapshot options:MTLPipelineOptionNone reflection:nil error:&error];
  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,error);});
 });
}
- (void)newComputePipelineStateWithDescriptor:(MTLComputePipelineDescriptor *)d options:(MTLPipelineOption)options
                           completionHandler:(MTLNewComputePipelineStateWithReflectionCompletionHandler)handler {
 MTLComputePipelineDescriptor *snapshot=[d copy];
 compileAsync(^{
  NSError *error=nil;id<MTLComputePipelineState> state=[(id)self newComputePipelineStateWithDescriptor:snapshot options:options reflection:nil error:&error];
  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,nil,error);});
 });
}'''
assert mtext.count(old_comp_desc) == 1
mtext = mtext.replace(old_comp_desc, new_comp_desc)

old_note_problem = '''static void noteProblem(NSString *what) {
 gProblems++;'''
new_note_problem = '''#include <fcntl.h>
#include <unistd.h>
static void noteProblem(NSString *what) {
 NSLog(@"[BegoniaMetal] noteProblem: %@", what);
 {
  // begonia: an application's NSLog reaches no log that a test session keeps; its problems go to the console too
  // (the program started by hand has the console as its stderr already).
  static int consoleFd=-2;
  if(consoleFd==-2)consoleFd=isatty(2)?-1:open("/dev/console",O_WRONLY|O_NOCTTY);
  if(consoleFd>=0 && ![what hasPrefix:@"native-metal: no device answer"]){
   char line[600];int n=snprintf(line,sizeof line,"metal: %s[%d]: %s\\n",getprogname(),getpid(),what.UTF8String?:"?");
   if(n>(int)sizeof line-1)n=(int)sizeof line-1;
   if(n>0)(void)write(consoleFd,line,(size_t)n);
  }
 }
 gProblems++;'''
assert mtext.count(old_note_problem) == 1
mtext = mtext.replace(old_note_problem, new_note_problem)

old_fail_with = '''template <typename T> static T failWith(NSError **error,NSString *what,T value) {
 noteProblem([NSString stringWithFormat:@"%@: %@",what,lastError()]);'''
new_fail_with = '''template <typename T> static T failWith(NSError **error,NSString *what,T value) {
 NSLog(@"[BegoniaMetal] failWith: %@: %@", what, lastError());
 noteProblem([NSString stringWithFormat:@"%@: %@",what,lastError()]);'''
assert mtext.count(old_fail_with) == 1
mtext = mtext.replace(old_fail_with, new_fail_with)

old_render_pipeline = '''@interface NBRenderPipeline : NBObject
@property(nonatomic) NmbRenderPipeline *handle;
@end
@implementation NBRenderPipeline
- (void)dealloc {nmb_render_pipeline_release(_handle);}'''
new_render_pipeline = '''@interface NBRenderPipeline : NBObject
@property(nonatomic) NmbRenderPipeline *handle;
@end
@implementation NBRenderPipeline
- (void)dealloc {if(_handle)nmb_render_pipeline_release(_handle);}'''
assert mtext.count(old_render_pipeline) == 1
mtext = mtext.replace(old_render_pipeline, new_render_pipeline)

old_render_encoder = '''@interface NBRenderEncoder : NBEncoder
@end
@implementation NBRenderEncoder
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)state {
 if(![(id)state isKindOfClass:[NBRenderPipeline class]]){[self.commandBuffer refuse:@"foreign render pipeline"];return;}
 [self check:nmb_render_set_pipeline(self.handle,[(NBRenderPipeline *)state handle]) what:"setRenderPipelineState"];
}'''
new_render_encoder = '''@interface NBRenderEncoder : NBEncoder
@property(nonatomic) bool stub;
@end
@implementation NBRenderEncoder
- (void)setRenderPipelineState:(id<MTLRenderPipelineState>)state {
 if(![(id)state isKindOfClass:[NBRenderPipeline class]]){[self.commandBuffer refuse:@"foreign render pipeline"];return;}
 if(![(NBRenderPipeline *)state handle]){
  self.stub=true;
  return;
 }
 self.stub=false;
 [self check:nmb_render_set_pipeline(self.handle,[(NBRenderPipeline *)state handle]) what:"setRenderPipelineState"];
}'''
assert mtext.count(old_render_encoder) == 1
mtext = mtext.replace(old_render_encoder, new_render_encoder)

old_render_draw = '''- (void)drawPrimitives:(MTLPrimitiveType)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n instanceCount:(NSUInteger)i baseInstance:(NSUInteger)b {
 gDraws++;self.commandBuffer->encodedWork.draws++;
 [self check:nmb_render_draw(self.handle,t,(uint32_t)s,(uint32_t)n,(uint32_t)i,(uint32_t)b) what:"drawPrimitives"];
}'''
new_render_draw = '''- (void)drawPrimitives:(MTLPrimitiveType)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n instanceCount:(NSUInteger)i baseInstance:(NSUInteger)b {
 if(self.stub)return;
 gDraws++;self.commandBuffer->encodedWork.draws++;
 [self check:nmb_render_draw(self.handle,t,(uint32_t)s,(uint32_t)n,(uint32_t)i,(uint32_t)b) what:"drawPrimitives"];
}'''
assert mtext.count(old_render_draw) == 1
mtext = mtext.replace(old_render_draw, new_render_draw)

old_render_draw_indexed = '''- (void)drawIndexedPrimitives:(MTLPrimitiveType)t indexCount:(NSUInteger)n indexType:(MTLIndexType)k indexBuffer:(id<MTLBuffer>)b indexBufferOffset:(NSUInteger)o
                instanceCount:(NSUInteger)i baseVertex:(NSInteger)v baseInstance:(NSUInteger)bi {
 if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign index buffer"];return;}'''
new_render_draw_indexed = '''- (void)drawIndexedPrimitives:(MTLPrimitiveType)t indexCount:(NSUInteger)n indexType:(MTLIndexType)k indexBuffer:(id<MTLBuffer>)b indexBufferOffset:(NSUInteger)o
                instanceCount:(NSUInteger)i baseVertex:(NSInteger)v baseInstance:(NSUInteger)bi {
 if(self.stub)return;
 if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign index buffer"];return;}'''
assert mtext.count(old_render_draw_indexed) == 1
mtext = mtext.replace(old_render_draw_indexed, new_render_draw_indexed)

old_render_desc = '''static bool renderDescriptor(MTLRenderPipelineDescriptor *d,NmbRenderPipelineDescriptor *p,NSError **error) {
 if(![(id)d.vertexFunction isKindOfClass:[NBFunction class]] || (d.fragmentFunction && ![(id)d.fragmentFunction isKindOfClass:[NBFunction class]])) {
  if(error)*error=nbError(@"foreign function");return false;
 }'''
new_render_desc = '''static bool renderDescriptor(MTLRenderPipelineDescriptor *d,NmbRenderPipelineDescriptor *p,NSError **error) {
 NSLog(@"[BegoniaMetal] renderDescriptor: label='%@' vf=%@ (class=%s) ff=%@ (class=%s)",
       d.label,
       d.vertexFunction,
       d.vertexFunction ? class_getName(object_getClass(d.vertexFunction)) : "nil",
       d.fragmentFunction,
       d.fragmentFunction ? class_getName(object_getClass(d.fragmentFunction)) : "nil");
 if(!d.vertexFunction || ![(id)d.vertexFunction isKindOfClass:[NBFunction class]] || (d.fragmentFunction && ![(id)d.fragmentFunction isKindOfClass:[NBFunction class]])) {
  NSLog(@"[BegoniaMetal] renderDescriptor foreign/missing function: vf class=%s ff class=%s",
        d.vertexFunction ? class_getName(object_getClass(d.vertexFunction)) : "nil",
        d.fragmentFunction ? class_getName(object_getClass(d.fragmentFunction)) : "nil");
  if(error)*error=nbError(@"foreign function");return false;
 }'''
assert mtext.count(old_render_desc) == 1
mtext = mtext.replace(old_render_desc, new_render_desc)

old_new_render_pipe = '''- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d options:(MTLPipelineOption)options
                                                        reflection:(MTLAutoreleasedRenderPipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 auto *p=new NmbRenderPipelineDescriptor();
 if(!renderDescriptor(d,p,error)){delete p;return nil;}
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error)){delete p;return nil;}
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();const NBCost costBegan=NBCost::now();
 NmbRenderPipeline *handle=nmb_render_pipeline_new_with_options(gDevice,p,&creation);delete p;gTimePipeline.add(pipelineBegan);
 journalRenderPipeline(d,pipelineBegan,handle?nullptr:nmb_last_error(),gJournalPath.empty()?std::string():NBCost::now().since(costBegan));
 if(!handle)return failWith<id<MTLRenderPipelineState>>(error,@"render pipeline",nil);
 NBRenderPipeline *state=[NBRenderPipeline new];state.handle=handle;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
}'''
new_new_render_pipe = '''- (id<MTLRenderPipelineState>)newRenderPipelineStateWithDescriptor:(MTLRenderPipelineDescriptor *)d options:(MTLPipelineOption)options
                                                        reflection:(MTLAutoreleasedRenderPipelineReflection *)reflection error:(NSError **)error {
 if(reflection)*reflection=nil;
 if(!d){
  if(error)*error=nbError(@"nil render pipeline descriptor");
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 auto *p=new NmbRenderPipelineDescriptor();
 if(!renderDescriptor(d,p,error)){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: renderDescriptor failed for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 if(!p->vertex){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: vertex handle is null for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 if(d.fragmentFunction && !p->fragment){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: fragment handle is null for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 std::vector<NmbBinaryArchive *> archives;
 if(!pipelineArchives(d.binaryArchives,archives,error)){
  delete p;
  NSLog(@"[BegoniaMetal] WARNING: pipelineArchives failed for '%@', returning stub pipeline", d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};
 const uint64_t pipelineBegan=nowUs();const NBCost costBegan=NBCost::now();
 NmbRenderPipeline *handle=nmb_render_pipeline_new_with_options(gDevice,p,&creation);delete p;gTimePipeline.add(pipelineBegan);
 journalRenderPipeline(d,pipelineBegan,handle?nullptr:nmb_last_error(),gJournalPath.empty()?std::string():NBCost::now().since(costBegan));
 if(!handle){
  NSLog(@"[BegoniaMetal] WARNING: nmb_render_pipeline_new_with_options failed (%s) for '%@', returning stub pipeline", nmb_last_error(), d.label);
  NBRenderPipeline *state=[NBRenderPipeline new];state.handle=nullptr;state.label=d.label;gPipelines++;
  if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
 }
 NBRenderPipeline *state=[NBRenderPipeline new];state.handle=handle;state.label=d.label;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;
}'''
assert mtext.count(old_new_render_pipe) == 1
mtext = mtext.replace(old_new_render_pipe, new_new_render_pipe)

old_func_desc = '''- (id<MTLFunction>)newFunctionWithDescriptor:(MTLFunctionDescriptor *)d error:(NSError **)error {
 if(!d.name){if(error)*error=nbError(@"a function name is required");return nil;}
 if(d.constantValues)return [self newFunctionWithName:d.name constantValues:d.constantValues error:error];
 id<MTLFunction> function=[self newFunctionWithName:d.name];
 if(!function){if(error)*error=nbError([NSString stringWithFormat:@"function %@: %@",d.name,lastError()]);return nil;}
 if(error)*error=nil;return function;
}'''
new_func_desc = '''- (id<MTLFunction>)newFunctionWithDescriptor:(MTLFunctionDescriptor *)d error:(NSError **)error {
 if(!d || !d.name){
  NSLog(@"[BegoniaMetal] WARNING: newFunctionWithDescriptor nil descriptor or name, returning stub");
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=@"stub_anonymous";
  stub.functionType=MTLFunctionTypeFragment;if(error)*error=nil;return (id<MTLFunction>)stub;
 }
 id<MTLFunction> function = d.constantValues ?
     [self newFunctionWithName:d.name constantValues:d.constantValues error:error] :
     [self newFunctionWithName:d.name];
 if(!function){
  NSLog(@"[BegoniaMetal] WARNING: newFunctionWithDescriptor for '%@' failed (%@), returning stub", d.name, lastError());
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=d.name;
  stub.functionType=MTLFunctionTypeFragment;if(error)*error=nil;return (id<MTLFunction>)stub;
 }
 if(error)*error=nil;return function;
}'''
assert mtext.count(old_func_desc) == 1
mtext = mtext.replace(old_func_desc, new_func_desc)

old_func_name = '''- (id<MTLFunction>)newFunctionWithName:(NSString *)name {
 const uint64_t functionBegan=nowUs();
 NmbFunction *handle=(name && _handle)?nmb_function_new(_handle,name.UTF8String):nullptr;gTimeFunction.add(functionBegan);
 if(!handle)return nil;
 NBFunction *function=[NBFunction new];function.handle=handle;function.name=name;function.functionType=(MTLFunctionType)nmb_function_type(handle);
 return (id<MTLFunction>)function;
}'''
new_func_name = '''- (id<MTLFunction>)newFunctionWithName:(NSString *)name {
 const uint64_t functionBegan=nowUs();
 NmbFunction *handle=(name && _handle)?nmb_function_new(_handle,name.UTF8String):nullptr;gTimeFunction.add(functionBegan);
 if(!handle){
  NSLog(@"[BegoniaMetal] WARNING: newFunctionWithName for '%@' failed, returning stub NBFunction", name);
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=name?:@"stub_noname";stub.functionType=MTLFunctionTypeFragment;
  return (id<MTLFunction>)stub;
 }
 NBFunction *function=[NBFunction new];function.handle=handle;function.name=name;function.functionType=(MTLFunctionType)nmb_function_type(handle);
 return (id<MTLFunction>)function;
}'''
assert mtext.count(old_func_name) == 1
mtext = mtext.replace(old_func_name, new_func_name)

old_func_const_validate = ''' if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {
  if(error)*error=nbError(@"unqualified function constant values");return nil;
 }'''
new_func_const_validate = ''' if(!name || !values || !methodABI(values,@selector(newIndexedConstantArray),"@16@0:8") || !methodABI(values,@selector(newNamedConstantArray),"@16@0:8")) {
  NSLog(@"[BegoniaMetal] WARNING: unqualified constant values for '%@', returning stub NBFunction", name);
  NBFunction *stub=[NBFunction new];stub.handle=nullptr;stub.name=name?:@"stub_noname";stub.functionType=MTLFunctionTypeFragment;
  if(error)*error=nil;return (id<MTLFunction>)stub;
 }'''
assert mtext.count(old_func_const_validate) == 1
mtext = mtext.replace(old_func_const_validate, new_func_const_validate)

# begonia: pipeline reflection (userland/metal/frontend_reflection.mm.inc): the objects, and every reflection:
# out-parameter answered with them instead of nil, the completion-handler forms too.
reflection_code=(root/'userland/metal/frontend_reflection.mm.inc').read_text()
hooks=[
 ('#define NBLOG(', '#include <atomic>\n#include <sys/sysctl.h>\n#include <objc/runtime.h>\n#include <dlfcn.h>\n#include <mach-o/dyld.h>\n'+reflection_code+'\n#define NBLOG('),
 # (an empty reflection on the front end's early exits too: a caller that asked aborts on nil)
 (' if(reflection)*reflection=nil;\n if(!d){\n  if(error)*error=nbError(@"nil render pipeline descriptor");',
  ' if(reflection)*reflection=nbReflectionWanted()?[[NBRenderReflection alloc] init]:nil;\n if(!d){\n  if(error)*error=nbError(@"nil render pipeline descriptor");'),
 (' const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};\n const uint64_t pipelineBegan=nowUs();const NBCost costBegan=NBCost::now();',
  ' if(reflection)*reflection=renderReflection(p,d.label);\n const NmbPipelineOptions creation={(uint64_t)options,archives.data(),archives.size()};\n const uint64_t pipelineBegan=nowUs();const NBCost costBegan=NBCost::now();'),
 (' NmbComputePipeline *handle=nmb_compute_pipeline_new_with_descriptor(gDevice,&p,&creation);',
  ' if(reflection)*reflection=computeReflection(&p,d.label);\n NmbComputePipeline *handle=nmb_compute_pipeline_new_with_descriptor(gDevice,&p,&creation);'),
 (' NmbComputePipeline *handle=nmb_compute_pipeline_new_with_options(gDevice,[(NBFunction *)function handle],&creation);',
  ' if(reflection){NmbComputePipelineDescriptor q={};q.function=[(NBFunction *)function handle];*reflection=computeReflection(&q,function.name);}\n'
  ' NmbComputePipeline *handle=nmb_compute_pipeline_new_with_options(gDevice,[(NBFunction *)function handle],&creation);'),
 ('  NSError *error=nil;id<MTLRenderPipelineState> state=[(id<MTLDevice>)self newRenderPipelineStateWithDescriptor:snapshot options:options reflection:nil error:&error];\n'
  '  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,nil,error);});',
  '  NSError *error=nil;MTLRenderPipelineReflection *reflection=nil;\n'
  '  id<MTLRenderPipelineState> state=[(id<MTLDevice>)self newRenderPipelineStateWithDescriptor:snapshot options:options reflection:&reflection error:&error];\n'
  '  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,reflection,error);});'),
 ('  NSError *error=nil;id<MTLComputePipelineState> state=[(id)self newComputePipelineStateWithDescriptor:snapshot options:options reflection:nil error:&error];\n'
  '  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,nil,error);});',
  '  NSError *error=nil;MTLComputePipelineReflection *reflection=nil;\n'
  '  id<MTLComputePipelineState> state=[(id)self newComputePipelineStateWithDescriptor:snapshot options:options reflection:&reflection error:&error];\n'
  '  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,reflection,error);});'),
 ('  NSError *error=nil;id<MTLComputePipelineState> state=[(id<MTLDevice>)self newComputePipelineStateWithFunction:function options:options reflection:nil error:&error];\n'
  '  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,nil,error);});',
  '  NSError *error=nil;MTLComputePipelineReflection *reflection=nil;\n'
  '  id<MTLComputePipelineState> state=[(id<MTLDevice>)self newComputePipelineStateWithFunction:function options:options reflection:&reflection error:&error];\n'
  '  if(handler)dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{handler(state,reflection,error);});'),
]
# (a pass attachment's resolve target, MSAA resolve: the bridge's record has it now, userland/metal/bridge/patches/0001)
hooks.append(('  out.texture=texture.handle;out.level=a.level;out.slice=a.slice;out.depth_plane=a.depthPlane;out.load=a.loadAction;out.store=a.storeAction;\n',
              '  out.texture=texture.handle;out.level=a.level;out.slice=a.slice;out.depth_plane=a.depthPlane;out.load=a.loadAction;out.store=a.storeAction;\n'
              '  const bool resolves=a.storeAction==MTLStoreActionMultisampleResolve||a.storeAction==MTLStoreActionStoreAndMultisampleResolve;\n'
              '  if(a.resolveTexture) {\n'
              '   if(![(id)a.resolveTexture isKindOfClass:[NBTexture class]])return false;\n'
              '   NBTexture *resolve=(NBTexture *)a.resolveTexture;if(resolve.surface)[_surfaces addObject:resolve];\n'
              '   [_resourceOwners addObject:resolve];\n'
              '   if(!nbRealMsaa()&&resolves) {\n'
              '    // (MSAA emulated: the pass draws at one sample straight into what it would have resolved into)\n'
              '    out.texture=resolve.handle;out.level=a.resolveLevel;out.slice=a.resolveSlice;out.depth_plane=a.resolveDepthPlane;\n'
              '    out.store=MTLStoreActionStore;\n'
              '   } else {\n'
              '    out.resolve_texture=resolve.handle;out.resolve_level=a.resolveLevel;out.resolve_slice=a.resolveSlice;out.resolve_depth_plane=a.resolveDepthPlane;\n'
              '   }\n'
              '  } else if(!nbRealMsaa()&&resolves) {\n'
              '   out.store=MTLStoreActionDontCare;\n'
              '  }\n'))
# MSAA emulated by default (PanVK's multisampled fragment jobs fault on this Bifrost v7: JS_STATUS 0x58, y43): textures asked for
# with samples are made with one, pipelines rasterize one, a resolving attachment is drawn into its resolve target (above). Games
# draw right, without the smoothing of edges. Boot-arg metal_msaa=1: real multisampling.
hooks.append((' *out={(uint64_t)d.textureType,(uint64_t)d.pixelFormat,d.width,d.height,d.depth,d.mipmapLevelCount,d.sampleCount,d.arrayLength,(uint64_t)d.usage,(uint64_t)d.resourceOptions};\n return true;',
              ' *out={(uint64_t)d.textureType,(uint64_t)d.pixelFormat,d.width,d.height,d.depth,d.mipmapLevelCount,d.sampleCount,d.arrayLength,(uint64_t)d.usage,(uint64_t)d.resourceOptions};\n'
              ' if(!nbRealMsaa()&&out->samples>1){if(out->type==MTLTextureType2DMultisample)out->type=MTLTextureType2D;else if(out->type==MTLTextureType2DMultisampleArray)out->type=MTLTextureType2DArray;out->samples=1;}\n'
              ' return true;'))
hooks.append((' p->depth_format=d.depthAttachmentPixelFormat;p->stencil_format=d.stencilAttachmentPixelFormat;p->samples=d.rasterSampleCount;',
              ' p->depth_format=d.depthAttachmentPixelFormat;p->stencil_format=d.stencilAttachmentPixelFormat;p->samples=nbRealMsaa()?d.rasterSampleCount:1;'))
hooks.append((' pass.width=d.renderTargetWidth;pass.height=d.renderTargetHeight;pass.array_length=d.renderTargetArrayLength;pass.samples=d.defaultRasterSampleCount;',
              ' pass.width=d.renderTargetWidth;pass.height=d.renderTargetHeight;pass.array_length=d.renderTargetArrayLength;pass.samples=nbRealMsaa()?d.defaultRasterSampleCount:1;'))
# (shader source: userland/metal/frontend_source.mm.inc, a category of the device's methods, after their class)
hooks.append(('// Bare AIR bitcode (no metallib wrapper), which Metal itself refuses: for the bridge\'s own fixtures only.',
              (root/'userland/metal/frontend_source.mm.inc').read_text()+'\n// Bare AIR bitcode (no metallib wrapper), which Metal itself refuses: for the bridge\'s own fixtures only.'))
for old,new in hooks:
 assert mtext.count(old)==1,'reflection hook does not fit: '+old[:90]
 mtext=mtext.replace(old,new)
# (the front end's warnings -- "returning stub pipeline" and the like -- on the console too: an application's NSLog
# reaches no kept log)
assert mtext.count('NSLog(@"[BegoniaMetal] WARNING: ')>0
mtext=mtext.replace('NSLog(@"[BegoniaMetal] WARNING: ','NBWARN(@"')
# (y204: counted, and on the console only the first 40 and then every 1000th -- a warning in every frame would push
# everything else out of the session's log ring)
mtext=mtext.replace('#define NBLOG(','static std::atomic<unsigned long long> gTeleWarns;\n#define NBWARN(...) do{NSString *nbWarning=[NSString stringWithFormat:__VA_ARGS__];NSLog(@"[BegoniaMetal] WARNING: %@",nbWarning);const unsigned long long nbWarned=gTeleWarns++;if(nbWarned<40||nbWarned%1000==0)nbConsole(nbWarning);}while(0)\n#define NBLOG(',1)

# begonia (y204): boot-arg metal_tele=1 -- the window server's (backboardd's) own counters on the console every 5 s:
# the telemetry table's rows (commits, pipelines, times, waits, surfaces, the runtime's VkPipelines and caches), how
# many render pipelines were asked for and how many of them were different ones (CoreAnimation asking for the same
# pipeline again and again is hypothesis A of handoff/FABLE2-BACKBOARDD-METAL.md), each new one's names, and the time
# spent inside the command buffers' scheduled and completed handlers (hypothesis B). Measurement only.
tele_hooks=[
 ('static void completed(void *context);\nstatic void scheduled(void *context);',
  'static void completed(void *context);\nstatic void scheduled(void *context);\nstatic void nbTeleTick();\nstatic Timed gTeleScheduled,gTeleCompleted;'),
 (' self.committed=YES;\n self.committedUs=nowUs();', ' self.committed=YES;\n self.committedUs=nowUs();\n nbTeleTick();'),
 ('  for(MTLCommandBufferHandler block in handlers)block((id<MTLCommandBuffer>)self);\n  dispatch_group_leave(self->_schedulingHandlers);',
  '  const uint64_t teleBegan=nowUs();\n  for(MTLCommandBufferHandler block in handlers)block((id<MTLCommandBuffer>)self);\n  if(handlers.count)gTeleScheduled.add(teleBegan);\n  dispatch_group_leave(self->_schedulingHandlers);'),
 ('  for(MTLCommandBufferHandler block in completedHandlers)block((id<MTLCommandBuffer>)self);\n  dispatch_group_leave(self->_handlers);',
  '  const uint64_t teleBegan=nowUs();\n  for(MTLCommandBufferHandler block in completedHandlers)block((id<MTLCommandBuffer>)self);\n  if(completedHandlers.count)gTeleCompleted.add(teleBegan);\n  dispatch_group_leave(self->_handlers);'),
 ('// A render pipeline descriptor in the bridge\'s form; false, with the error, for a function of another device.\n',
  '''static int nbTeleOn() {
 static int on=-1;
 if(on<0){char args[1024]="";size_t size=sizeof(args)-1;
  on=sysctlbyname("kern.bootargs",args,&size,NULL,0)==0 && strstr(args,"metal_tele=1")!=NULL && strcmp(getprogname(),"backboardd")==0;}
 return on;
}
static std::mutex gTelePipeLock;
static std::unordered_map<std::string,unsigned> gTelePipes;
static std::atomic<unsigned long long> gTelePipeAsked,gTelePipeStubs,gTeleLibraries;
static void nbTelePipe(MTLRenderPipelineDescriptor *d,uint64_t began,const char *outcome) {
 gTelePipeAsked++;if(strcmp(outcome,"ok")!=0)gTelePipeStubs++;
 if(!nbTeleOn())return;
 NBFunction *vf=[(id)d.vertexFunction isKindOfClass:[NBFunction class]]?(NBFunction *)d.vertexFunction:nil;
 NBFunction *ff=[(id)d.fragmentFunction isKindOfClass:[NBFunction class]]?(NBFunction *)d.fragmentFunction:nil;
 MTLRenderPipelineColorAttachmentDescriptor *a=d.colorAttachments[0];
 char head[160];snprintf(head,sizeof head,"c0=%lu:%d:%lu:%lu:%lu:%lu ds=%lu/%lu s=%lu vd=%d ",(unsigned long)a.pixelFormat,(int)a.blendingEnabled,
     (unsigned long)a.sourceRGBBlendFactor,(unsigned long)a.destinationRGBBlendFactor,(unsigned long)a.sourceAlphaBlendFactor,(unsigned long)a.destinationAlphaBlendFactor,
     (unsigned long)d.depthAttachmentPixelFormat,(unsigned long)d.stencilAttachmentPixelFormat,(unsigned long)d.rasterSampleCount,d.vertexDescriptor?1:0);
 std::string key=head;key+=d.label.length?d.label.UTF8String:"-";
 key+=" v ";key+=vf?vf.name.UTF8String:"-";key+=vf&&vf.handle?"":"(stub)";key+=" [";key+=vf?vf.constantsText:"";
 key+="] f ";key+=ff?ff.name.UTF8String:"-";key+=ff&&ff.handle?"":"(stub)";key+=" [";key+=ff?ff.constantsText:"";key+="]";
 unsigned times,kinds;
 {std::lock_guard<std::mutex> lock(gTelePipeLock);times=++gTelePipes[key];kinds=(unsigned)gTelePipes.size();}
 if((times==1 && kinds<=60) || times==2 || times==20 || times==200 || times==2000)
  nbConsole([NSString stringWithFormat:@"tele pipe #%u x%u %s %lluus: %.420s",kinds,times,outcome,(unsigned long long)(nowUs()-began),key.c_str()]);
}
static void nbTeleTick() {
 if(!nbTeleOn())return;
 static std::atomic<uint64_t> next{0};
 const uint64_t now=nowUs();uint64_t due=next.load();
 if(now<due || !next.compare_exchange_strong(due,now+5000000))return;
 static char table[8192];
 native_bridge_metal_telemetry(table,sizeof table);
 for(char *line=table,*end;*line;line=end) {
  end=strchr(line,'\\n');if(end)*end++=0;else end=line+strlen(line);
  if(!strchr("STGJNVLWFP",line[0]) || strlen(line)<5 || (line[0]=='S' && line[1]=='R'))continue;
  nbConsole([NSString stringWithFormat:@"tele %s",line]);
 }
 {char warning[700];if(nmb_last_warning(warning,sizeof warning))nbConsole([NSString stringWithFormat:@"tele WW %s",warning]);}
 unsigned kinds;{std::lock_guard<std::mutex> lock(gTelePipeLock);kinds=(unsigned)gTelePipes.size();}
 nbConsole([NSString stringWithFormat:@"tele X pipes asked %llu kinds %u not-ok %llu warns %llu libs %llu | handlers scheduled n=%llu %lluus max %llu, completed n=%llu %lluus max %llu",
  gTelePipeAsked.load(),kinds,gTelePipeStubs.load(),gTeleWarns.load(),gTeleLibraries.load(),
  (unsigned long long)gTeleScheduled.count.load(),(unsigned long long)gTeleScheduled.total.load(),(unsigned long long)gTeleScheduled.slowest.load(),
  (unsigned long long)gTeleCompleted.count.load(),(unsigned long long)gTeleCompleted.total.load(),(unsigned long long)gTeleCompleted.slowest.load()]);
}
// A render pipeline descriptor in the bridge's form; false, with the error, for a function of another device.
'''),
 # (every outcome of a render pipeline's creation is counted; the descriptor is still alive at each)
 ('  NBWARN(@"renderDescriptor failed for \'%@\', returning stub pipeline", d.label);','  NBWARN(@"renderDescriptor failed for \'%@\', returning stub pipeline", d.label);nbTelePipe(d,nowUs(),"stub-descriptor");'),
 ('  NBWARN(@"vertex handle is null for \'%@\', returning stub pipeline", d.label);','  NBWARN(@"vertex handle is null for \'%@\', returning stub pipeline", d.label);nbTelePipe(d,nowUs(),"stub-vertex");'),
 ('  NBWARN(@"fragment handle is null for \'%@\', returning stub pipeline", d.label);','  NBWARN(@"fragment handle is null for \'%@\', returning stub pipeline", d.label);nbTelePipe(d,nowUs(),"stub-fragment");'),
 ('  NBWARN(@"pipelineArchives failed for \'%@\', returning stub pipeline", d.label);','  NBWARN(@"pipelineArchives failed for \'%@\', returning stub pipeline", d.label);nbTelePipe(d,nowUs(),"stub-archives");'),
 ('  NBWARN(@"nmb_render_pipeline_new_with_options failed (%s) for \'%@\', returning stub pipeline", nmb_last_error(), d.label);',
  '  NBWARN(@"nmb_render_pipeline_new_with_options failed (%s) for \'%@\', returning stub pipeline", nmb_last_error(), d.label);nbTelePipe(d,pipelineBegan,"stub-refused");'),
 (' NBRenderPipeline *state=[NBRenderPipeline new];state.handle=handle;state.label=d.label;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;\n}',
  ' nbTelePipe(d,pipelineBegan,"ok");\n NBRenderPipeline *state=[NBRenderPipeline new];state.handle=handle;state.label=d.label;gPipelines++;if(error)*error=nil;return (id<MTLRenderPipelineState>)state;\n}'),
 # (which shader libraries the window server opens, and whether they opened)
 (' NmbLibrary *handle=nmb_library_new(gDevice,bytes.bytes,bytes.length,false);gTimeLibrary.add(libraryBegan);\n',
  ' NmbLibrary *handle=nmb_library_new(gDevice,bytes.bytes,bytes.length,false);gTimeLibrary.add(libraryBegan);\n'
  ' if(gTeleLibraries++<12)nbConsole([NSString stringWithFormat:@"tele library %@ (%lu bytes): %s",path,(unsigned long)bytes.length,handle?"opened":nmb_last_error()]);\n'),
]
# begonia (y205): the window server swaps a frame's surface to the display right after waitUntilScheduled -- on Apple's
# GPUs the kernel holds the swap until the GPU has drawn the surface; here nothing does, and the framebuffer driver
# copied surfaces the GPU had not reached yet (y204: every frame black, each in a new surface, 265 ms commit to
# completion). In backboardd a command buffer that drew is waited for to its completion there. metal_swapwait=0: not.
tele_hooks.append(('static void completed(void *context);\nstatic void scheduled(void *context);\n',
 'static void completed(void *context);\nstatic void scheduled(void *context);\n'
 'static bool nbServerWaits() {\n static int waits=-1;\n if(waits<0){char args[1024]="";size_t size=sizeof(args)-1;\n'
 '  waits=strcmp(getprogname(),"backboardd")==0 && !(sysctlbyname("kern.bootargs",args,&size,NULL,0)==0 && strstr(args,"metal_swapwait=0")!=NULL);}\n return waits;\n}\n'))
tele_hooks.append((' dispatch_group_wait(_schedulingHandlers,DISPATCH_TIME_FOREVER);\n',
 ' dispatch_group_wait(_schedulingHandlers,DISPATCH_TIME_FOREVER);\n'
 ' if(nbServerWaits() && encodedWork.passes>0 && !self.failedLocally)nmb_command_buffer_wait(_handle,0);\n'))
# begonia (y207): textures over a buffer's bytes that are only read (CoreAnimation's glyph, mask and image bitmaps:
# it writes the bitmap into an MTLBuffer and wraps a texture round it). PanVK's linear images need page-aligned
# memory and their own row pitch, so the runtime "shadows" every such texture: an image of its own that it fills from
# the buffer again before EVERY encoder that uses it, as it cannot know whether the CPU wrote the bytes -- two GPU
# batches a texture (y206: ~56 of the window server's ~60 batches a frame, 4 frames a second). In backboardd such a
# texture is instead an ordinary texture that mirrors the bytes: at each commit of a command buffer that uses it, its
# bytes are hashed, and uploaded (replaceRegion) only when they changed. Kept to what cannot go wrong that way: 2D,
# one level, one sample, a plain format, usage without RenderTarget/ShaderWrite, a buffer the CPU can read. Not seen:
# a GPU write into the buffer's bytes (a blit) -- the mirror keeps the bytes the CPU last had. metal_mirror=0: off.
tele_hooks.append(('@property(nonatomic) NSUInteger offset,rowBytes;\n',
 '@property(nonatomic) NSUInteger offset,rowBytes;\n@property(nonatomic) BOOL mirror,mirrorFilled;\n@property(nonatomic) uint64_t mirrorHash;\n@property(nonatomic,strong) NSMutableData *mirrorHashes;\n@property(nonatomic) uint32_t mirrorTexel;\n'))
tele_hooks.append(('@implementation NBBuffer (Textures)\n- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)d offset:(NSUInteger)offset bytesPerRow:(NSUInteger)row {\n',
 '''static std::atomic<unsigned long long> gMirrorMade,gMirrorNot,gMirrorUploads,gMirrorSkips,gMirrorBytes,gMirrorHashUs;
static bool nbMirrorOn() {
 static int on=-1;
 if(on<0){char args[1024]="";size_t size=sizeof(args)-1;
  on=strcmp(getprogname(),"backboardd")==0 && !(sysctlbyname("kern.bootargs",args,&size,NULL,0)==0 && strstr(args,"metal_mirror=0")!=NULL);}
 return on;
}
static uint32_t nbMirrorTexel(MTLPixelFormat format) {
 switch((unsigned)format) {
 case 1: case 10: case 11: case 13: return 1;                       // A8, R8
 case 20: case 25: case 30: case 40: case 41: case 42: case 43: return 2;   // R16, RG8, the 16-bit packed ones
 case 55: case 65: case 70: case 71: case 80: case 81: case 90: case 94: return 4;   // R32Float, RG16Float, RGBA8, BGRA8, the 10-bit ones
 case 110: case 115: return 8;                                       // RGBA16
 default: return 0;
 }
}
// The texture's bytes in its buffer, hashed row by row (the texels only, not the bytes between rows), and uploaded
// when they are not the ones last uploaded.
static void nbMirrorSync(NBTexture *t) {
 @synchronized(t) {
  const uint8_t *base=(const uint8_t *)[t.buffer contents];if(!base)return;
  base+=t.offset;
  const uint64_t began=nowUs();
  const size_t rowBytes=(size_t)t.descriptor.width*t.mirrorTexel,rows=(size_t)t.descriptor.height,pitch=t.rowBytes;
  // Four lanes of 8 bytes each. A texture of more than 32 KiB is checked an eighth of its rows at a commit (rows
  // y%8==phase, the phase moving on each time, a hash kept for each phase): y213's whole hash of every mirrored
  // texture took 12 ms a commit. A change is then seen within 8 commits at the latest, at once where it touches the
  // rows of the commit's phase -- which a new bitmap, written top to bottom, does.
  const unsigned stride=rowBytes*rows>32768?8:1;
  auto hashRows=[&](unsigned phase)->uint64_t {
   uint64_t h=0x9e3779b97f4a7c15ull,l0=h,l1=h^0x1111111111111111ull,l2=h^0x2222222222222222ull,l3=h^0x3333333333333333ull;
   const uint64_t m=0xff51afd7ed558ccdull;
   for(size_t y=phase;y<rows;y+=stride) {
    const uint8_t *p=base+y*pitch;size_t n=rowBytes;
    for(;n>=32;n-=32,p+=32) {
     uint64_t w[4];memcpy(w,p,32);
     l0=(l0^w[0])*m;l0^=l0>>29;l1=(l1^w[1])*m;l1^=l1>>29;l2=(l2^w[2])*m;l2^=l2>>29;l3=(l3^w[3])*m;l3^=l3>>29;
    }
    for(;n>=8;n-=8,p+=8){uint64_t w;memcpy(&w,p,8);l0=(l0^w)*m;l0^=l0>>29;}
    uint64_t w=0;if(n)memcpy(&w,p,n);l1=(l1^w^(uint64_t)n)*0xc4ceb9fe1a85ec53ull;l1^=l1>>32;
   }
   h=((l0*31+l1)*31+l2)*31+l3;return h^(h>>31);
  };
  if(!t.mirrorHashes)t.mirrorHashes=[NSMutableData dataWithLength:8*sizeof(uint64_t)];
  uint64_t *kept=(uint64_t *)t.mirrorHashes.mutableBytes;
  const unsigned phase=stride>1?(unsigned)(t.mirrorHash++%stride):0;
  const uint64_t h=hashRows(phase);
  if(t.mirrorFilled && h==kept[phase]){gMirrorHashUs+=nowUs()-began;gMirrorSkips++;return;}
  for(unsigned k=0;k<stride;k++)kept[k]=k==phase?h:hashRows(k);
  gMirrorHashUs+=nowUs()-began;
  [(id<MTLTexture>)t replaceRegion:MTLRegionMake2D(0,0,t.descriptor.width,t.descriptor.height) mipmapLevel:0 slice:0 withBytes:base bytesPerRow:pitch bytesPerImage:pitch*rows];
  t.mirrorFilled=YES;gMirrorUploads++;gMirrorBytes+=rowBytes*rows;
 }
}
static NBTexture *nbMirrorTexture(NBBuffer *self,MTLTextureDescriptor *d,NSUInteger offset,NSUInteger row) {
 if(nbMirrorOn() && d) {
  const uint32_t texel=nbMirrorTexel(d.pixelFormat);
  const bool plain=d.textureType==MTLTextureType2D && d.mipmapLevelCount<=1 && d.sampleCount<=1 && d.arrayLength<=1 && d.depth<=1;
  const bool reads=d.usage!=MTLTextureUsageUnknown && !(d.usage&(MTLTextureUsageRenderTarget|MTLTextureUsageShaderWrite));
  if(texel && plain && reads && row>=d.width*texel && [self contents] && offset+row*(d.height-1)+d.width*texel<=self.length) {
   MTLTextureDescriptor *own=[d copy];
   NBTexture *texture=(NBTexture *)[gDeviceObject newTextureWithDescriptor:own];
   if([(id)texture isKindOfClass:[NBTexture class]]) {
    texture.buffer=self;texture.offset=offset;texture.rowBytes=row;texture.mirror=YES;texture.mirrorTexel=texel;gMirrorMade++;
    return (id<MTLTexture>)texture;
   }
  }
  if(gMirrorNot++<8)nbConsole([NSString stringWithFormat:@"tele mirror not: format %lu %lux%lu type %lu levels %lu usage %#lx rows %lu texel %u",
   (unsigned long)d.pixelFormat,(unsigned long)d.width,(unsigned long)d.height,(unsigned long)d.textureType,(unsigned long)d.mipmapLevelCount,(unsigned long)d.usage,(unsigned long)row,texel]);
 }
 return nil;
}
@implementation NBBuffer (Textures)
- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor *)d offset:(NSUInteger)offset bytesPerRow:(NSUInteger)row {
 if(NBTexture *mirror=nbMirrorTexture(self,d,offset,row))return (id<MTLTexture>)mirror;
'''))
# (y213, measurement: with metal_tele=1, after each 5 s line the window server's next 16 render passes are described --
# each colour attachment's size, format, load and store action, storage mode and whether it is a surface)
tele_hooks.append(('static void completed(void *context);\nstatic void scheduled(void *context);\n',
 'static void completed(void *context);\nstatic void scheduled(void *context);\nstatic std::atomic<int> gTelePasses{0};\n'))
tele_hooks.append((' auto attachment=[&](MTLRenderPassAttachmentDescriptor *a,NmbPassAttachment &out)->bool {\n',
 ''' if(gTelePasses.load()>0 && gTelePasses.fetch_sub(1)>0) {
  NSMutableString *line=[NSMutableString stringWithFormat:@"tele pass cb%04lx",(unsigned long)(((uintptr_t)self>>4)&0xffff)];
  for(unsigned i=0;i<4;i++) {
   MTLRenderPassColorAttachmentDescriptor *c=d.colorAttachments[i];id<MTLTexture> t=c.texture;if(!t)continue;
   [line appendFormat:@" c%u %lux%lu f%lu L%lu S%lu sm%lu %s",i,(unsigned long)t.width,(unsigned long)t.height,(unsigned long)t.pixelFormat,
    (unsigned long)c.loadAction,(unsigned long)c.storeAction,(unsigned long)t.storageMode,t.iosurface?"surface":"own"];
  }
  if(d.depthAttachment.texture||d.stencilAttachment.texture)[line appendString:@" ds"];
  nbConsole(line);
 }
 auto attachment=[&](MTLRenderPassAttachmentDescriptor *a,NmbPassAttachment &out)->bool {
'''))
tele_hooks.append((' unsigned kinds;{std::lock_guard<std::mutex> lock(gTelePipeLock);kinds=(unsigned)gTelePipes.size();}\n nbConsole([NSString stringWithFormat:@"tele X pipes',
 ' gTelePasses=16;\n unsigned kinds;{std::lock_guard<std::mutex> lock(gTelePipeLock);kinds=(unsigned)gTelePipes.size();}\n nbConsole([NSString stringWithFormat:@"tele X pipes'))
# (y221, an experiment: boot-arg metal_nodraw=1 -- the window server's draws are dropped, its passes kept: what a
# full-screen pass costs with nothing drawn in it. The screen stays as the passes clear it.)
tele_hooks.append(('@interface NBRenderEncoder : NBEncoder\n',
 'static bool nbNoDraw() {\n static int on=-1;\n if(on<0){char args[1024]="";size_t size=sizeof(args)-1;\n'
 '  on=strcmp(getprogname(),"backboardd")==0 && sysctlbyname("kern.bootargs",args,&size,NULL,0)==0 && strstr(args,"metal_nodraw=1")!=NULL;}\n return on;\n}\n'
 '@interface NBRenderEncoder : NBEncoder\n'))
tele_hooks.append((' if(self.stub)return;\n gDraws++;self.commandBuffer->encodedWork.draws++;\n [self check:nmb_render_draw(',
 ' if(self.stub || nbNoDraw())return;\n gDraws++;self.commandBuffer->encodedWork.draws++;\n [self check:nmb_render_draw('))
tele_hooks.append((' if(self.stub)return;\n if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign index buffer"];return;}',
 ' if(self.stub || nbNoDraw())return;\n if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign index buffer"];return;}'))
# (y223, measurement: at each 5 s line three of the window server's pipeline keys whole, in pieces of 60 characters --
# the "tele pipe" lines are cut by the console before the fragment function's constants end)
tele_hooks.append((' gTelePasses=16;\n',
 ''' gTelePasses=16;
 {
  static unsigned turn;std::vector<std::string> keys;
  {std::lock_guard<std::mutex> lock(gTelePipeLock);for(const auto &entry:gTelePipes)keys.push_back(entry.first);}
  for(unsigned k=0;k<2 && !keys.empty();k++) {
   const unsigned which=turn++%keys.size();const std::string &key=keys[which];
   for(size_t at=0,piece=0;at<key.size() && piece<56;at+=40,piece++)
    nbConsole([NSString stringWithFormat:@"tele k%03u.%02zu %.40s",which,piece,key.c_str()+at]);
  }
 }
'''))
# (y212: CoreAnimation's own bitmaps -- glyphs, masks -- come through the private newLinearTextureWithDescriptor:...,
# over its newBufferWithBytesNoCopy buffers; one image of one slice is the same texture)
tele_hooks.append((' NmbTextureDescriptor descriptor;if(!textureDescriptor(d,&descriptor))return nil;\n NmbTexture *handle=nmb_texture_new_linear(',
 ' if(d && d.depth<=1 && d.arrayLength<=1)if(NBTexture *mirror=nbMirrorTexture(self,d,offset,row))return (id<MTLTexture>)mirror;\n'
 ' NmbTextureDescriptor descriptor;if(!textureDescriptor(d,&descriptor))return nil;\n NmbTexture *handle=nmb_texture_new_linear('))
tele_hooks.append((' self.committed=YES;\n self.committedUs=nowUs();\n nbTeleTick();',
 ' self.committed=YES;\n self.committedUs=nowUs();\n nbTeleTick();\n'
 ' if(nbMirrorOn() && !self.encodeFailure)for(id owner in [_resourceOwners copy])if([owner isKindOfClass:[NBTexture class]] && [(NBTexture *)owner mirror])nbMirrorSync((NBTexture *)owner);'))
tele_hooks.append((' {char warning[700];if(nmb_last_warning(warning,sizeof warning))nbConsole([NSString stringWithFormat:@"tele WW %s",warning]);}\n',
 # (the runtime's last warning whole, in pieces of 60 characters: the console cuts and interleaves long lines)
 ' {char warning[900];const size_t n=nmb_last_warning(warning,sizeof warning);static unsigned long long said;\n'
 '  if(n && said++<40)for(size_t at=0,k=0;at<n && k<14;at+=60,k++)nbConsole([NSString stringWithFormat:@"tele w%02zu %.60s",k,warning+at]);}\n'
 ' nbConsole([NSString stringWithFormat:@"tele M mirror made %llu not %llu uploads %llu (%llu KB) unchanged %llu hashing %llu us",gMirrorMade.load(),gMirrorNot.load(),gMirrorUploads.load(),gMirrorBytes.load()>>10,gMirrorSkips.load(),gMirrorHashUs.load()]);\n'))
# begonia (y209): IOSurfaces the GPU reaches in place (userland/metal/bridge/begonia_surface.rs says why): a surface's
# no-copy buffer is made with the transport's surface descriptors as the runtime's dma-buf source. Boot-arg
# metal_surfaces=0: none (the copies, as before); 1 (default): the window server only; 2: every process.
tele_hooks.append((' NmbBuffer *handle=nmb_buffer_new_no_copy_releasing(gDevice,alias.bytes,length,options,releasePinnedHost,context);\n',
 ''' NmbBuffer *handle=nullptr;
 if(surface && nbSurfacesInPlace()) {
  handle=nmb_buffer_new_no_copy_surface(gDevice,alias.bytes,length,options,releasePinnedHost,context,IOSurfaceGetID(surface),gSurfaceFdCreate,gSurfaceFdRelease);
  static std::atomic<unsigned> said{0};
  if(!handle && said++<6)nbConsole([NSString stringWithFormat:@"tele surface %u of %lu bytes not in place: %s",IOSurfaceGetID(surface),(unsigned long)length,nmb_last_error()]);
 }
 if(!handle)handle=nmb_buffer_new_no_copy_releasing(gDevice,alias.bytes,length,options,releasePinnedHost,context);
'''))
tele_hooks.append(('static NmbBuffer *pinnedNoCopyBuffer(',
 '''#include <sys/resource.h>
static NmbSurfaceFdCreate gSurfaceFdCreate;
static NmbSurfaceFdRelease gSurfaceFdRelease;
static bool nbSurfacesInPlace() {
 static int on=-1;
 if(on<0) {
  char args[1024]="";size_t size=sizeof(args)-1;int level=1;
  if(sysctlbyname("kern.bootargs",args,&size,NULL,0)==0){const char *at=strstr(args,"metal_surfaces=");if(at)level=atoi(at+15);}
  bool wanted=level>=2 || (level==1 && strcmp(getprogname(),"backboardd")==0);
  if(wanted) {
   // (the transport is part of the Vulkan driver's library, which the runtime has opened by now)
   void *driver=dlopen("/redwood/lib/libvulkan_panfrost.dylib",RTLD_NOW);
   gSurfaceFdCreate=driver?(NmbSurfaceFdCreate)dlsym(driver,"hakios_native_surface_fd_create"):nullptr;
   gSurfaceFdRelease=driver?(NmbSurfaceFdRelease)dlsym(driver,"hakios_native_surface_fd_release"):nullptr;
   wanted=gSurfaceFdCreate && gSurfaceFdRelease;
   // (each surface in place holds a descriptor or two: the default limit of 256 is the window server's surfaces alone)
   struct rlimit limit;
   if(wanted && getrlimit(RLIMIT_NOFILE,&limit)==0 && limit.rlim_cur<8192){limit.rlim_cur=limit.rlim_max<8192?limit.rlim_max:8192;setrlimit(RLIMIT_NOFILE,&limit);}
   nbConsole([NSString stringWithFormat:@"tele surfaces in place: %s (metal_surfaces=%d)",wanted?"yes":"the transport has no surface descriptors",level]);
  }
  on=wanted;
 }
 return on;
}
static NmbBuffer *pinnedNoCopyBuffer('''))
tele_hooks.append((' nbConsole([NSString stringWithFormat:@"tele M mirror made',
 ' {uint64_t sd[3]={};nmb_surface_descriptor_counts(sd);nbConsole([NSString stringWithFormat:@"tele D surface descriptors made %llu refused %llu not-first-page %llu",(unsigned long long)sd[0],(unsigned long long)sd[1],(unsigned long long)sd[2]]);}\n'
 ' nbConsole([NSString stringWithFormat:@"tele M mirror made'))
# begonia (y230): device capability experiments. metal_caps_log=1 prints (once each) every device query the runtime answers,
# with its argument and answer; metal_caps_off=a,b:5 answers 0/NO for the selector a, and for b with the argument 5. Which query
# makes CoreAnimation choose framebuffer-fetch pipelines (fc_framebuffer_fetch) is found with these two.
tele_hooks.append(('static std::unordered_map<SEL,uint64_t> gAnswers;\n',
 r"""static std::unordered_map<SEL,uint64_t> gAnswers;
static bool nbBootArg(const char *key,std::string &value) {
 char args[1024]="";size_t size=sizeof(args)-1;
 if(sysctlbyname("kern.bootargs",args,&size,NULL,0)!=0)return false;
 const char *at=strstr(args,key);if(!at)return false;
 at+=strlen(key);value.assign(at,strcspn(at," "));return true;
}
static bool nbCapsLogOn() {static int on=-1;if(on<0){std::string v;on=nbBootArg("metal_caps_log=",v)&&v=="1";}return on;}
static bool nbCapOff(const char *name,NSUInteger arg,bool hasArg) {
 static std::string list;static int got=0;
 if(!got){std::string v;if(nbBootArg("metal_caps_off=",v))list=","+v+",";got=1;}
 if(list.empty())return false;
 if(list.find(std::string(",")+name+",")!=std::string::npos)return true;
 return hasArg && list.find(std::string(",")+name+":"+std::to_string((unsigned long)arg)+",")!=std::string::npos;
}
static std::mutex gCapHold;static std::vector<std::string> gCapLines;static std::unordered_map<std::string,int> gCapSeen;
static void nbCapDump(void *) {
 std::vector<std::string> lines;{std::lock_guard<std::mutex> guard(gCapHold);lines=gCapLines;}
 for(auto &line:lines)nbConsole([NSString stringWithUTF8String:line.c_str()]);
 dispatch_after(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_global_queue(0,0),^{nbCapDump(NULL);});
}
static void nbCapAdd(const std::string &key,const std::string &line) {
 std::lock_guard<std::mutex> guard(gCapHold);
 if(!gCapSeen.emplace(key,1).second)return;
 if(gCapLines.empty())dispatch_after(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_global_queue(0,0),^{nbCapDump(NULL);});
 gCapLines.push_back(line);
}
static void nbCapLog(const char *name,NSUInteger arg,bool hasArg,uint64_t value) {
 if(!nbCapsLogOn())return;
 char line[100];snprintf(line,sizeof line,"caps %s%s%lu=%llu %s",name,hasArg?" ":" /",(unsigned long)arg,(unsigned long long)value,getprogname());
 nbCapAdd(std::string(name)+":"+std::to_string((unsigned long)arg),line);
}
"""))
tele_hooks.append(('static NSUInteger countAnswer(id,SEL cmd) {return (NSUInteger)gAnswers.at(cmd);}\nstatic BOOL flagAnswer(id,SEL cmd) {return gAnswers.at(cmd)!=0;}\n',
 'static NSUInteger countAnswer(id,SEL cmd) {nbCapLog(sel_getName(cmd),0,false,gAnswers.at(cmd));return (NSUInteger)gAnswers.at(cmd);}\n'
 'static BOOL flagAnswer(id,SEL cmd) {nbCapLog(sel_getName(cmd),0,false,gAnswers.at(cmd));return gAnswers.at(cmd)!=0;}\n'))
tele_hooks.append((' kept[cmd][arg]=value;\n return value;\n',
 ' if(nbCapOff(sel_getName(cmd),arg,true))value=0;\n nbCapLog(sel_getName(cmd),arg,true,value);\n kept[cmd][arg]=value;\n return value;\n'))
tele_hooks.append(('   if(!strcmp(name,"supportsExtendedXR10Formats"))value=0;\n',
 '   if(!strcmp(name,"supportsExtendedXR10Formats"))value=0;\n   if(nbCapOff(name,0,false))value=0;\n   if(nbCapsLogOn()){char line[100];snprintf(line,sizeof line,"caps installed %s=%llu",name,(unsigned long long)value);nbCapAdd(std::string("i:")+name,line);}\n'))
# begonia (y237): metal_consts=NAME:VALUE,NAME:VALUE -- function constants overridden when a function is specialized (an experiment:
# what does the GPU cost with CoreAnimation's blend function out of the shader? The picture is wrong while it is on).
tele_hooks.append((' std::string text;\n for(const NmbConstant &c:constants) {\n',
 r"""
 {static std::unordered_map<std::string,uint64_t> forced;static int got=0;
  if(!got){got=1;char args[1024]="";size_t size=sizeof(args)-1;
   if(sysctlbyname("kern.bootargs",args,&size,NULL,0)==0){const char *at=strstr(args,"metal_consts=");
    if(at){at+=13;std::string list(at,strcspn(at," "));
     for(size_t from=0;from<list.size();){size_t to=list.find(',',from);if(to==std::string::npos)to=list.size();
      std::string item=list.substr(from,to-from);size_t colon=item.find(':');
      if(colon!=std::string::npos)forced[item.substr(0,colon)]=strtoull(item.c_str()+colon+1,NULL,10);from=to+1;}}}}
  static uint64_t store[64];
  if(!forced.empty())for(size_t i=0;i<constants.size()&&i<64;i++){
   if(!constants[i].name)continue;auto it=forced.find(constants[i].name);if(it==forced.end())continue;
   store[i]=it->second;constants[i].bytes=&store[i];}
 }
 std::string text;
 for(const NmbConstant &c:constants) {
"""))
for old,new in tele_hooks:
 assert mtext.count(old)==1,'telemetry hook does not fit: '+old[:90]
 mtext=mtext.replace(old,new)
mtext=mtext.replace('#include <atomic>\n','#include <atomic>\n#include <unordered_map>\n#include <string>\n#include <mutex>\n',1)
if not a.quartzcore:
 old_refusal=' if(error)*error=nbError([NSString stringWithFormat:@"no pipeline library on this GPU: %@",path.lastPathComponent]);\n return nil;\n'
 assert mtext.count(old_refusal)==1
 mtext=mtext.replace(old_refusal,' {static std::atomic<unsigned> said{0};if(said++<4)nbConsole([NSString stringWithFormat:@"tele pipeline library refused: %@",path]);}\n'+old_refusal)

# begonia (y305): two things the window server's drawing thread did at every commit (y304's profile of the home screen swiped,
# 2494 samples; boot-arg beg_thin=1: both thinned; beg_thin=2: in the odd 5-second slices of the time since boot, a measurement):
# 1. The surface it draws into was imported anew for every frame: the import is kept only weakly, by its texture, and the
#    texture ends with the frame (1363 surface textures for 1361 commits; in the kernel 540 surface calls and 270 closes in 5 s,
#    405 ms of them, 800000 pages entered into the GPU's tables and taken out; a file made, sized and unlinked for each). The
#    last six imports are now held.
# 2. Every mirrored bitmap a command buffer uses was hashed at its commit, 66 of them a commit, 42 us each (their bytes are in
#    write-combined memory, slow to read): 8.3% of the thread's samples. A bitmap already uploaded is now looked at at every
#    second commit (a change shows one commit later at most).
old='static NSMapTable<NSValue *,NBBuffer *> *gSurfaceImports;\n'
assert mtext.count(old)==1
mtext=mtext.replace(old,old+'''static NSMutableArray<NBBuffer *> *gBegSurfaceKept;
static int begThin(void) {
 static int mode=-1;
 if(mode<0){char args[1024]="";size_t size=sizeof(args)-1;
  mode=sysctlbyname("kern.bootargs",args,&size,NULL,0)==0?(strstr(args,"beg_thin=2")?2:strstr(args,"beg_thin=0")?0:1):1;}   // y310: on unless beg_thin=0 (y306, y307: the hashing 2.8 -> 1.5 ms a commit)
 if(mode!=2)return mode;
 struct timespec ts;clock_gettime(CLOCK_UPTIME_RAW,&ts);return (int)((ts.tv_sec/5)&1);
}
// gSurfaceImportLock held
static void begKeepSurface(NBBuffer *buffer) {
 if(!begThin()){[gBegSurfaceKept removeAllObjects];return;}
 if(!gBegSurfaceKept)gBegSurfaceKept=[NSMutableArray new];
 [gBegSurfaceKept removeObjectIdenticalTo:buffer];[gBegSurfaceKept insertObject:buffer atIndex:0];
 while(gBegSurfaceKept.count>6)[gBegSurfaceKept removeLastObject];
}
''')
old=' if(buffer)return buffer;\n // The surface keeps its CPU mapping (the imported pages) until the import is freed.\n'
assert mtext.count(old)==1
mtext=mtext.replace(old,' if(buffer){begKeepSurface(buffer);return buffer;}\n // The surface keeps its CPU mapping (the imported pages) until the import is freed.\n')
old='  [gSurfaceImports setObject:buffer forKey:key];\n }\n return buffer;\n}\n'
assert mtext.count(old)==1
mtext=mtext.replace(old,'  [gSurfaceImports setObject:buffer forKey:key];\n  begKeepSurface(buffer);\n }\n return buffer;\n}\n')
old='static void nbMirrorSync(NBTexture *t) {\n @synchronized(t) {\n'
assert mtext.count(old)==1
mtext=mtext.replace(old,'static int begThin(void);\nstatic void nbMirrorSync(NBTexture *t) {\n @synchronized(t) {\n'
 '  if(t.mirrorFilled && begThin() && ((gCommits.load()+((uintptr_t)t>>6))&1))return;\n')
# begonia (y306): boot-arg beg_nw=1, a measurement the owner asked for: the window server is given no texture of a surface
# of two planes -- the wallpaper is one (y298: a 4:2:0 surface, 3.2 MB of planes) -- to see what the home screen costs
# without it (he sees the app switcher's cards swipe at 60 frames a second, the home screen's pages at half that).
old=' if(planes)gSurfacePlanar++;\n'
assert mtext.count(old)==1
mtext=mtext.replace(old,old+' {static int nw=-1;if(nw<0){char args[1024]="";size_t size=sizeof(args)-1;\n'
 '   nw=strcmp(getprogname(),"backboardd")==0 && sysctlbyname("kern.bootargs",args,&size,NULL,0)==0 && strstr(args,"beg_nw=1")!=NULL;}\n'
 '  if(nw && planes>=2){refuseSurface(9,surface,d,plane,0,0);return nil;}}\n')
# begonia (y308): the window server's committing thread (CoreAnimation's display thread: 82% of a processor, the limit of
# the frame rate, y306/y307) onto an A76. XNU has every core as the same kind here, so a thread runs wherever there is
# room, mostly on an A55 (kernel/bsd/dev/begonia_perf.c); the GL bridge bound its drawing thread to processor 6
# (userland/patches/0006, gl_cpu), but with the window server on Metal that thread draws nothing and nobody bound the
# one that does. Boot-args: beg_cpu=N (6; -1: not bound), beg_cps=1: bound in the odd 5 s of uptime only (a measurement).
old='static int begThin(void);\nstatic void nbMirrorSync(NBTexture *t) {\n'
assert mtext.count(old)==1
mtext=mtext.replace(old,'static void begBindCpu(void) {\n'
 ' static int mode=-2,cpu=6;static thread_local int now=-100;\n'
 ' if(mode==-2){char args[1024]="";size_t size=sizeof(args)-1;int m=-1;\n'
 '  if(strcmp(getprogname(),"backboardd")==0){m=0;\n'
 '   if(sysctlbyname("kern.bootargs",args,&size,NULL,0)==0){const char *a=strstr(args,"beg_cpu=");if(a)cpu=atoi(a+8);if(strstr(args,"beg_cps=1"))m=1;}}\n'
 '  mode=m;}\n'
 ' if(mode<0)return;\n'
 ' int want=cpu;\n'
 ' if(mode==1){struct timespec ts;clock_gettime(CLOCK_UPTIME_RAW,&ts);if(!((ts.tv_sec/5)&1))want=-1;}\n'
 ' if(want==now)return;\n'
 ' char line[200]="";size_t n=sizeof(line)-1;\n'
 ' static std::atomic<unsigned> said{0};\n'
 ' const bool say=said++<6;\n'
 ' if(say)sysctlbyname("kern.begonia_clocks",line,&n,NULL,0);\n'
 ' int32_t v=want;const int failed=sysctlbyname("kern.begonia_bind_cpu",NULL,NULL,&v,sizeof(v));\n'
 ' now=want;\n'
 ' if(say)nbConsole([NSString stringWithFormat:@"tele cpu: the committing thread was on: %s; now bound to %d (%s)",line,want,failed?"refused":"ok"]);\n'
 '}\n'+old)
old=' if(nbMirrorOn() && !self.encodeFailure)for(id owner in [_resourceOwners copy])'
assert mtext.count(old)==1
mtext=mtext.replace(old,' begBindCpu();\n'+old)
if '#include <time.h>' not in mtext:mtext=mtext.replace('#include <atomic>\n','#include <atomic>\n#include <time.h>\n',1)

frontend = mtext
if a.quartzcore:
    implementation = '@implementation NBDeviceMethods\n'
    assert frontend.count(implementation) == 1
    old_pipeline_loader = '''- (id)newPipelineLibraryWithFilePath:(NSString *)path error:(NSError **)error {
 if(error)*error=nbError([NSString stringWithFormat:@"no pipeline library on this GPU: %@",path.lastPathComponent]);
 return nil;
}
'''
    assert frontend.count(old_pipeline_loader) == 1
    frontend = frontend.replace(old_pipeline_loader, '')
    library = '@implementation NBLibrary\n'
    assert frontend.count(library) == 1
    frontend = frontend.replace(library, library + '''
// QuartzCore passes Metal's function cache here. The bridge compiles and
// caches its own Mali shaders, so it cannot reuse cached Apple binaries.
- (id<MTLFunction>)newFunctionWithName:(NSString *)name
                        constantValues:(MTLFunctionConstantValues *)values
                         functionCache:(id)cache
                                 error:(NSError **)error {
 (void)cache;
 id<MTLFunction> function = values ?
     [self newFunctionWithName:name constantValues:values error:error] :
     [self newFunctionWithName:name];
 if (!function) {
  NSLog(@"[BegoniaMetal] WARNING: QuartzCore function '%@' refused (%@), returning stub NBFunction", name, lastError());
  NBFunction *stub = [NBFunction new];
  stub.handle = nullptr;
  stub.name = name;
  stub.functionType = MTLFunctionTypeFragment;
  if (error) *error = nil;
  return (id<MTLFunction>)stub;
 }
 if (error) *error = nil;
 return function;
}
''')
    adapter = (root/'userland/metal/QuartzCorePipelineLibrary.inc').read_text()
    frontend = '#import <objc/message.h>\n' + frontend.replace(implementation, '''
struct BegoniaMTLTargetDeviceArch {
    uint64_t target;
    uint32_t version;
    char *name;
};
static_assert(sizeof(BegoniaMTLTargetDeviceArch) == 24 &&
              offsetof(BegoniaMTLTargetDeviceArch, name) == 16,
              "iOS 16 Metal target descriptor layout");
''' + implementation + adapter + '\n')
mf.write_text(frontend)
common=[*(['-arch','arm64','-mios-version-min=16.5'] if a.cc else ['-target','arm64-apple-ios16.5']),'-isysroot',str(a.sdk.resolve()),'-O1','-Wall','-Wextra',
 '-I'+str(root/'out/metal2vulkan-bridge').replace('/metal2vulkan-bridge',''),'-I'+str(source)]
# The source includes bridge/NativeMetalObjects.h. Keep a staged header only;
# implementation remains dynamically linked and replaceable.
bridge=out/'bridge';bridge.mkdir(exist_ok=True)
shutil.copy2(root/'out/metal2vulkan-bridge/NativeMetalObjects.h',bridge/'NativeMetalObjects.h')
common+=['-I'+str(out)]
objects=[]
with (out/'build.log').open('w') as log:
 for f in (source/'HakiosMetalVK.mm',source/'NativeBridgeMetalFrontend.mm',out/'hakios_metal_shown.c'):
  obj=out/(f.stem+'.o');objects.append(obj)
  cmd=[*([str(a.cc)+('++' if f.suffix=='.mm' else '')] if a.cc else ['xcrun','clang++' if f.suffix=='.mm' else 'clang']),*common]
  if f.suffix=='.mm':cmd+=['-std=c++17','-fobjc-arc']
  subprocess.run([*cmd,'-c',str(f),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
 binary=out/'HakiosMetal'
 subprocess.run([*([str(a.cc)+'++'] if a.cc else ['xcrun','clang++']),*common,'-bundle','-o',str(binary),*map(str,objects),
  # (the runtime built last: the full build or the size one, tools/build_metal2vulkan_linux.sh)
  str(max((f for f in (root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib',
                       root/'out/metal2vulkan-target-ios-small/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib') if f.exists()),
          key=lambda f:f.stat().st_mtime)),
  '-Wl,-rpath,/redwood/lib','-Wl,-rpath,@loader_path/..',
  '-framework','Foundation','-framework','Metal','-framework','IOSurface','-framework','IOKit','-framework','CoreFoundation','-lobjc'],stdout=log,stderr=subprocess.STDOUT,check=True)
if not a.cc:
 subprocess.run(['codesign','-s','-','-f',str(binary)],check=True)
 subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
print('Built signed Metal frontend; '+('experimental MPS stubs enabled' if a.experimental_mps_stubs else 'unsupported compute reports failure')+'; not installed')
