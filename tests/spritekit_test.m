/* SPDX-License-Identifier: MIT
 * A game engine's frame through Metal, with nobody at the phone: SpriteKit's SKRenderer draws a scene -- a background
 * colour, coloured sprites, a sprite with a texture made here, a shape and a rotated sprite -- into a texture of our
 * own, and the pixels are read back and compared with what the scene is. SpriteKit brings its own shaders
 * (SpriteKit.framework/default.metallib, from the support image: userland/metal/frontend_reflection.mm.inc), so this
 * exercises what a 2D game asks of Metal: libraries, pipelines with reflection, buffers, textures, samplers, blending.
 *
 *   BEGONIA SPRITEKIT: ... lines; "BEGONIA SPRITEKIT: PASS n/m" at the end.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <SpriteKit/SpriteKit.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define W 256
#define H 256

static unsigned checks, passed;

static void check(const uint8_t *px, int x, int y, int r, int g, int b, const char *what) {
    /* the texture is BGRA, rows from the top; SpriteKit's y goes up from the bottom */
    const uint8_t *p = px + ((size_t)(H - 1 - y) * W + (size_t)x) * 4;
    int ok = abs(p[2] - r) <= 24 && abs(p[1] - g) <= 24 && abs(p[0] - b) <= 24;
    checks++;
    passed += ok;
    printf("BEGONIA SPRITEKIT: %s at %d,%d: %u,%u,%u (want %d,%d,%d) %s\n", what, x, y, p[2], p[1], p[0], r, g, b,
           ok ? "ok" : "WRONG");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
    alarm(90);
    puts("BEGONIA SPRITEKIT: begin");
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { puts("BEGONIA SPRITEKIT: no Metal device"); return 1; }
        printf("BEGONIA SPRITEKIT: device %s\n", device.name.UTF8String);

        SKScene *scene = [SKScene sceneWithSize:CGSizeMake(W, H)];
        scene.backgroundColor = [SKColor colorWithRed:0 green:0 blue:1 alpha:1];
        scene.scaleMode = SKSceneScaleModeFill;

        SKSpriteNode *red = [SKSpriteNode spriteNodeWithColor:[SKColor redColor] size:CGSizeMake(64, 64)];
        red.position = CGPointMake(48, 48);
        [scene addChild:red];

        SKSpriteNode *green = [SKSpriteNode spriteNodeWithColor:[SKColor greenColor] size:CGSizeMake(64, 64)];
        green.position = CGPointMake(208, 48);
        green.zRotation = M_PI / 4;
        [scene addChild:green];

        /* a texture made from bytes: left half white, right half yellow */
        uint8_t bytes[32 * 32 * 4];
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                uint8_t *q = bytes + (y * 32 + x) * 4;
                q[0] = 255; q[1] = 255; q[2] = x < 16 ? 255 : 0; q[3] = 255;   /* RGBA */
            }
        SKTexture *texture = [SKTexture textureWithData:[NSData dataWithBytes:bytes length:sizeof bytes] size:CGSizeMake(32, 32)];
        texture.filteringMode = SKTextureFilteringNearest;
        SKSpriteNode *textured = [SKSpriteNode spriteNodeWithTexture:texture size:CGSizeMake(64, 64)];
        textured.position = CGPointMake(48, 208);
        [scene addChild:textured];

        SKShapeNode *circle = [SKShapeNode shapeNodeWithCircleOfRadius:30];
        circle.fillColor = [SKColor magentaColor];
        circle.strokeColor = [SKColor magentaColor];
        circle.position = CGPointMake(208, 208);
        [scene addChild:circle];

        SKSpriteNode *half = [SKSpriteNode spriteNodeWithColor:[SKColor whiteColor] size:CGSizeMake(40, 40)];
        half.alpha = 0.5;        /* blended over the blue: (128,128,255) */
        half.position = CGPointMake(128, 128);
        [scene addChild:half];

        SKRenderer *renderer = [SKRenderer rendererWithDevice:device];
        renderer.scene = scene;
        puts("BEGONIA SPRITEKIT: renderer made");

        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                     width:W height:H mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.storageMode = MTLStorageModeShared;
        id<MTLTexture> target = [device newTextureWithDescriptor:d];
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!target || !queue) { puts("BEGONIA SPRITEKIT: no target or queue"); return 1; }

        for (int frame = 0; frame < 3; frame++) {
            [renderer updateAtTime:frame / 60.0];
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = target;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            [renderer renderWithViewport:CGRectMake(0, 0, W, H) commandBuffer:commands renderPassDescriptor:pass];
            [commands commit];
            [commands waitUntilCompleted];
            printf("BEGONIA SPRITEKIT: frame %d status %lu%s%s\n", frame, (unsigned long)commands.status,
                   commands.error ? " error " : "", commands.error ? commands.error.localizedDescription.UTF8String : "");
        }

        static uint8_t px[W * H * 4];
        [target getBytes:px bytesPerRow:W * 4 fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
        check(px, 128, 20, 0, 0, 255, "background");
        check(px, 48, 48, 255, 0, 0, "red sprite");
        check(px, 208, 48, 0, 255, 0, "rotated green sprite, middle");
        check(px, 208 + 40, 48, 0, 0, 255, "rotated green sprite, beyond its corner");
        check(px, 30, 208, 255, 255, 255, "textured sprite, left (white)");
        check(px, 66, 208, 255, 255, 0, "textured sprite, right (yellow)");
        check(px, 208, 208, 255, 0, 255, "circle shape");
        check(px, 128, 128, 128, 128, 255, "half-transparent sprite");
        printf("BEGONIA SPRITEKIT: %s %u/%u\n", passed == checks ? "PASS" : "FAIL", passed, checks);
    }
    return passed == checks ? 0 : 2;
}
