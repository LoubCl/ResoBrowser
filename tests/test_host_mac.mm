#import <Cocoa/Cocoa.h>
#import <CoreFoundation/CoreFoundation.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ffgl/FFGL.h"

int main( int argc, const char* argv[] )
{
	@autoreleasepool
	{
		[NSApplication sharedApplication];

		NSString* bundlePath = @"./BrowserSource.bundle";
		if( argc > 1 )
			bundlePath = [NSString stringWithUTF8String:argv[ 1 ]];

		NSURL* bundleUrl = [NSURL fileURLWithPath:bundlePath];
		CFBundleRef bundle = CFBundleCreate( kCFAllocatorDefault, ( __bridge CFURLRef )bundleUrl );
		if( !bundle || !CFBundleLoadExecutable( bundle ) )
		{
			std::printf( "[FAIL] Could not load BrowserSource.bundle at %s\n", [bundlePath UTF8String] );
			return 1;
		}

		auto plugMainFn = reinterpret_cast< FF_Main_FuncPtr >(
			CFBundleGetFunctionPointerForName( bundle, CFSTR( "plugMain" ) ) );
		if( !plugMainFn )
		{
			std::printf( "[FAIL] plugMain export not found in BrowserSource.bundle!\n" );
			return 1;
		}

		FFMixed zeroInput;
		zeroInput.UIntValue = 0;

		// 1. FF_GET_INFO
		FFMixed infoRet = plugMainFn( FF_GET_INFO, zeroInput, nullptr );
		auto* info      = static_cast< PluginInfoStruct* >( infoRet.PointerValue );
		if( !info )
		{
			std::printf( "[FAIL] FF_GET_INFO returned null\n" );
			return 1;
		}
		char idStr[ 5 ]    = { 0 };
		char nameStr[ 17 ] = { 0 };
		std::memcpy( idStr, info->PluginUniqueID, 4 );
		std::memcpy( nameStr, info->PluginName, 16 );
		std::printf( "[PASS] Plugin Info: ID='%s', Name='%s', Type=%u (FF_SOURCE=%u)\n", idStr, nameStr, info->PluginType, FF_SOURCE );

		// 2. FF_INITIALISE_V2
		FFMixed initRet = plugMainFn( FF_INITIALISE_V2, zeroInput, nullptr );
		if( initRet.UIntValue != FF_SUCCESS )
		{
			std::printf( "[FAIL] FF_INITIALISE_V2 failed\n" );
			return 1;
		}

		// 3. Inspect parameters
		FFMixed numParamsRet = plugMainFn( FF_GET_NUM_PARAMETERS, zeroInput, nullptr );
		uint32_t numParams   = numParamsRet.UIntValue;
		std::printf( "[PASS] Parameter Count: %u\n", numParams );

		for( uint32_t i = 0; i < numParams; ++i )
		{
			FFMixed idx;
			idx.UIntValue     = i;
			const char* pName = static_cast< const char* >( plugMainFn( FF_GET_PARAMETER_NAME, idx, nullptr ).PointerValue );
			uint32_t pType    = plugMainFn( FF_GET_PARAMETER_TYPE, idx, nullptr ).UIntValue;

			char groupBuf[ 64 ] = { 0 };
			GetStringStruct gs  = { i, { groupBuf, sizeof( groupBuf ) - 1 } };
			FFMixed gsIn;
			gsIn.PointerValue = &gs;
			plugMainFn( FF_GET_PARAM_GROUP, gsIn, nullptr );

			std::printf( "  Param[%2u]: Name='%-16s' Type=%3u Group='%s'\n", i, pName ? pName : "", pType, groupBuf );
		}

		// 4. Create an OpenGL 4.1 Core Context on macOS via CGL
		CGLPixelFormatAttribute attrs[] = {
			kCGLPFAOpenGLProfile,
			(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
			kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
			kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
			kCGLPFAAccelerated,
			(CGLPixelFormatAttribute)0
		};
		CGLPixelFormatObj pix = nullptr;
		GLint numPix          = 0;
		CGLChoosePixelFormat( attrs, &pix, &numPix );
		if( !pix )
		{
			CGLPixelFormatAttribute swAttrs[] = {
				kCGLPFAOpenGLProfile,
				(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
				(CGLPixelFormatAttribute)0
			};
			CGLChoosePixelFormat( swAttrs, &pix, &numPix );
		}

		CGLContextObj ctx = nullptr;
		CGLCreateContext( pix, nullptr, &ctx );
		CGLDestroyPixelFormat( pix );
		CGLSetCurrentContext( ctx );

		GLuint fbo = 0, fboTex = 0;
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glGenTextures( 1, &fboTex );
		glBindTexture( GL_TEXTURE_2D, fboTex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 640, 360, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );

		FFGLViewportStruct vp = { 0, 0, 640, 360 };
		FFMixed vpIn;
		vpIn.PointerValue   = &vp;
		FFMixed instRet     = plugMainFn( FF_INSTANTIATE_GL, vpIn, nullptr );
		FFInstanceID instID = instRet.PointerValue;
		if( !instID || instRet.UIntValue == FF_FAIL )
		{
			std::printf( "[FAIL] FF_INSTANTIATE_GL failed\n" );
			return 1;
		}
		std::printf( "[PASS] FF_INSTANTIATE_GL succeeded\n" );

		// 5. Test setting URL parameter
		const char* testUrl = "data:text/html,<body style='background:#00ff88;margin:0'><h1>Resolume macOS Test</h1></body>";
		SetParameterStruct setUrl;
		setUrl.ParameterNumber                = 0;
		setUrl.NewParameterValue.PointerValue = const_cast< char* >( testUrl );
		FFMixed setUrlIn;
		setUrlIn.PointerValue = &setUrl;
		plugMainFn( FF_SET_PARAMETER, setUrlIn, instID );

		ProcessOpenGLStruct pogl = { 0, nullptr, fbo };
		FFMixed poglIn;
		poglIn.PointerValue = &pogl;

		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glViewport( 0, 0, 640, 360 );
		uint8_t centerPixel[ 4 ] = { 0 };
		for( int frame = 0; frame < 40; ++frame )
		{
			[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.08]];
			glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			FFMixed res = plugMainFn( FF_PROCESS_OPENGL, poglIn, instID );
			if( res.UIntValue != FF_SUCCESS )
			{
				std::printf( "[FAIL] FF_PROCESS_OPENGL failed on frame %d\n", frame );
				return 1;
			}
			glReadPixels( 320, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel );
			if( centerPixel[ 1 ] > 200 )
			{
				std::printf( "[PASS] Live WKWebView pixel verified at frame %d: RGBA=(%u, %u, %u, %u)\n",
							 frame, centerPixel[ 0 ], centerPixel[ 1 ], centerPixel[ 2 ], centerPixel[ 3 ] );
				break;
			}
		}
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		std::printf( "[PASS] Final sampled pixel RGBA=(%u, %u, %u, %u)\n",
					 centerPixel[ 0 ], centerPixel[ 1 ], centerPixel[ 2 ], centerPixel[ 3 ] );

		plugMainFn( FF_DEINSTANTIATE_GL, zeroInput, instID );
		plugMainFn( FF_DEINITIALISE, zeroInput, nullptr );

		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( ctx );
		CFRelease( bundle );

		std::printf( "[PASS] Full macOS lifecycle test completed cleanly!\n" );
		return 0;
	}
}
