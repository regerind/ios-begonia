// begonia: Metal shader source (newLibraryWithSource:). SceneKit writes its shaders as source at run time and has them
// compiled on the device, and so do many games' engines; the front end had no such method, so Metal's own _MTLDevice
// answered, with objects the pipelines here cannot use ("foreign function", SceneKit's box not drawn, y34).
// This first step only finds out what the system's compiler gives on this phone: Apple's answer, said on the console --
// its class, the error, and what in it may hold the AIR the runtime takes. Put after the device's class is made, by
// tools/build_metal_frontend_ios.py.
static void nbSayObject(NSString *what, id object) {
 if (!object) {nbConsole([NSString stringWithFormat:@"source: %@: nil", what]); return;}
 NSMutableString *line = [NSMutableString stringWithFormat:@"source: %@: %s;", what, object_getClassName(object)];
 for (Class c = object_getClass(object); c && c != [NSObject class]; c = class_getSuperclass(c)) {
  unsigned count = 0; Method *methods = class_copyMethodList(c, &count);
  for (unsigned i = 0; i < count; i++) {
   const char *name = sel_getName(method_getName(methods[i]));
   if (strcasestr(name, "data") || strcasestr(name, "bitcode") || strcasestr(name, "air") || strcasestr(name, "serial") ||
       strcasestr(name, "binary") || strcasestr(name, "contents"))
    [line appendFormat:@" %s.%s", class_getName(c), name];
  }
  free(methods);
 }
 nbConsole(line.length > 650 ? [line substringToIndex:650] : line);
}

@implementation NBDeviceMethods (BegoniaSource)
- (id<MTLLibrary>)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options error:(NSError **)error {
 static std::atomic<unsigned> said{0};
 Class base = objc_getClass("_MTLDevice");
 IMP apple = base ? class_getMethodImplementation(base, _cmd) : NULL;
 NSError *problem = nil;
 id library = apple ? ((id (*)(id, SEL, NSString *, MTLCompileOptions *, NSError **))apple)(self, _cmd, source, options, &problem) : nil;
 if (said.fetch_add(1) < 6) {
  nbConsole([NSString stringWithFormat:@"source: %lu characters, language version %#lx, fast math %d: %@", (unsigned long)source.length,
             (unsigned long)options.languageVersion, options.fastMathEnabled, problem ? problem.localizedDescription : @"no error"]);
  nbSayObject(@"the system's library", library);
  if (library) {
   NSArray *names = [library functionNames];
   nbConsole([NSString stringWithFormat:@"source: functions %@", [names componentsJoinedByString:@", "]]);
   if (names.count) nbSayObject(@"its first function", [library newFunctionWithName:names[0]]);
  }
 }
 if (error) *error = problem;
 return library;
}
- (void)newLibraryWithSource:(NSString *)source options:(MTLCompileOptions *)options completionHandler:(MTLNewLibraryCompletionHandler)handler {
 NSError *problem = nil;
 id<MTLLibrary> library = [(id)self newLibraryWithSource:source options:options error:&problem];
 if (handler) dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{handler(library, problem);});
}
@end
