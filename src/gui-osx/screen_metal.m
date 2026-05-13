/*
  Hatari - screen_metal.m
  Copyright (C) 2026 by manni07
  Created: 2026-05-13

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "main.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif
#include <SDL_syswm.h>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <simd/simd.h>

#include "screen_metal.h"

typedef struct
{
	vector_float2 position;
	vector_float2 texcoord;
} screen_metal_vertex_t;

static id<MTLDevice> metalDevice;
static id<MTLCommandQueue> metalCommandQueue;
static id<MTLRenderPipelineState> metalPipelineState;
static id<MTLSamplerState> metalSamplerState;
static id<MTLTexture> metalTexture;
static CAMetalLayer *metalLayer;
static NSView *metalHostView;
static int metalSourceWidth;
static int metalSourceHeight;
static int metalWindowWidth;
static int metalWindowHeight;
static int metalContentX;
static int metalContentY;
static int metalContentWidth;
static int metalContentHeight;

static const screen_metal_vertex_t metalVertices[] =
{
	{ { -1.0f, -1.0f }, { 0.0f, 1.0f } },
	{ { -1.0f,  1.0f }, { 0.0f, 0.0f } },
	{ {  1.0f, -1.0f }, { 1.0f, 1.0f } },
	{ {  1.0f,  1.0f }, { 1.0f, 0.0f } }
};

static id<MTLTexture> ScreenMetal_CreateTexture(int width, int height)
{
	MTLTextureDescriptor *desc;

	desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
	                                                          width:width
	                                                         height:height
	                                                      mipmapped:NO];
	desc.usage = MTLTextureUsageShaderRead;
	desc.storageMode = MTLStorageModeShared;
	return [metalDevice newTextureWithDescriptor:desc];
}

static void ScreenMetal_UpdateContentRect(void)
{
	double scaleX, scaleY, scale;
	double drawWidth, drawHeight;

	if (metalSourceWidth <= 0 || metalSourceHeight <= 0
	    || metalWindowWidth <= 0 || metalWindowHeight <= 0)
	{
		metalContentX = 0;
		metalContentY = 0;
		metalContentWidth = 0;
		metalContentHeight = 0;
		return;
	}

	scaleX = (double)metalWindowWidth / metalSourceWidth;
	scaleY = (double)metalWindowHeight / metalSourceHeight;
	scale = fmin(scaleX, scaleY);
	drawWidth = metalSourceWidth * scale;
	drawHeight = metalSourceHeight * scale;

	metalContentX = (int)((metalWindowWidth - drawWidth) / 2.0);
	metalContentY = (int)((metalWindowHeight - drawHeight) / 2.0);
	metalContentWidth = (int)drawWidth;
	metalContentHeight = (int)drawHeight;
}

static bool ScreenMetal_CreatePipeline(void)
{
	NSError *error = nil;
	id<MTLLibrary> library;
	id<MTLFunction> vertexFunction;
	id<MTLFunction> fragmentFunction;
	MTLRenderPipelineDescriptor *pipelineDesc;
	static NSString *shaderSource =
		@"#include <metal_stdlib>\n"
		 "using namespace metal;\n"
		 "struct VertexIn { float2 position; float2 texcoord; };\n"
		 "struct VertexOut { float4 position [[position]]; float2 texcoord; };\n"
		 "vertex VertexOut hatari_vertex(const device VertexIn *vertices [[buffer(0)]], uint vid [[vertex_id]]) {\n"
		 "  VertexOut out;\n"
		 "  out.position = float4(vertices[vid].position, 0.0, 1.0);\n"
		 "  out.texcoord = vertices[vid].texcoord;\n"
		 "  return out;\n"
		 "}\n"
		 "fragment float4 hatari_fragment(VertexOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler s [[sampler(0)]]) {\n"
		 "  return tex.sample(s, in.texcoord);\n"
		 "}\n";

	library = [metalDevice newLibraryWithSource:shaderSource options:nil error:&error];
	if (!library)
		return false;

	vertexFunction = [library newFunctionWithName:@"hatari_vertex"];
	fragmentFunction = [library newFunctionWithName:@"hatari_fragment"];
	if (!vertexFunction || !fragmentFunction)
		return false;

	pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
	pipelineDesc.vertexFunction = vertexFunction;
	pipelineDesc.fragmentFunction = fragmentFunction;
	pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

	metalPipelineState = [metalDevice newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
	return metalPipelineState != nil;
}

static void ScreenMetal_UpdateLayerSize(void)
{
	CGRect bounds;
	CGFloat scale;

	if (!metalLayer || !metalHostView)
		return;

	bounds = metalHostView.bounds;
	scale = metalHostView.window ? metalHostView.window.backingScaleFactor : [[NSScreen mainScreen] backingScaleFactor];
	metalLayer.frame = bounds;
	metalLayer.contentsScale = scale;
	metalLayer.drawableSize = CGSizeMake(bounds.size.width * scale, bounds.size.height * scale);
}

static bool ScreenMetal_CreateSampler(bool nearest)
{
	MTLSamplerDescriptor *desc;

	desc = [[MTLSamplerDescriptor alloc] init];
	desc.minFilter = nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
	desc.magFilter = nearest ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear;
	desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
	desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
	metalSamplerState = [metalDevice newSamplerStateWithDescriptor:desc];
	return metalSamplerState != nil;
}

bool ScreenMetal_Available(void)
{
	return MTLCreateSystemDefaultDevice() != nil;
}

bool ScreenMetal_Init(SDL_Window *window, int width, int height, int win_width, int win_height)
{
	SDL_SysWMinfo info;

	(void)win_width;
	(void)win_height;

	ScreenMetal_UnInit();

	if (!window)
		return false;

	metalDevice = MTLCreateSystemDefaultDevice();
	if (!metalDevice)
		return false;

	SDL_VERSION(&info.version);
	if (!SDL_GetWindowWMInfo(window, &info))
		return false;

	metalHostView = info.info.cocoa.window.contentView;
	if (!metalHostView)
		return false;

	metalLayer = [CAMetalLayer layer];
	metalLayer.device = metalDevice;
	metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	metalLayer.framebufferOnly = NO;
	metalHostView.wantsLayer = YES;
	metalHostView.layer = metalLayer;
	ScreenMetal_UpdateLayerSize();

	metalCommandQueue = [metalDevice newCommandQueue];
	if (!metalCommandQueue || !ScreenMetal_CreatePipeline() || !ScreenMetal_CreateSampler(false))
		return false;

	metalTexture = ScreenMetal_CreateTexture(width, height);
	if (!metalTexture)
		return false;

	metalSourceWidth = width;
	metalSourceHeight = height;
	metalWindowWidth = win_width;
	metalWindowHeight = win_height;
	ScreenMetal_UpdateContentRect();
	return true;
}

void ScreenMetal_UnInit(void)
{
	if (metalHostView && metalHostView.layer == metalLayer)
		metalHostView.layer = nil;
	metalTexture = nil;
	metalSamplerState = nil;
	metalPipelineState = nil;
	metalCommandQueue = nil;
	metalLayer = nil;
	metalHostView = nil;
	metalDevice = nil;
	metalSourceWidth = 0;
	metalSourceHeight = 0;
	metalWindowWidth = 0;
	metalWindowHeight = 0;
	metalContentX = 0;
	metalContentY = 0;
	metalContentWidth = 0;
	metalContentHeight = 0;
}

void ScreenMetal_SetTextureScale(int width, int height, int win_width, int win_height, bool nearest)
{
	if (!metalDevice)
		return;

	if (!metalTexture || metalSourceWidth != width || metalSourceHeight != height)
	{
		metalTexture = ScreenMetal_CreateTexture(width, height);
		metalSourceWidth = width;
		metalSourceHeight = height;
	}
	if (!metalSamplerState)
		ScreenMetal_CreateSampler(nearest);
	else
		ScreenMetal_CreateSampler(nearest);

	metalWindowWidth = win_width;
	metalWindowHeight = win_height;
	ScreenMetal_UpdateContentRect();
	ScreenMetal_UpdateLayerSize();
}

void ScreenMetal_Update(SDL_Surface *screen)
{
	id<CAMetalDrawable> drawable;
	MTLRenderPassDescriptor *passDesc;
	id<MTLCommandBuffer> commandBuffer;
	id<MTLRenderCommandEncoder> encoder;
	MTLViewport viewport;
	double scaleX, scaleY, scale, drawWidth, drawHeight;
	size_t bytesPerRow;

	if (!metalTexture || !metalLayer || !screen)
		return;

	ScreenMetal_UpdateLayerSize();
	bytesPerRow = (size_t)screen->pitch;
	[metalTexture replaceRegion:MTLRegionMake2D(0, 0, metalSourceWidth, metalSourceHeight)
	                mipmapLevel:0
	                  withBytes:screen->pixels
	                bytesPerRow:bytesPerRow];

	drawable = [metalLayer nextDrawable];
	if (!drawable)
		return;

	passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
	passDesc.colorAttachments[0].texture = drawable.texture;
	passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
	passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
	passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

	commandBuffer = [metalCommandQueue commandBuffer];
	encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
	[encoder setRenderPipelineState:metalPipelineState];
	[encoder setVertexBytes:metalVertices length:sizeof(metalVertices) atIndex:0];
	[encoder setFragmentTexture:metalTexture atIndex:0];
	[encoder setFragmentSamplerState:metalSamplerState atIndex:0];

	scaleX = metalLayer.drawableSize.width / metalSourceWidth;
	scaleY = metalLayer.drawableSize.height / metalSourceHeight;
	scale = fmin(scaleX, scaleY);
	drawWidth = metalSourceWidth * scale;
	drawHeight = metalSourceHeight * scale;
	viewport.originX = floor((metalLayer.drawableSize.width - drawWidth) / 2.0);
	viewport.originY = floor((metalLayer.drawableSize.height - drawHeight) / 2.0);
	viewport.width = drawWidth;
	viewport.height = drawHeight;
	viewport.znear = 0.0;
	viewport.zfar = 1.0;
	ScreenMetal_UpdateContentRect();
	[encoder setViewport:viewport];
	[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
	[encoder endEncoding];
	[commandBuffer presentDrawable:drawable];
	[commandBuffer commit];
}

void ScreenMetal_GetContentRect(int *x, int *y, int *width, int *height)
{
	if (x)
		*x = metalContentX;
	if (y)
		*y = metalContentY;
	if (width)
		*width = metalContentWidth;
	if (height)
		*height = metalContentHeight;
}
