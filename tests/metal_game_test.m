/* SPDX-License-Identifier: MIT
 * What games ask of Metal, one feature at a time, with nobody at the phone. The shaders are Metal source compiled on the
 * phone (newLibraryWithSource: -- the system's compiler, then the runtime: userland/metal/frontend_source.mm.inc), as
 * engines do. Every part says "BEGONIA GAME: <part> PASS|FAIL ..." and the last line counts them.
 *
 *   compute      a kernel adding two buffers
 *   draw         an instanced draw of textured quads, a sampler, depth testing, a vertex descriptor
 *   indirect     a draw whose counts come from a buffer
 *   arguments    a fragment shader reading its colour through an argument buffer (MTLArgumentEncoder)
 *   astc         a texture in ASTC 4x4 (the compressed format iOS games ship), one void-extent block per 4x4
 *   mipmaps      generateMipmapsForTexture: and a read of the smallest level
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned parts, passed;

static void part(const char *name, bool ok, NSString *detail) {
    parts++;
    passed += ok;
    printf("BEGONIA GAME: %s %s %s\n", name, ok ? "PASS" : "FAIL", detail.UTF8String ?: "");
}

static const char *const source =
    "#include <metal_stdlib>
"
    "using namespace metal;
"
    "
"
    "kernel void add(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device float *out [[buffer(2)]],
"
    "                uint i [[thread_position_in_grid]]) {
"
    "    out[i] = a[i] + b[i];
"
    "}
"
    "
"
    "struct VertexIn { float2 position [[attribute(0)]]; float2 uv [[attribute(1)]]; };
"
    "struct VertexOut { float4 position [[position]]; float2 uv; };
"
    "struct Instance { float2 offset; float depth; float pad; };
"
    "
"
    "vertex VertexOut quad_vertex(VertexIn in [[stage_in]], constant Instance *instances [[buffer(1)]], uint instance [[instance_id]]) {
"
    "    VertexOut out;
"
    "    out.position = float4(in.position * 0.25 + instances[instance].offset, instances[instance].depth, 1);
"
    "    out.uv = in.uv;
"
    "    return out;
"
    "}
"
    "fragment half4 quad_fragment(VertexOut in [[stage_in]], texture2d<half> texture [[texture(0)]], sampler s [[sampler(0)]]) {
"
    "    return texture.sample(s, in.uv);
"
    "}
"
    "
"
    "struct Material { float4 colour; };
"
    "fragment float4 argument_fragment(VertexOut in [[stage_in]], constant Material &material [[buffer(0)]]) {
"
    "    return material.colour;
"
    "}
"
    "struct Arguments { device Material *material [[id(0)]]; };
"
    "fragment float4 argument_buffer_fragment(VertexOut in [[stage_in]], constant Arguments &arguments [[buffer(0)]]) {
"
    "    return arguments.material->colour;
"
    "}
";

static id<MTLTexture> target(id<MTLDevice> device, MTLPixelFormat format, NSUInteger w, NSUInteger h, MTLStorageMode mode) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = mode;
    return [device newTextureWithDescriptor:d];
}

static void pixel(id<MTLTexture> t, NSUInteger x, NSUInteger y, uint8_t out[4]) {
    [t getBytes:out bytesPerRow:t.width * 4 fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0];
}

static bool near(const uint8_t p[4], int b, int g, int r) {   /* BGRA */
    return abs(p[0] - b) <= 12 && abs(p[1] - g) <= 12 && abs(p[2] - r) <= 12;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(150);
    puts("BEGONIA GAME: begin");
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { puts("BEGONIA GAME: no Metal device"); return 1; }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:source] options:[MTLCompileOptions new] error:&error];
        printf("BEGONIA GAME: device %s, library %s %s\n", device.name.UTF8String, library ? "compiled" : "NOT compiled",
               error ? error.localizedDescription.UTF8String : "");
        if (!library) return 1;

        /* compute */
        {
            const unsigned n = 1024;
            id<MTLBuffer> a = [device newBufferWithLength:n * 4 options:MTLResourceStorageModeShared];
            id<MTLBuffer> b = [device newBufferWithLength:n * 4 options:MTLResourceStorageModeShared];
            id<MTLBuffer> out = [device newBufferWithLength:n * 4 options:MTLResourceStorageModeShared];
            for (unsigned i = 0; i < n; i++) {((float *)a.contents)[i] = i; ((float *)b.contents)[i] = 2 * i + 1;}
            id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"add"] error:&error];
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:pipeline];
            [encoder setBuffer:a offset:0 atIndex:0];
            [encoder setBuffer:b offset:0 atIndex:1];
            [encoder setBuffer:out offset:0 atIndex:2];
            [encoder dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [encoder endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            unsigned right = 0;
            for (unsigned i = 0; i < n; i++) right += ((float *)out.contents)[i] == 3.0f * i + 1;
            part("compute", pipeline && right == n, [NSString stringWithFormat:@"%u/%u right, status %lu", right, n, (unsigned long)commands.status]);
        }

        /* the quad, its texture and sampler, and the pipelines the draws below share */
        const float quad[] = {-1, -1, 0, 1,  1, -1, 1, 1,  -1, 1, 0, 0,  1, 1, 1, 0};
        id<MTLBuffer> vertices = [device newBufferWithBytes:quad length:sizeof quad options:MTLResourceStorageModeShared];
        MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format = MTLVertexFormatFloat2; vd.attributes[0].offset = 0; vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format = MTLVertexFormatFloat2; vd.attributes[1].offset = 8; vd.attributes[1].bufferIndex = 0;
        vd.layouts[0].stride = 16;
        uint8_t texels[4 * 4 * 4];
        for (int i = 0; i < 16; i++) {texels[i * 4] = 255; texels[i * 4 + 1] = 200; texels[i * 4 + 2] = 0; texels[i * 4 + 3] = 255;}  /* RGBA: orange */
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:NO];
        id<MTLTexture> orange = [device newTextureWithDescriptor:td];
        [orange replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:texels bytesPerRow:16];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterNearest;
        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sd];
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [library newFunctionWithName:@"quad_vertex"];
        pd.fragmentFunction = [library newFunctionWithName:@"quad_fragment"];
        pd.vertexDescriptor = vd;
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        id<MTLRenderPipelineState> textured = [device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!textured) printf("BEGONIA GAME: textured pipeline: %s\n", error.localizedDescription.UTF8String);
        MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = MTLCompareFunctionLess;
        dd.depthWriteEnabled = YES;
        id<MTLDepthStencilState> depthState = [device newDepthStencilStateWithDescriptor:dd];
        id<MTLTexture> colour = target(device, MTLPixelFormatBGRA8Unorm, 64, 64, MTLStorageModeShared);
        id<MTLTexture> depth = target(device, MTLPixelFormatDepth32Float, 64, 64, MTLStorageModePrivate);
        /* four instances in the four corners; the bottom left one twice, the second behind the first */
        const float instances[] = {-0.5, -0.5, 0.2, 0,  0.5, -0.5, 0.5, 0,  -0.5, 0.5, 0.5, 0,  0.5, 0.5, 0.5, 0};
        id<MTLBuffer> instanceBuffer = [device newBufferWithBytes:instances length:sizeof instances options:MTLResourceStorageModeShared];

        MTLRenderPassDescriptor *(^pass)(void) = ^{
            MTLRenderPassDescriptor *p = [MTLRenderPassDescriptor renderPassDescriptor];
            p.colorAttachments[0].texture = colour;
            p.colorAttachments[0].loadAction = MTLLoadActionClear;
            p.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            p.colorAttachments[0].storeAction = MTLStoreActionStore;
            p.depthAttachment.texture = depth;
            p.depthAttachment.loadAction = MTLLoadActionClear;
            p.depthAttachment.clearDepth = 1;
            p.depthAttachment.storeAction = MTLStoreActionDontCare;
            return p;
        };

        /* draw: instanced, textured, depth-tested */
        {
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:pass()];
            [e setRenderPipelineState:textured];
            [e setDepthStencilState:depthState];
            [e setVertexBuffer:vertices offset:0 atIndex:0];
            [e setVertexBuffer:instanceBuffer offset:0 atIndex:1];
            [e setFragmentTexture:orange atIndex:0];
            [e setFragmentSamplerState:sampler atIndex:0];
            [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:4];
            [e endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            uint8_t corners[4][4], middle[4];
            pixel(colour, 16, 48, corners[0]); pixel(colour, 48, 48, corners[1]); pixel(colour, 16, 16, corners[2]); pixel(colour, 48, 16, corners[3]);
            pixel(colour, 32, 32, middle);
            bool ok = near(corners[0], 0, 200, 255) && near(corners[1], 0, 200, 255) && near(corners[2], 0, 200, 255) &&
                      near(corners[3], 0, 200, 255) && near(middle, 0, 0, 0);
            part("draw", ok, [NSString stringWithFormat:@"corners %u,%u,%u %u,%u,%u middle %u,%u,%u (BGR), status %lu", corners[0][0],
                              corners[0][1], corners[0][2], corners[3][0], corners[3][1], corners[3][2], middle[0], middle[1], middle[2],
                              (unsigned long)commands.status]);
        }

        /* indirect: the same draw, its counts from a buffer (vertexCount 4, instanceCount 1, vertexStart 0, baseInstance 0) */
        {
            const uint32_t arguments[4] = {4, 1, 0, 0};
            id<MTLBuffer> indirect = [device newBufferWithBytes:arguments length:sizeof arguments options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:pass()];
            [e setRenderPipelineState:textured];
            [e setDepthStencilState:depthState];
            [e setVertexBuffer:vertices offset:0 atIndex:0];
            [e setVertexBuffer:instanceBuffer offset:16 atIndex:1];        /* the bottom right instance alone */
            [e setFragmentTexture:orange atIndex:0];
            [e setFragmentSamplerState:sampler atIndex:0];
            [e drawPrimitives:MTLPrimitiveTypeTriangleStrip indirectBuffer:indirect indirectBufferOffset:0];
            [e endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            uint8_t drawn[4], empty[4];
            pixel(colour, 48, 48, drawn); pixel(colour, 16, 48, empty);
            part("indirect", near(drawn, 0, 200, 255) && near(empty, 0, 0, 0),
                 [NSString stringWithFormat:@"drawn %u,%u,%u empty %u,%u,%u, status %lu", drawn[0], drawn[1], drawn[2], empty[0], empty[1],
                  empty[2], (unsigned long)commands.status]);
        }

        /* arguments: a material through an argument buffer */
        {
            MTLRenderPipelineDescriptor *ad = [pd copy];
            id<MTLFunction> fragment = [library newFunctionWithName:@"argument_buffer_fragment"];
            ad.fragmentFunction = fragment;
            id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:ad error:&error];
            id<MTLArgumentEncoder> encoder = [fragment newArgumentEncoderWithBufferIndex:0];
            const float green[4] = {0, 1, 0, 1};
            id<MTLBuffer> material = [device newBufferWithBytes:green length:sizeof green options:MTLResourceStorageModeShared];
            id<MTLBuffer> argumentBuffer = encoder ? [device newBufferWithLength:encoder.encodedLength options:MTLResourceStorageModeShared] : nil;
            [encoder setArgumentBuffer:argumentBuffer offset:0];
            [encoder setBuffer:material offset:0 atIndex:0];
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:pass()];
            [e setRenderPipelineState:pipeline];
            [e setDepthStencilState:depthState];
            [e setVertexBuffer:vertices offset:0 atIndex:0];
            [e setVertexBuffer:instanceBuffer offset:0 atIndex:1];
            [e setFragmentBuffer:argumentBuffer offset:0 atIndex:0];
            [e useResource:material usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
            [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:1];
            [e endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            uint8_t p[4];
            pixel(colour, 16, 48, p);
            part("arguments", pipeline && encoder && near(p, 0, 255, 0),
                 [NSString stringWithFormat:@"pipeline %s, encoder %s (%lu bytes), pixel %u,%u,%u, status %lu", pipeline ? "made" : "NOT made",
                  encoder ? "made" : "NOT made", (unsigned long)encoder.encodedLength, p[0], p[1], p[2], (unsigned long)commands.status]);
        }

        /* astc: 8x8 texels in four ASTC 4x4 void-extent blocks of one colour (blue), drawn by the textured pipeline */
        {
            bool supported = [device supportsFamily:MTLGPUFamilyApple2];
            /* a void-extent block (LDR, no extent), then its colour as four 16-bit values: R 0, G 0, B 0xffff, A 0xffff */
            const uint8_t block[16] = {0xfc, 0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};
            uint8_t blocks[64];
            for (int i = 0; i < 4; i++) memcpy(blocks + 16 * i, block, 16);
            MTLTextureDescriptor *ad = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatASTC_4x4_LDR width:8 height:8 mipmapped:NO];
            id<MTLTexture> astc = [device newTextureWithDescriptor:ad];
            [astc replaceRegion:MTLRegionMake2D(0, 0, 8, 8) mipmapLevel:0 withBytes:blocks bytesPerRow:32];
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            id<MTLRenderCommandEncoder> e = [commands renderCommandEncoderWithDescriptor:pass()];
            [e setRenderPipelineState:textured];
            [e setDepthStencilState:depthState];
            [e setVertexBuffer:vertices offset:0 atIndex:0];
            [e setVertexBuffer:instanceBuffer offset:0 atIndex:1];
            [e setFragmentTexture:astc atIndex:0];
            [e setFragmentSamplerState:sampler atIndex:0];
            [e drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:1];
            [e endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            uint8_t p[4];
            pixel(colour, 16, 48, p);
            part("astc", astc && near(p, 255, 0, 0), [NSString stringWithFormat:@"family Apple2 %d, texture %s, pixel %u,%u,%u (want 255,0,0 BGR), status %lu",
                                                      supported, astc ? "made" : "NOT made", p[0], p[1], p[2], (unsigned long)commands.status]);
        }

        /* mipmaps: a 16x16 texture of one colour, its levels made by the blit encoder, the 1x1 level read */
        {
            MTLTextureDescriptor *md = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:YES];
            md.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            id<MTLTexture> mipped = [device newTextureWithDescriptor:md];
            uint8_t levelZero[16 * 16 * 4];
            for (int i = 0; i < 256; i++) {levelZero[i * 4] = 40; levelZero[i * 4 + 1] = 80; levelZero[i * 4 + 2] = 160; levelZero[i * 4 + 3] = 255;}
            [mipped replaceRegion:MTLRegionMake2D(0, 0, 16, 16) mipmapLevel:0 withBytes:levelZero bytesPerRow:64];
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit generateMipmapsForTexture:mipped];
            [blit endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            uint8_t last[4] = {0};
            [mipped getBytes:last bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:mipped.mipmapLevelCount - 1];
            part("mipmaps", mipped.mipmapLevelCount == 5 && abs(last[0] - 40) <= 4 && abs(last[1] - 80) <= 4 && abs(last[2] - 160) <= 4,
                 [NSString stringWithFormat:@"%lu levels, the last %u,%u,%u,%u, status %lu", (unsigned long)mipped.mipmapLevelCount, last[0], last[1],
                  last[2], last[3], (unsigned long)commands.status]);
        }
        printf("BEGONIA GAME: %u/%u PASS\n", passed, parts);
    }
    return passed == parts ? 0 : 2;
}
