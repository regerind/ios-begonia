// begonia: Metal shader source (newLibraryWithSource:). SceneKit writes its shaders as source at run time and has them
// compiled on the device, and so do many games' engines; the front end had no such method, so Metal's own _MTLDevice
// answered with its own objects, which the pipelines here cannot use ("foreign function": SceneKit's box not drawn, y34).
//
// The system's compiler works here (y35: SceneKit's 131 KB of source, no error, an _MTLLibrary of commonprofile_vert and
// commonprofile_frag): its front half, source to AIR, needs no GPU of Apple's. So the source goes to it, and the library
// it makes -- a metallib of AIR, libraryDataContents or serializeToURL:error: -- is opened by the runtime as any other
// (newLibraryWithData:). Put after the device's class is made, by tools/build_metal_frontend_ios.py.
#include <objc/message.h>

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

// begonia: draws and dispatches whose counts come from a buffer (games draw so: GPU culling, particles). The runtime has
// them (userland/metal/bridge/begonia_indirect.rs); the front end had not, and a program died on the unknown selector (y48).
@implementation NBRenderEncoder (BegoniaIndirect)
- (void)drawPrimitives:(MTLPrimitiveType)t indirectBuffer:(id<MTLBuffer>)b indirectBufferOffset:(NSUInteger)o {
 if(self.stub)return;
 if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign indirect buffer"];return;}
 [self.commandBuffer.resourceOwners addObject:b];
 gDraws++;self.commandBuffer->encodedWork.draws++;
 [self check:nmb_render_draw_indirect(self.handle,t,[(NBBuffer *)b handle],o) what:"drawPrimitives:indirectBuffer:"];
}
- (void)drawIndexedPrimitives:(MTLPrimitiveType)t indexType:(MTLIndexType)k indexBuffer:(id<MTLBuffer>)ib indexBufferOffset:(NSUInteger)io
               indirectBuffer:(id<MTLBuffer>)b indirectBufferOffset:(NSUInteger)o {
 if(self.stub)return;
 if(![(id)b isKindOfClass:[NBBuffer class]] || ![(id)ib isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign indirect or index buffer"];return;}
 [self.commandBuffer.resourceOwners addObject:b];[self.commandBuffer.resourceOwners addObject:ib];
 gDraws++;self.commandBuffer->encodedWork.draws++;
 [self check:nmb_render_draw_indexed_indirect(self.handle,t,k,[(NBBuffer *)ib handle],io,[(NBBuffer *)b handle],o) what:"drawIndexedPrimitives:indirectBuffer:"];
}
@end
@implementation NBComputeEncoder (BegoniaIndirect)
- (void)dispatchThreadgroupsWithIndirectBuffer:(id<MTLBuffer>)b indirectBufferOffset:(NSUInteger)o threadsPerThreadgroup:(MTLSize)g {
 if(self.stub)return;
 if(![(id)b isKindOfClass:[NBBuffer class]]){[self.commandBuffer refuse:@"foreign indirect buffer"];return;}
 [self.commandBuffer.resourceOwners addObject:b];
 gDispatches++;self.commandBuffer->encodedWork.dispatches++;
 const uint64_t group[3]={g.width,g.height,g.depth};
 [self check:nmb_compute_dispatch_indirect(self.handle,[(NBBuffer *)b handle],o,group) what:"dispatchThreadgroupsWithIndirectBuffer"];
}
@end

// begonia: argument buffers (MTLArgumentEncoder), over the runtime's (userland/metal/bridge/begonia_arguments.rs).
@interface NBArgumentEncoder : NBObject <MTLArgumentEncoder>
@property(nonatomic) NmbArgumentEncoder *handle;
@end
@implementation NBArgumentEncoder
- (void)dealloc {if(_handle)nmb_argument_encoder_release(_handle);}
- (NSUInteger)encodedLength {return (NSUInteger)nmb_argument_encoder_encoded_length(_handle);}
- (NSUInteger)alignment {return (NSUInteger)nmb_argument_encoder_alignment(_handle);}
- (void)report:(bool)ok what:(const char *)what {if(!ok)NBLOG(@"native-metal: argument encoder %s refused: %@",what,lastError());}
- (void)setArgumentBuffer:(id<MTLBuffer>)b offset:(NSUInteger)o {
 [self report:nmb_argument_encoder_set_target(_handle,[(id)b isKindOfClass:[NBBuffer class]]?[(NBBuffer *)b handle]:nullptr,o,UINT64_MAX) what:"setArgumentBuffer"];
}
- (void)setArgumentBuffer:(id<MTLBuffer>)b startOffset:(NSUInteger)o arrayElement:(NSUInteger)element {
 [self report:nmb_argument_encoder_set_target(_handle,[(id)b isKindOfClass:[NBBuffer class]]?[(NBBuffer *)b handle]:nullptr,o,element) what:"setArgumentBuffer:startOffset:arrayElement:"];
}
- (void)setBuffer:(id<MTLBuffer>)b offset:(NSUInteger)o atIndex:(NSUInteger)i {
 [self report:nmb_argument_encoder_set_buffer(_handle,[(id)b isKindOfClass:[NBBuffer class]]?[(NBBuffer *)b handle]:nullptr,o,(uint32_t)i) what:"setBuffer"];
}
- (void)setBuffers:(const id<MTLBuffer> __unsafe_unretained [])b offsets:(const NSUInteger [])o withRange:(NSRange)r {
 for(NSUInteger k=0;k<r.length;k++)[self setBuffer:b[k] offset:o[k] atIndex:r.location+k];
}
- (void)setTexture:(id<MTLTexture>)t atIndex:(NSUInteger)i {
 [self report:nmb_argument_encoder_set_texture(_handle,[(id)t isKindOfClass:[NBTexture class]]?[(NBTexture *)t handle]:nullptr,(uint32_t)i) what:"setTexture"];
}
- (void)setTextures:(const id<MTLTexture> __unsafe_unretained [])t withRange:(NSRange)r {
 for(NSUInteger k=0;k<r.length;k++)[self setTexture:t[k] atIndex:r.location+k];
}
- (void)setSamplerState:(id<MTLSamplerState>)s atIndex:(NSUInteger)i {
 [self report:nmb_argument_encoder_set_sampler(_handle,[(id)s isKindOfClass:[NBSampler class]]?[(NBSampler *)s handle]:nullptr,(uint32_t)i) what:"setSamplerState"];
}
- (void)setSamplerStates:(const id<MTLSamplerState> __unsafe_unretained [])s withRange:(NSRange)r {
 for(NSUInteger k=0;k<r.length;k++)[self setSamplerState:s[k] atIndex:r.location+k];
}
- (void *)constantDataAtIndex:(NSUInteger)i {return nmb_argument_encoder_constant_data(_handle,(uint32_t)i);}
- (id<MTLArgumentEncoder>)newArgumentEncoderForBufferAtIndex:(NSUInteger)i {
 NmbArgumentEncoder *nested=nmb_argument_encoder_nested(_handle,(uint32_t)i);
 if(!nested){NBLOG(@"native-metal: nested argument encoder refused: %@",lastError());return nil;}
 NBArgumentEncoder *encoder=[NBArgumentEncoder new];encoder.handle=nested;return encoder;
}
@end
@implementation NBFunction (BegoniaArguments)
- (id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)i {
 NmbArgumentEncoder *handle=self.handle?nmb_argument_encoder_new(self.handle,(uint32_t)i):nullptr;
 if(!handle){NBLOG(@"native-metal: argument encoder refused: %@",lastError());return nil;}
 NBArgumentEncoder *encoder=[NBArgumentEncoder new];encoder.handle=handle;return encoder;
}
- (id<MTLArgumentEncoder>)newArgumentEncoderWithBufferIndex:(NSUInteger)i reflection:(MTLAutoreleasedArgument *)reflection {
 if(reflection)*reflection=nil;
 return [self newArgumentEncoderWithBufferIndex:i];
}
@end

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
