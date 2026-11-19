#include <objc/message.h>
// begonia: a pipeline's reflection, as Metal's objects (MTLArgument, MTLBinding, MTLRenderPipelineReflection,
// MTLComputePipelineReflection). Put into NativeBridgeMetalFrontend.mm by tools/build_metal_frontend_ios.py.
//
// The front end answered every reflection: out-parameter with nil. Freeform asks with MTLPipelineOptionArgumentInfo |
// BufferTypeInfo (3), then looks in vertexArguments and fragmentArguments for a buffer (type 0) of a struct (data type
// 1) by its name -- its uniforms: their index and size -- and for a sampler (type 3) by its name; given nil it aborts
// at launch ("Failed to create pipeline reflection object", y24 to y26). The bindings come from the runtime
// (nmb_render_pipeline_reflection, userland/metal/bridge/begonia_reflection.rs), read off the functions' AIR.
// Boot-arg metal_reflection=0: nil again, as before.

@interface NBArgument : MTLArgument <MTLBinding> {
 @public
 NSString *_bgName;
 NSUInteger _bgType, _bgAccess, _bgIndex, _bgFlags, _bgBufferAlignment, _bgBufferSize, _bgBufferDataType;
 NSUInteger _bgTextureType, _bgTextureDataType, _bgArrayLength, _bgThreadgroupAlignment, _bgThreadgroupSize;
}
@end
@implementation NBArgument
- (NSString *)name {return _bgName;}
- (MTLArgumentType)type {return (MTLArgumentType)_bgType;}
- (MTLArgumentAccess)access {return (MTLArgumentAccess)_bgAccess;}
- (NSUInteger)index {return _bgIndex;}
- (BOOL)isActive {return (_bgFlags & 1) != 0;}
- (BOOL)active {return (_bgFlags & 1) != 0;}
- (BOOL)isUsed {return (_bgFlags & 1) != 0;}
- (BOOL)used {return (_bgFlags & 1) != 0;}
- (BOOL)isArgument {return (_bgFlags & 2) != 0;}
- (BOOL)argument {return (_bgFlags & 2) != 0;}
- (NSUInteger)bufferAlignment {return _bgBufferAlignment;}
- (NSUInteger)bufferDataSize {return _bgBufferSize;}
- (MTLDataType)bufferDataType {return (MTLDataType)_bgBufferDataType;}
- (MTLStructType *)bufferStructType {return nil;}
- (MTLPointerType *)bufferPointerType {return nil;}
- (NSUInteger)threadgroupMemoryAlignment {return _bgThreadgroupAlignment;}
- (NSUInteger)threadgroupMemoryDataSize {return _bgThreadgroupSize;}
- (MTLTextureType)textureType {return (MTLTextureType)_bgTextureType;}
- (MTLDataType)textureDataType {return (MTLDataType)_bgTextureDataType;}
- (BOOL)isDepthTexture {return (_bgFlags & 32) != 0;}
- (BOOL)depthTexture {return (_bgFlags & 32) != 0;}
- (NSUInteger)arrayLength {return _bgArrayLength;}
- (NSString *)description {
 return [NSString stringWithFormat:@"%@ type %lu index %lu%s size %lu data type %lu", _bgName, (unsigned long)_bgType,
         (unsigned long)_bgIndex, (_bgFlags & 1) ? "" : " (unused)", (unsigned long)_bgBufferSize, (unsigned long)_bgBufferDataType];
}
@end
@interface NBBufferArgument : NBArgument <MTLBufferBinding>
@end
@implementation NBBufferArgument
@end
@interface NBTextureArgument : NBArgument <MTLTextureBinding>
@end
@implementation NBTextureArgument
@end
@interface NBThreadgroupArgument : NBArgument <MTLThreadgroupBinding>
@end
@implementation NBThreadgroupArgument
@end

@interface NBRenderReflection : MTLRenderPipelineReflection {
 @public
 NSArray *_bgVertex, *_bgFragment, *_bgTile, *_bgObject, *_bgMesh;
}
@end
@implementation NBRenderReflection
- (NSArray *)vertexBindings {return _bgVertex ?: @[];}
- (NSArray *)fragmentBindings {return _bgFragment ?: @[];}
- (NSArray *)tileBindings {return _bgTile ?: @[];}
- (NSArray *)objectBindings {return _bgObject ?: @[];}
- (NSArray *)meshBindings {return _bgMesh ?: @[];}
- (NSArray *)vertexArguments {return _bgVertex ?: @[];}
- (NSArray *)fragmentArguments {return _bgFragment ?: @[];}
- (NSArray *)tileArguments {return _bgTile ?: @[];}
@end
@interface NBComputeReflection : MTLComputePipelineReflection {
 @public
 NSArray *_bgBindings;
}
@end
@implementation NBComputeReflection
- (NSArray *)bindings {return _bgBindings ?: @[];}
- (NSArray *)arguments {return _bgBindings ?: @[];}
@end

static void nbConsole(NSString *what) {
 static int fd = -2;
 if (fd == -2) fd = isatty(2) ? 2 : open("/dev/console", O_WRONLY | O_NOCTTY);
 if (fd < 0) return;
 char line[700];
 int n = snprintf(line, sizeof line, "metal: %s[%d]: %s\n", getprogname(), getpid(), what.UTF8String ?: "?");
 if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
 if (n > 0) (void)write(fd, line, (size_t)n);
}

static bool nbReflectionWanted() {
 static int wanted = -1;
 if (wanted < 0) {
  char args[1024] = ""; size_t size = sizeof(args) - 1;
  wanted = !(sysctlbyname("kern.bootargs", args, &size, NULL, 0) == 0 && strstr(args, "metal_reflection=0") != NULL);
 }
 return wanted != 0;
}

static NSArray *nbArguments(const NmbBinding *b, uint64_t n) {
 NSMutableArray *out = [NSMutableArray arrayWithCapacity:(NSUInteger)n];
 for (uint64_t i = 0; i < n; i++) {
  Class cls = (b[i].flags & 4) ? [NBBufferArgument class] : (b[i].flags & 8) ? [NBTextureArgument class] :
              (b[i].flags & 16) ? [NBThreadgroupArgument class] : [NBArgument class];
  NBArgument *a = [[cls alloc] init];
  char name[97]; memcpy(name, b[i].name, 96); name[96] = 0;
  a->_bgName = [NSString stringWithUTF8String:name] ?: @"";
  a->_bgType = b[i].binding_type; a->_bgAccess = b[i].access; a->_bgIndex = b[i].index; a->_bgFlags = b[i].flags;
  a->_bgBufferAlignment = (NSUInteger)b[i].buffer_alignment; a->_bgBufferSize = (NSUInteger)b[i].buffer_data_size;
  a->_bgBufferDataType = b[i].buffer_data_type; a->_bgTextureType = b[i].texture_type;
  a->_bgTextureDataType = b[i].texture_data_type; a->_bgArrayLength = b[i].array_length;
  a->_bgThreadgroupAlignment = (NSUInteger)b[i].threadgroup_alignment; a->_bgThreadgroupSize = (NSUInteger)b[i].threadgroup_size;
  [out addObject:a];
 }
 return out;
}

// The bindings of every stage, one after the other, and how many each stage has; false: the runtime gave none.
static bool nbReflect(bool render, const void *descriptor, uint64_t *counts, std::vector<NmbBinding> &all) {
 all.resize(64);
 for (int attempt = 0; attempt < 3; attempt++) {
  int64_t n = render ? nmb_render_pipeline_reflection((const NmbRenderPipelineDescriptor *)descriptor, all.data(), all.size(), counts)
                     : nmb_compute_pipeline_reflection((const NmbComputePipelineDescriptor *)descriptor, all.data(), all.size(), counts);
  if (n < 0) return false;
  bool fits = (uint64_t)n <= all.size();
  all.resize((size_t)n);
  if (fits) return true;
 }
 return false;
}

static void nbSayReflection(NSString *label, const char *stage, NSArray *arguments) {
 static std::atomic<unsigned> said{0};
 if (said.fetch_add(1) >= 40) return;
 nbConsole([NSString stringWithFormat:@"reflection of '%@', %s: %@", label ?: @"-", stage,
            [[arguments valueForKey:@"description"] componentsJoinedByString:@"; "]]);
}

// Never nil when asked for: a caller that asks gets an object, empty when the runtime could not say (Freeform aborts
// on nil, and an empty list only makes it fall back to its defaults).
static MTLRenderPipelineReflection *renderReflection(const NmbRenderPipelineDescriptor *p, NSString *label) {
 if (!nbReflectionWanted()) return nil;
 NBRenderReflection *r = [[NBRenderReflection alloc] init];
 std::vector<NmbBinding> all; uint64_t counts[5] = {0, 0, 0, 0, 0};
 if (!nbReflect(true, p, counts, all)) {
  nbConsole([NSString stringWithFormat:@"reflection of '%@' not known: %s; an empty one given", label ?: @"-", nmb_last_error()]);
  return r;
 }
 uint64_t at = 0; NSArray *stages[5];
 for (int s = 0; s < 5; s++) {stages[s] = nbArguments(all.data() + at, counts[s]); at += counts[s];}
 r->_bgVertex = stages[0]; r->_bgFragment = stages[1]; r->_bgTile = stages[2]; r->_bgObject = stages[3]; r->_bgMesh = stages[4];
 nbSayReflection(label, "vertex", stages[0]);
 nbSayReflection(label, "fragment", stages[1]);
 return r;
}

// begonia: the shader libraries of applications. The system image was made without any .metallib (the port had no
// Metal; select-system.py SKIP_EXT), so Freeform's bundle has no default.metallib (31 KB in the firmware) and every
// function it asks for was a stub without code -- no pipeline, no reflection, an abort at launch. The libraries of the
// programs shown the device are in the support image instead, /redwood/ext/metallib/<bundle identifier>/<name>.metallib
// (tools/make_gles_support.py --metal-client); a lookup of a .metallib that the bundle has not got answers with that.
static IMP nbOriginalURL, nbOriginalURLIn, nbOriginalPath;

static NSString *nbKeptMetallib(NSBundle *bundle, NSString *name, NSString *extension) {
 if (![extension hasSuffix:@"metallib"] || !name) return nil;
 // an application's by its identifier (where it is installed differs from phone to phone); a framework's by its path
 NSString *path = bundle.bundleIdentifier ?
     [NSString stringWithFormat:@"/redwood/ext/metallib/%@/%@.metallib", bundle.bundleIdentifier, name] : nil;
 if (!path || ![[NSFileManager defaultManager] isReadableFileAtPath:path]) {
  path = bundle.bundlePath ? [NSString stringWithFormat:@"/redwood/ext/metallib%@/%@.metallib", bundle.bundlePath, name] : nil;
  if (!path || ![[NSFileManager defaultManager] isReadableFileAtPath:path]) return nil;
 }
 static std::atomic<unsigned> said{0};
 if (said.fetch_add(1) < 8) nbConsole([NSString stringWithFormat:@"%@.metallib of %@ from the support image", name, bundle.bundleIdentifier]);
 return path;
}
static NSURL *nbURLForResource(NSBundle *bundle, SEL selector, NSString *name, NSString *extension) {
 NSURL *url = ((NSURL *(*)(id, SEL, NSString *, NSString *))nbOriginalURL)(bundle, selector, name, extension);
 NSString *kept = url ? nil : nbKeptMetallib(bundle, name, extension);
 return kept ? [NSURL fileURLWithPath:kept] : url;
}
static NSURL *nbURLForResourceIn(NSBundle *bundle, SEL selector, NSString *name, NSString *extension, NSString *directory) {
 NSURL *url = ((NSURL *(*)(id, SEL, NSString *, NSString *, NSString *))nbOriginalURLIn)(bundle, selector, name, extension, directory);
 NSString *kept = url ? nil : nbKeptMetallib(bundle, name, extension);
 return kept ? [NSURL fileURLWithPath:kept] : url;
}
static NSString *nbPathForResource(NSBundle *bundle, SEL selector, NSString *name, NSString *type) {
 NSString *path = ((NSString *(*)(id, SEL, NSString *, NSString *))nbOriginalPath)(bundle, selector, name, type);
 return path ?: nbKeptMetallib(bundle, name, type);
}
__attribute__((constructor)) static void nbMetallibsFromTheSupportImage() {
 Class bundle = [NSBundle class];
 Method m;
 if ((m = class_getInstanceMethod(bundle, @selector(URLForResource:withExtension:))))
  nbOriginalURL = method_setImplementation(m, (IMP)nbURLForResource);
 if ((m = class_getInstanceMethod(bundle, @selector(URLForResource:withExtension:subdirectory:))))
  nbOriginalURLIn = method_setImplementation(m, (IMP)nbURLForResourceIn);
 if ((m = class_getInstanceMethod(bundle, @selector(pathForResource:ofType:))))
  nbOriginalPath = method_setImplementation(m, (IMP)nbPathForResource);
}

// begonia: where the Metal stack's libraries are in this process, said as each one is loaded, so that a crash's pc
// (kernel patch 0016's usertrap note) can be placed in them: offset = pc - the address said, then llvm-symbolizer on the
// unstripped library (out/...).
static void nbImageAdded(const struct mach_header *header, intptr_t slide) {
 Dl_info info;
 (void)slide;
 if (dladdr(header, &info) == 0 || info.dli_fname == NULL) return;
 if (strstr(info.dli_fname, "HakiosMetal") || strstr(info.dli_fname, "libmetal2vulkan") || strstr(info.dli_fname, "libvulkan_panfrost"))
  nbConsole([NSString stringWithFormat:@"image %s at %#lx", info.dli_fname, (unsigned long)header]);
}
__attribute__((constructor)) static void nbSayImages() {
 _dyld_register_func_for_add_image(nbImageAdded);
}

// begonia: boot-arg metal_calls=1 -- what a program encodes, call by call, on the console: every method of the render
// and blit encoders and of the command buffer goes through forwardInvocation:, which says its selector and arguments
// (the first 300 calls of a process) and then runs the method itself. For finding why a frame comes out empty (SceneKit,
// y38: pipelines made, buffers completed, not one pixel of the box).
static std::atomic<unsigned> nbCallsSaid{0};
static NSString *nbArgument(NSInvocation *invocation, NSUInteger i) {
 const char *type = [invocation.methodSignature getArgumentTypeAtIndex:i];
 while (*type == 'r' || *type == 'n' || *type == 'N' || *type == 'o' || *type == 'O' || *type == 'R' || *type == 'V') type++;
 switch (*type) {
 case 'c': case 'C': case 'B': {unsigned char v = 0; [invocation getArgument:&v atIndex:i]; return [NSString stringWithFormat:@"%u", v];}
 case 'i': case 'I': {unsigned v = 0; [invocation getArgument:&v atIndex:i]; return [NSString stringWithFormat:@"%u", v];}
 case 'q': case 'Q': case 'l': case 'L': {unsigned long long v = 0; [invocation getArgument:&v atIndex:i]; return [NSString stringWithFormat:@"%llu", v];}
 case 'f': {float v = 0; [invocation getArgument:&v atIndex:i]; return [NSString stringWithFormat:@"%g", v];}
 case 'd': {double v = 0; [invocation getArgument:&v atIndex:i]; return [NSString stringWithFormat:@"%g", v];}
 case '@': {
  __unsafe_unretained id v = nil; [invocation getArgument:&v atIndex:i];
  if (!v) return @"nil";
  NSString *label = [v respondsToSelector:@selector(label)] ? [v label] : nil;
  return label.length ? [NSString stringWithFormat:@"%s'%@'", object_getClassName(v), label] : [NSString stringWithUTF8String:object_getClassName(v)];
 }
 case '^': {void *v = NULL; [invocation getArgument:&v atIndex:i]; return [NSString stringWithFormat:@"%p", v];}
 case '{': {
  NSUInteger size = 0; NSGetSizeAndAlignment(type, &size, NULL);
  if (strncmp(type, "{?=dddddd}", 10) == 0 || strncmp(type, "{MTLViewport", 12) == 0 || (size == 48 && strstr(type, "dddddd"))) {
   double v[6]; [invocation getArgument:v atIndex:i];
   return [NSString stringWithFormat:@"{%g %g %g %g %g %g}", v[0], v[1], v[2], v[3], v[4], v[5]];
  }
  if (size <= 64) {
   uint64_t v[8] = {0}; [invocation getArgument:v atIndex:i];
   NSMutableString *s = [NSMutableString stringWithString:@"{"];
   for (NSUInteger k = 0; k < (size + 7) / 8; k++) [s appendFormat:@"%s%llu", k ? " " : "", v[k]];
   [s appendString:@"}"]; return s;
  }
  return [NSString stringWithFormat:@"{%lu bytes}", (unsigned long)size];
 }
 default: return [NSString stringWithFormat:@"(%s)", type];
 }
}
static void nbForwardInvocation(id self, SEL _cmd, NSInvocation *invocation) {
 (void)_cmd;
 SEL original = NSSelectorFromString([@"nbOriginal_" stringByAppendingString:NSStringFromSelector(invocation.selector)]);
 if (nbCallsSaid.fetch_add(1) < 300) {
  NSMutableString *line = [NSMutableString stringWithFormat:@"call %s %@", object_getClassName(self), NSStringFromSelector(invocation.selector)];
  for (NSUInteger i = 2; i < invocation.methodSignature.numberOfArguments; i++) [line appendFormat:@" %@", nbArgument(invocation, i)];
  nbConsole(line.length > 600 ? [line substringToIndex:600] : line);
 }
 invocation.selector = original;
 [invocation invoke];
}
static void nbTraceClass(const char *name) {
 Class cls = objc_getClass(name);
 if (!cls) return;
 unsigned count = 0; Method *methods = class_copyMethodList(cls, &count);
 for (unsigned i = 0; i < count; i++) {
  SEL sel = method_getName(methods[i]);
  const char *s = sel_getName(sel);
  if (strncmp(s, "nbOriginal_", 11) == 0 || strcmp(s, "dealloc") == 0 || strcmp(s, ".cxx_destruct") == 0 || strcmp(s, "handle") == 0 ||
      strncmp(s, "set", 3) != 0 && strncmp(s, "draw", 4) != 0 && strncmp(s, "end", 3) != 0 && strncmp(s, "commit", 6) != 0 &&
      strncmp(s, "render", 6) != 0 && strncmp(s, "blit", 4) != 0 && strncmp(s, "copy", 4) != 0 && strncmp(s, "present", 7) != 0 &&
      strncmp(s, "use", 3) != 0 && strncmp(s, "wait", 4) != 0)
   continue;
  SEL renamed = NSSelectorFromString([@"nbOriginal_" stringByAppendingString:NSStringFromSelector(sel)]);
  if (class_addMethod(cls, renamed, method_getImplementation(methods[i]), method_getTypeEncoding(methods[i])))
   method_setImplementation(methods[i], _objc_msgForward);
 }
 free(methods);
 class_addMethod(cls, @selector(forwardInvocation:), (IMP)nbForwardInvocation, "v@:@");
}
__attribute__((constructor)) static void nbTraceCalls() {
 char args[1024] = ""; size_t size = sizeof(args) - 1;
 if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) != 0 || strstr(args, "metal_calls=1") == NULL) return;
 nbTraceClass("NBRenderEncoder");
 nbTraceClass("NBBlitEncoder");
 nbTraceClass("NBCommandBuffer");
}

static MTLComputePipelineReflection *computeReflection(const NmbComputePipelineDescriptor *p, NSString *label) {
 if (!nbReflectionWanted()) return nil;
 NBComputeReflection *r = [[NBComputeReflection alloc] init];
 std::vector<NmbBinding> all; uint64_t counts[1] = {0};
 if (!nbReflect(false, p, counts, all)) {
  nbConsole([NSString stringWithFormat:@"reflection of kernel '%@' not known: %s; an empty one given", label ?: @"-", nmb_last_error()]);
  return r;
 }
 r->_bgBindings = nbArguments(all.data(), counts[0]);
 nbSayReflection(label, "kernel", r->_bgBindings);
 return r;
}
