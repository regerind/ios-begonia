/* SPDX-License-Identifier: MIT
 * cg_shapes_test: which CoreGraphics shapes come out, in a plain bitmap context and in WebKit's tile context
 * (CGIOSurfaceContextCreate on an IOSurface, as WebKit's GPU process draws a page). y67: an SVG logo drew its one
 * straight-edged letter and none of its curved ones; text and PNGs drew. Each shape is drawn alone on a cleared
 * 256x256 surface and the pixels it covered are counted; the plain bitmap context is the reference.
 *   cg_shapes_test      every shape, both ways: "CG SHAPES: <shape>: bitmap N, iosurface M (ratio)"
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurfaceRef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#define SIZE 256

typedef CGContextRef (*surface_context_fn)(IOSurfaceRef, size_t, size_t, size_t, size_t, CGColorSpaceRef, CGBitmapInfo);

static CFNumberRef
num(int value)
{
	return CFNumberCreate(NULL, kCFNumberIntType, &value);
}

static IOSurfaceRef
make_surface(void)
{
	CFMutableDictionaryRef props = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	CFNumberRef n;
	IOSurfaceRef surface;

	n = num(SIZE); CFDictionarySetValue(props, kIOSurfaceWidth, n); CFRelease(n);
	n = num(SIZE); CFDictionarySetValue(props, kIOSurfaceHeight, n); CFRelease(n);
	n = num(4); CFDictionarySetValue(props, kIOSurfaceBytesPerElement, n); CFRelease(n);
	n = num(SIZE * 4); CFDictionarySetValue(props, kIOSurfaceBytesPerRow, n); CFRelease(n);
	n = num('BGRA'); CFDictionarySetValue(props, kIOSurfacePixelFormat, n); CFRelease(n);
	n = num(SIZE * SIZE * 4); CFDictionarySetValue(props, kIOSurfaceAllocSize, n); CFRelease(n);
	surface = IOSurfaceCreate(props);
	CFRelease(props);
	return surface;
}

/* pixels with any alpha */
static unsigned
covered(const uint32_t *pixels)
{
	unsigned n = 0;

	for (int i = 0; i < SIZE * SIZE; i++) {
		n += (pixels[i] >> 24) != 0;
	}
	return n;
}

static void
shape(CGContextRef c, int which)
{
	CGMutablePathRef p = CGPathCreateMutable();

	CGContextSetRGBFillColor(c, 0.26, 0.52, 0.96, 1);
	CGContextSetRGBStrokeColor(c, 0.20, 0.66, 0.33, 1);
	switch (which) {
	case 0:                                         /* rect */
		CGContextFillRect(c, CGRectMake(20, 20, 100, 100));
		break;
	case 1:                                         /* ellipse */
		CGContextFillEllipseInRect(c, CGRectMake(20, 20, 100, 100));
		break;
	case 2:                                         /* circle by arc */
		CGPathAddArc(p, NULL, 128, 128, 60, 0, 2 * M_PI, false);
		CGContextAddPath(c, p);
		CGContextFillPath(c);
		break;
	case 3:                                         /* rounded rect */
		CGPathAddRoundedRect(p, NULL, CGRectMake(20, 20, 200, 120), 24, 24);
		CGContextAddPath(c, p);
		CGContextFillPath(c);
		break;
	case 4:                                         /* cubic Bezier, closed (a "D") */
		CGPathMoveToPoint(p, NULL, 40, 30);
		CGPathAddCurveToPoint(p, NULL, 220, 30, 220, 226, 40, 226);
		CGPathCloseSubpath(p);
		CGContextAddPath(c, p);
		CGContextFillPath(c);
		break;
	case 5:                                         /* quadratic, closed */
		CGPathMoveToPoint(p, NULL, 30, 200);
		CGPathAddQuadCurveToPoint(p, NULL, 128, 0, 226, 200);
		CGPathCloseSubpath(p);
		CGContextAddPath(c, p);
		CGContextFillPath(c);
		break;
	case 6:                                         /* even-odd ring */
		CGPathAddEllipseInRect(p, NULL, CGRectMake(28, 28, 200, 200));
		CGPathAddEllipseInRect(p, NULL, CGRectMake(78, 78, 100, 100));
		CGContextAddPath(c, p);
		CGContextEOFillPath(c);
		break;
	case 7:                                         /* stroked curve, round caps (a pen line) */
		CGContextSetLineWidth(c, 8);
		CGContextSetLineCap(c, kCGLineCapRound);
		CGPathMoveToPoint(p, NULL, 20, 128);
		CGPathAddCurveToPoint(p, NULL, 80, 10, 170, 250, 236, 128);
		CGContextAddPath(c, p);
		CGContextStrokePath(c);
		break;
	case 8:                                         /* polygon (straight edges, like the logo's "l") */
		CGPathMoveToPoint(p, NULL, 30, 30);
		CGPathAddLineToPoint(p, NULL, 220, 60);
		CGPathAddLineToPoint(p, NULL, 120, 220);
		CGPathCloseSubpath(p);
		CGContextAddPath(c, p);
		CGContextFillPath(c);
		break;
	case 9:                                         /* filled curve without antialiasing */
		CGContextSetShouldAntialias(c, false);
		CGContextFillEllipseInRect(c, CGRectMake(20, 20, 100, 100));
		break;
	}
	CGPathRelease(p);
}

static const char *const names[] = { "rect", "ellipse", "arc circle", "rounded rect", "cubic", "quadratic",
	                             "even-odd ring", "stroked curve", "polygon", "ellipse, no antialiasing" };

int
main(void)
{
	surface_context_fn create = (surface_context_fn)dlsym(RTLD_DEFAULT, "CGIOSurfaceContextCreate");
	CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGBitmapInfo info = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
	static uint32_t plain[SIZE * SIZE];

	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("CG SHAPES: CGIOSurfaceContextCreate is %s\n", create ? "there" : "NOT THERE");
	for (int which = 0; which < (int)(sizeof(names) / sizeof(names[0])); which++) {
		unsigned a = 0, b = 0;
		uint32_t seed = 0;

		memset(plain, 0, sizeof(plain));
		CGContextRef c = CGBitmapContextCreate(plain, SIZE, SIZE, 8, SIZE * 4, space, info);
		if (c != NULL) {
			shape(c, which);
			CGContextRelease(c);
			a = covered(plain);
		}
		IOSurfaceRef s = make_surface();
		if (s != NULL && create != NULL && (c = create(s, SIZE, SIZE, 8, 32, space, info)) != NULL) {
			shape(c, which);
			CGContextFlush(c);
			CGContextRelease(c);
			IOSurfaceLock(s, kIOSurfaceLockReadOnly, &seed);
			b = covered(IOSurfaceGetBaseAddress(s));
			IOSurfaceUnlock(s, kIOSurfaceLockReadOnly, &seed);
		}
		if (s != NULL) {
			CFRelease(s);
		}
		printf("CG SHAPES: %s: bitmap %u, iosurface %u%s\n", names[which], a, b,
		    a == 0 ? "  (NOTHING in the plain bitmap)" : (b * 10 < a * 9 || b * 10 > a * 11) ? "  (DIFFERENT)" : "");
	}
	printf("CG SHAPES: end\n");
	return 0;
}
