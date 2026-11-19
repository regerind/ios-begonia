/* SPDX-License-Identifier: MIT
 * A 3D engine's frame through Metal, with nobody at the phone: SceneKit's SCNRenderer draws a lit red box seen by a
 * camera, over a blue background, into a colour texture with a depth attachment; the pixels are read back. SceneKit
 * brings its own shaders (SceneKit.framework/default.metallib, from the support image), so this exercises what a 3D
 * game asks of Metal: vertex buffers and descriptors, depth testing, lighting shaders, uniforms, more pipelines.
 *
 *   BEGONIA SCENEKIT: ... lines; "BEGONIA SCENEKIT: PASS n/m" at the end.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <SceneKit/SceneKit.h>
#import <UIKit/UIKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define W 256
#define H 256

static unsigned checks, passed;

static void check(const uint8_t *px, int x, int y, bool (*want)(int r, int g, int b), const char *what) {
    const uint8_t *p = px + ((size_t)y * W + (size_t)x) * 4;      /* BGRA, rows from the top */
    bool ok = want(p[2], p[1], p[0]);
    checks++;
    passed += ok;
    printf("BEGONIA SCENEKIT: %s at %d,%d: %u,%u,%u %s\n", what, x, y, p[2], p[1], p[0], ok ? "ok" : "WRONG");
}
static bool blue(int r, int g, int b) {return b > 200 && r < 40 && g < 40;}
static bool lit_red(int r, int g, int b) {return r > 60 && r > 2 * g && r > 2 * b;}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    alarm(170);
    puts("BEGONIA SCENEKIT: begin");
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { puts("BEGONIA SCENEKIT: no Metal device"); return 1; }
        printf("BEGONIA SCENEKIT: device %s\n", device.name.UTF8String);

        SCNScene *scene = [SCNScene scene];
        SCNBox *box = [SCNBox boxWithWidth:1 height:1 length:1 chamferRadius:0];
        box.firstMaterial.diffuse.contents = [UIColor redColor];
        box.firstMaterial.lightingModelName = SCNLightingModelLambert;
        SCNNode *boxNode = [SCNNode nodeWithGeometry:box];
        boxNode.eulerAngles = SCNVector3Make(0.4f, 0.6f, 0);
        [scene.rootNode addChildNode:boxNode];

        SCNNode *camera = [SCNNode node];
        camera.camera = [SCNCamera camera];
        camera.position = SCNVector3Make(0, 0, 3);
        [scene.rootNode addChildNode:camera];

        SCNNode *light = [SCNNode node];
        light.light = [SCNLight light];
        light.light.type = SCNLightTypeDirectional;
        light.eulerAngles = SCNVector3Make(-0.3f, 0.3f, 0);
        [scene.rootNode addChildNode:light];
        SCNNode *ambient = [SCNNode node];
        ambient.light = [SCNLight light];
        ambient.light.type = SCNLightTypeAmbient;
        ambient.light.color = [UIColor colorWithWhite:0.3 alpha:1];
        [scene.rootNode addChildNode:ambient];

        SCNRenderer *renderer = [SCNRenderer rendererWithDevice:device options:nil];
        renderer.scene = scene;
        renderer.pointOfView = camera;
        printf("BEGONIA SCENEKIT: renderer made (rendering API %lu, reverse Z %d)\n", (unsigned long)renderer.renderingAPI,
               renderer.usesReverseZ);

        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:W height:H mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        cd.storageMode = MTLStorageModeShared;
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                      width:W height:H mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        id<MTLTexture> colour = [device newTextureWithDescriptor:cd], depth = [device newTextureWithDescriptor:dd];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!colour || !depth || !queue) { puts("BEGONIA SCENEKIT: no targets or queue"); return 1; }

        for (int frame = 0; frame < 3; frame++) {
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = colour;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.depthAttachment.texture = depth;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            /* (SceneKit's depth is reversed by default -- near is 1, far is 0, the test "greater" -- as an SCNView clears it) */
            pass.depthAttachment.clearDepth = renderer.usesReverseZ ? 0 : 1;
            pass.depthAttachment.storeAction = MTLStoreActionDontCare;
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            [renderer renderAtTime:frame / 60.0 viewport:CGRectMake(0, 0, W, H) commandBuffer:commands passDescriptor:pass];
            [commands commit];
            [commands waitUntilCompleted];
            printf("BEGONIA SCENEKIT: frame %d status %lu%s%s\n", frame, (unsigned long)commands.status,
                   commands.error ? " error " : "", commands.error ? commands.error.localizedDescription.UTF8String : "");
        }

        static uint8_t px[W * H * 4];
        [colour getBytes:px bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
        unsigned red = 0, other = 0;
        for (int i = 0; i < W * H; i++) {
            if (lit_red(px[i * 4 + 2], px[i * 4 + 1], px[i * 4])) red++;
            else if (!blue(px[i * 4 + 2], px[i * 4 + 1], px[i * 4])) other++;
        }
        printf("BEGONIA SCENEKIT: %u red pixels, %u neither red nor the background, of %u\n", red, other, W * H);
        check(px, 8, 8, blue, "corner (background)");
        check(px, W / 2, H / 2, lit_red, "middle (the box)");
        checks++;
        passed += red > 4000 && red < 40000;
        printf("BEGONIA SCENEKIT: the box covers %u pixels (want 4000 to 40000) %s\n", red, red > 4000 && red < 40000 ? "ok" : "WRONG");
        printf("BEGONIA SCENEKIT: %s %u/%u\n", passed == checks ? "PASS" : "FAIL", passed, checks);

        /* the speed of a 3D frame: the box turning, 60 frames, each waited for */
        [boxNode runAction:[SCNAction repeatActionForever:[SCNAction rotateByX:0 y:M_PI z:0 duration:1]]];
        uint64_t began = clock_gettime_nsec_np(CLOCK_UPTIME_RAW), slowest = 0;
        for (int frame = 0; frame < 60; frame++) {
            uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = colour;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 1, 1);
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.depthAttachment.texture = depth;
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.clearDepth = renderer.usesReverseZ ? 0 : 1;
            pass.depthAttachment.storeAction = MTLStoreActionDontCare;
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            [renderer renderAtTime:1 + frame / 60.0 viewport:CGRectMake(0, 0, W, H) commandBuffer:commands passDescriptor:pass];
            [commands commit];
            [commands waitUntilCompleted];
            uint64_t t = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
            if (t > slowest) slowest = t;
        }
        uint64_t all = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - began;
        printf("BEGONIA SCENEKIT: speed: a lit box, 60 frames in %llu ms: %.1f ms a frame, the slowest %.1f ms\n",
               all / 1000000ull, all / 60.0 / 1e6, slowest / 1e6);
    }
    return passed == checks ? 0 : 2;
}
