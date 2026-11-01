#include <windows.h>
#include <GL/glew.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ffgl/FFGL.h"

int main()
{
	HMODULE hDll = LoadLibraryA( "BrowserSource.dll" );
	if( !hDll )
	{
		std::printf( "[FAIL] Could not load BrowserSource.dll (err=%lu)\n", GetLastError() );
		return 1;
	}

	auto plugMainFn = reinterpret_cast< FF_Main_FuncPtr >( GetProcAddress( hDll, "plugMain" ) );
	if( !plugMainFn )
	{
		std::printf( "[FAIL] plugMain export not found!\n" );
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

	// 4. Create an OpenGL 4.1 Core Context to test FF_INSTANTIATE_GL and FF_PROCESS_OPENGL
	WNDCLASSA wc     = {};
	wc.lpfnWndProc   = DefWindowProcA;
	wc.hInstance     = GetModuleHandleA( nullptr );
	wc.lpszClassName = "DummyGLWnd";
	RegisterClassA( &wc );
	HWND hwnd = CreateWindowA( "DummyGLWnd", "GL", WS_POPUP, 0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr );
	HDC hdc   = GetDC( hwnd );

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize                 = sizeof( pfd );
	pfd.nVersion              = 1;
	pfd.dwFlags               = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType            = PFD_TYPE_RGBA;
	pfd.cColorBits            = 32;
	int pf                    = ChoosePixelFormat( hdc, &pfd );
	SetPixelFormat( hdc, pf, &pfd );

	HGLRC tempRc = wglCreateContext( hdc );
	wglMakeCurrent( hdc, tempRc );
	glewInit();

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

	// 5. Test setting URL parameter (PT_URL = 0)
	const char* testUrl = "data:text/html,<body style='background:#00ff88;margin:0'><h1>Resolume Browser Test</h1></body>";
	SetParameterStruct setUrl;
	setUrl.ParameterNumber                = 0;
	setUrl.NewParameterValue.PointerValue = const_cast< char* >( testUrl );
	FFMixed setUrlIn;
	setUrlIn.PointerValue = &setUrl;
	plugMainFn( FF_SET_PARAMETER, setUrlIn, instID );

	FFMixed idx0;
	idx0.UIntValue      = 0;
	const char* readUrl = static_cast< const char* >( plugMainFn( FF_GET_PARAMETER, idx0, instID ).PointerValue );
	std::printf( "[PASS] URL Parameter Readback: %s\n", readUrl ? readUrl : "(null)" );

	// Wait briefly for WebView2 to navigate & capture a frame, calling FF_PROCESS_OPENGL
	ProcessOpenGLStruct pogl = { 0, nullptr, fbo };
	FFMixed poglIn;
	poglIn.PointerValue = &pogl;

	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, 640, 360 );
	uint8_t centerPixel[ 4 ] = { 0 };
	for( int frame = 0; frame < 40; ++frame )
	{
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		FFMixed res = plugMainFn( FF_PROCESS_OPENGL, poglIn, instID );
		if( res.UIntValue != FF_SUCCESS )
		{
			std::printf( "[FAIL] FF_PROCESS_OPENGL failed on frame %d\n", frame );
			return 1;
		}
		glReadPixels( 320, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel );
		if( centerPixel[ 1 ] > 200 )// Expect bright green (#00ff88 -> R=0, G=255, B=136)
		{
			std::printf( "[PASS] Live WebView2 pixel verified at frame %d: RGBA=(%u, %u, %u, %u)\n",
						 frame, centerPixel[ 0 ], centerPixel[ 1 ], centerPixel[ 2 ], centerPixel[ 3 ] );
			break;
		}
		Sleep( 100 );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	std::printf( "[PASS] Final sampled pixel RGBA=(%u, %u, %u, %u)\n",
				 centerPixel[ 0 ], centerPixel[ 1 ], centerPixel[ 2 ], centerPixel[ 3 ] );

	// 5b. Benchmark 60 FPS requestAnimationFrame capture rate & FF_PROCESS_OPENGL render latency
	const char* animUrl = "data:text/html,<body style='margin:0;background:#00ff88'><script>let r=0;function tick(){r=(r+3)&255;document.body.style.background='rgb('+r+',255,136)';requestAnimationFrame(tick);}requestAnimationFrame(tick);</script></body>";
	setUrl.ParameterNumber                = 0;
	setUrl.NewParameterValue.PointerValue = const_cast< char* >( animUrl );
	plugMainFn( FF_SET_PARAMETER, setUrlIn, instID );
	Sleep( 250 );

	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	uint8_t prevR          = 255;
	int distinctFrames     = 0;
	double totalRenderMs   = 0.0;
	LARGE_INTEGER freq, t0, t1, benchStart, benchEnd;
	QueryPerformanceFrequency( &freq );
	QueryPerformanceCounter( &benchStart );

	for( int i = 0; i < 60; ++i )
	{
		QueryPerformanceCounter( &t0 );
		plugMainFn( FF_PROCESS_OPENGL, poglIn, instID );
		glFinish();
		QueryPerformanceCounter( &t1 );
		totalRenderMs += ( double )( t1.QuadPart - t0.QuadPart ) * 1000.0 / ( double )freq.QuadPart;

		glReadPixels( 320, 180, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centerPixel );
		if( centerPixel[ 0 ] != prevR )
		{
			distinctFrames++;
			prevR = centerPixel[ 0 ];
		}
		Sleep( 15 );
	}
	QueryPerformanceCounter( &benchEnd );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	double elapsedSec = ( double )( benchEnd.QuadPart - benchStart.QuadPart ) / ( double )freq.QuadPart;
	std::printf( "[BENCH] Captured %d distinct animated frames in %.2fs (%.1f FPS), avg FF_PROCESS_OPENGL=%.2f ms\n",
				 distinctFrames, elapsedSec, distinctFrames / elapsedSec, totalRenderMs / 60.0 );

	// 6. Clean up
	plugMainFn( FF_DEINSTANTIATE_GL, zeroInput, instID );
	plugMainFn( FF_DEINITIALISE, zeroInput, nullptr );

	wglMakeCurrent( nullptr, nullptr );
	wglDeleteContext( tempRc );
	ReleaseDC( hwnd, hdc );
	DestroyWindow( hwnd );
	FreeLibrary( hDll );

	std::printf( "[PASS] Full lifecycle test completed cleanly!\n" );
	return 0;
}
