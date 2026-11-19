// begonia: Metal shader source (newLibraryWithSource:). SceneKit writes its shaders as source at run time and has them
// compiled on the device, and so do many games' engines; the front end had no such method, so Metal's own _MTLDevice
// answered with its own objects, which the pipelines here cannot use ("foreign function": SceneKit's box not drawn, y34).
//
// The system's compiler works here (y35: SceneKit's 131 KB of source, no error, an _MTLLibrary of commonprofile_vert and
// commonprofile_frag): its front half, source to AIR, needs no GPU of Apple's. So the source goes to it, and the library
// it makes -- a metallib of AIR, libraryDataContents or serializeToURL:error: -- is opened by the runtime as any other
// (newLibraryWithData:). Put after the device's class is made, by tools/build_metal_frontend_ios.py.
static NSData *nbLibraryBytes(id library) {
 if ([library respondsToSelector:@selector(libraryDataContents)]) {
  id contents = ((id (*)(id, SEL))objc_msgSend)(library, @selector(libraryDataContents));
  if ([contents isKindOfClass:[NSData class]] && [(NSData *)contents length] > 0) return contents;
 }
 if ([library respondsToSelector:@selector(serializeToURL:error:)]) {
  NSURL *url = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
                                       [NSString stringWithFormat:@"begonia-source-%d-%u.metallib", getpid(), arc4random()]]];
  NSError *error = nil;
  if (((BOOL (*)(id, SEL, NSURL *, NSError **))objc_msgSend)(library, @selector(serializeToURL:error:), url, &error)) {
   NSData *bytes = [NSData dataWithContentsOfURL:url];
   [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
   return bytes;
  }
 }
 return nil;
}

@implementation NBDeviceMethods (BegoniaSource)
- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
 static std::atomic<unsigned> said{0};
 Class base = objc_getClass("_MTLDevice");
 IMP apple = base ? class_getMethodImplementation(base, _cmd) : NULL;
 NSError *problem = nil;
 const uint64_t began = nowUs();
 id compiled = apple ? ((id (*)(id, SEL, NSString *, MTLCompileOptions *, NSError **))apple)(self, _cmd, source, options, &problem) : nil;
 if (!compiled) {
  nbConsole([NSString stringWithFormat:@"source: %lu characters not compiled: %@", (unsigned long)source.length,
             problem ? problem.localizedDescription : @"no answer"]);
  if (error) *error = problem;
  return nil;
 }
 NSData *bytes = nbLibraryBytes(compiled);
 if (!bytes) {
  nbConsole(@"source: the system's library gives no metallib to open");
  if (error) *error = nbError(@"the compiled source has no metallib");
  return nil;
 }
 dispatch_data_t data = dispatch_data_create(bytes.bytes, bytes.length, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
 id<MTLLibrary> library = [(id)self newLibraryWithData:data error:error];
 if (said.fetch_add(1) < 8)
  nbConsole([NSString stringWithFormat:@"source: %lu characters compiled by the system in %llu ms, %lu bytes of AIR, opened: %@",
             (unsigned long)source.length, (unsigned long long)((nowUs() - began) / 1000), (unsigned long)bytes.length,
             library ? [[library functionNames] componentsJoinedByString:@", "] : (error && *error ? (*error).localizedDescription : @"no")]);
 return library;
}
- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options completionHandler:(MTLNewLibraryCompletionHandler)handler {
 compileAsync(^{
  NSError *problem = nil;
  id<MTLLibrary> library = [(id)self newLibraryWithSource:source options:options error:&problem];
  if (handler) dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{handler(library, problem);});
 });
}
@end
