#include "FFGLBrowserSource.h"
#include "ffgl/FFGLLog.h"
#include "ffglex/FFGLScopedSamplerActivation.h"
#include "ffglex/FFGLScopedShaderBinding.h"
#include "ffglex/FFGLScopedTextureBinding.h"
#include "ffglex/FFGLUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ffglex;

enum BrowserParamID : FFUInt32
{
	// Group: Browser Source
	PT_URL = 0,
	PT_RELOAD,
	PT_BACK,
	PT_FORWARD,
	PT_TRANSPARENT_BG,
	PT_PAGE_ZOOM,
	PT_CUSTOM_CSS,

	// Group: Resolution & FPS
	PT_RES_MODE,
	PT_CUSTOM_WIDTH,
	PT_CUSTOM_HEIGHT,
	PT_TARGET_FPS,

	// Group: Audio
	PT_AUDIO_MUTE,
	PT_AUDIO_VOLUME,

	// Group: Interaction
	PT_INTERACTIVE_WINDOW,
	PT_MOUSE_X,
	PT_MOUSE_Y,
	PT_MOUSE_CLICK,
	PT_MOUSE_RIGHT_CLICK,
	PT_SCROLL_Y
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FFGLBrowserSource >,                 // Create method
	"BRWS",                                             // Plugin unique ID (4 chars)
	"Browser Source",                                   // Plugin name (max 16 chars)
	2,                                                  // API major version
	1,                                                  // API minor version
	1,                                                  // Plugin major version
	0,                                                  // Plugin minor version
	FF_SOURCE,                                          // Plugin type: Source
	"Cross-platform Web Browser Source (Win32 & macOS)",// Description
	"Resolume FFGL Browser Input Source"                // About
);

static const char kVertexShader[] = R"(#version 410 core
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

uniform int FlipY;
out vec2 uv;

void main()
{
	gl_Position = vPosition;
	// Browser pixel buffers are top-left origin.
	// If FlipY == 1, flip vUV.y so top-left buffer maps to standard OpenGL bottom-left UV.
	uv = vec2( vUV.x, FlipY == 1 ? ( 1.0 - vUV.y ) : vUV.y );
}
)";

static const char kFragmentShader[] = R"(#version 410 core
uniform sampler2D BrowserTex;

in vec2 uv;
out vec4 fragColor;

void main()
{
	fragColor = texture( BrowserTex, uv );
}
)";

FFGLBrowserSource::FFGLBrowserSource() :
	CFFGLPlugin( true )// Support top-left texture orientation if host requests it
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	// --- Group 1: Browser Source (URL & Navigation) ---
	SetParamInfo( PT_URL, "URL", FF_TYPE_TEXT, m_url.c_str() );
	SetParamGroup( PT_URL, "Browser Source" );

	SetParamInfo( PT_RELOAD, "Reload Page", FF_TYPE_EVENT, false );
	SetParamGroup( PT_RELOAD, "Browser Source" );

	SetParamInfo( PT_BACK, "Navigate Back", FF_TYPE_EVENT, false );
	SetParamGroup( PT_BACK, "Browser Source" );

	SetParamInfo( PT_FORWARD, "Navigate Fwd", FF_TYPE_EVENT, false );
	SetParamGroup( PT_FORWARD, "Browser Source" );

	SetParamInfo( PT_TRANSPARENT_BG, "Transparent BG", FF_TYPE_BOOLEAN, m_transparentBg );
	SetParamGroup( PT_TRANSPARENT_BG, "Browser Source" );

	SetParamInfo( PT_PAGE_ZOOM, "Page Zoom", FF_TYPE_STANDARD, m_pageZoom );
	SetParamRange( PT_PAGE_ZOOM, 0.25f, 3.0f );
	SetParamGroup( PT_PAGE_ZOOM, "Browser Source" );

	SetParamInfo( PT_CUSTOM_CSS, "Custom CSS", FF_TYPE_TEXT, m_customCss.c_str() );
	SetParamGroup( PT_CUSTOM_CSS, "Browser Source" );

	// --- Group 2: Resolution & FPS ---
	SetOptionParamInfo( PT_RES_MODE, "Resolution", 5, m_resolutionMode );
	SetParamElementInfo( PT_RES_MODE, 0, "Auto (Match Comp)", 0.0f );
	SetParamElementInfo( PT_RES_MODE, 1, "1920 x 1080 (1080p)", 1.0f );
	SetParamElementInfo( PT_RES_MODE, 2, "1280 x 720 (720p)", 2.0f );
	SetParamElementInfo( PT_RES_MODE, 3, "3840 x 2160 (4K)", 3.0f );
	SetParamElementInfo( PT_RES_MODE, 4, "Custom Size", 4.0f );
	SetParamGroup( PT_RES_MODE, "Resolution & FPS" );

	SetParamInfo( PT_CUSTOM_WIDTH, "Custom Width", FF_TYPE_INTEGER, m_customWidth );
	SetParamRange( PT_CUSTOM_WIDTH, 128.0f, 4096.0f );
	SetParamGroup( PT_CUSTOM_WIDTH, "Resolution & FPS" );

	SetParamInfo( PT_CUSTOM_HEIGHT, "Custom Height", FF_TYPE_INTEGER, m_customHeight );
	SetParamRange( PT_CUSTOM_HEIGHT, 128.0f, 4096.0f );
	SetParamGroup( PT_CUSTOM_HEIGHT, "Resolution & FPS" );

	SetParamInfo( PT_TARGET_FPS, "Target FPS", FF_TYPE_INTEGER, m_targetFps );
	SetParamRange( PT_TARGET_FPS, 1.0f, 120.0f );
	SetParamGroup( PT_TARGET_FPS, "Resolution & FPS" );

	UpdateResolutionVisibility( false );

	// --- Group 3: Audio ---
	SetParamInfo( PT_AUDIO_MUTE, "Mute Audio", FF_TYPE_BOOLEAN, m_audioMute );
	SetParamGroup( PT_AUDIO_MUTE, "Audio" );

	SetParamInfo( PT_AUDIO_VOLUME, "Audio Volume", FF_TYPE_STANDARD, m_audioVolume );
	SetParamRange( PT_AUDIO_VOLUME, 0.0f, 1.0f );
	SetParamGroup( PT_AUDIO_VOLUME, "Audio" );

	// --- Group 4: Interaction ---
	SetParamInfo( PT_INTERACTIVE_WINDOW, "Pop-out Window", FF_TYPE_BOOLEAN, m_interactiveWin );
	SetParamGroup( PT_INTERACTIVE_WINDOW, "Interaction" );

	SetParamInfo( PT_MOUSE_X, "Mouse X", FF_TYPE_XPOS, m_mouseX );
	SetParamGroup( PT_MOUSE_X, "Interaction" );

	SetParamInfo( PT_MOUSE_Y, "Mouse Y", FF_TYPE_YPOS, m_mouseY );
	SetParamGroup( PT_MOUSE_Y, "Interaction" );

	SetParamInfo( PT_MOUSE_CLICK, "Left Click", FF_TYPE_BOOLEAN, m_mouseLeftDown );
	SetParamGroup( PT_MOUSE_CLICK, "Interaction" );

	SetParamInfo( PT_MOUSE_RIGHT_CLICK, "Right Click", FF_TYPE_EVENT, false );
	SetParamGroup( PT_MOUSE_RIGHT_CLICK, "Interaction" );

	SetParamInfo( PT_SCROLL_Y, "Scroll Wheel", FF_TYPE_STANDARD, m_scrollY );
	SetParamRange( PT_SCROLL_Y, 0.0f, 1.0f );
	SetParamGroup( PT_SCROLL_Y, "Interaction" );

	FFGLLog::LogToHost( "Browser Source initialized" );
}

FFGLBrowserSource::~FFGLBrowserSource()
{
	if( m_browser )
	{
		m_browser->Shutdown();
		m_browser.reset();
	}
}

void FFGLBrowserSource::ResolveTargetDimensions( uint32_t& outWidth, uint32_t& outHeight ) const
{
	int mode = static_cast< int >( std::round( m_resolutionMode ) );
	switch( mode )
	{
	case 1:
		outWidth  = 1920;
		outHeight = 1080;
		break;
	case 2:
		outWidth  = 1280;
		outHeight = 720;
		break;
	case 3:
		outWidth  = 3840;
		outHeight = 2160;
		break;
	case 4:
		outWidth  = std::max< uint32_t >( 128, std::min< uint32_t >( 4096, static_cast< uint32_t >( m_customWidth ) ) );
		outHeight = std::max< uint32_t >( 128, std::min< uint32_t >( 4096, static_cast< uint32_t >( m_customHeight ) ) );
		break;
	case 0:
	default:
		outWidth  = currentViewport.width > 0 ? currentViewport.width : 1920;
		outHeight = currentViewport.height > 0 ? currentViewport.height : 1080;
		break;
	}
}

void FFGLBrowserSource::UpdateResolutionVisibility( bool raiseEvent )
{
	bool isCustom = ( static_cast< int >( std::round( m_resolutionMode ) ) == 4 );
	SetParamVisibility( PT_CUSTOM_WIDTH, isCustom, raiseEvent );
	SetParamVisibility( PT_CUSTOM_HEIGHT, isCustom, raiseEvent );
}

void FFGLBrowserSource::EnsureTextureAllocated( uint32_t width, uint32_t height )
{
	if( width == 0 || height == 0 )
		return;

	if( m_textureID != 0 && m_textureWidth == width && m_textureHeight == height )
		return;

	if( m_textureID == 0 )
		glGenTextures( 1, &m_textureID );

	Scoped2DTextureBinding texBinding( m_textureID );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );

	// Initialize with empty transparent black buffer
	std::vector< uint8_t > blank( static_cast< size_t >( width ) * height * 4, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, blank.data() );

	m_textureWidth  = width;
	m_textureHeight = height;
}

FFResult FFGLBrowserSource::InitGL( const FFGLViewportStruct* vp )
{
	CFFGLPlugin::InitGL( vp );

	if( !m_shader.Compile( kVertexShader, kFragmentShader ) )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !m_quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}

	{
		ScopedShaderBinding shaderBinding( m_shader.GetGLID() );
		m_texLocation   = m_shader.FindUniform( "BrowserTex" );
		m_flipYLocation = m_shader.FindUniform( "FlipY" );
		glUniform1i( m_texLocation, 0 );
	}

	uint32_t w = 1920, h = 1080;
	ResolveTargetDimensions( w, h );
	EnsureTextureAllocated( w, h );

	m_browser = CreateBrowserBackend();
	if( m_browser )
	{
		m_browser->Initialize( w, h, m_url );
		m_browser->SetTargetFps( static_cast< uint32_t >( std::max( 1.0f, m_targetFps ) ) );
		m_browser->SetTransparentBackground( m_transparentBg );
		m_browser->SetZoom( m_pageZoom );
		m_browser->SetAudioMuted( m_audioMute );
		m_browser->SetAudioVolume( m_audioVolume );
		if( !m_customCss.empty() )
			m_browser->SetCustomCss( m_customCss );
	}

	return FF_SUCCESS;
}

unsigned int FFGLBrowserSource::Resize( const FFGLViewportStruct* vp )
{
	CFFGLPlugin::Resize( vp );
	if( static_cast< int >( std::round( m_resolutionMode ) ) == 0 && m_browser )
	{
		uint32_t w = 1920, h = 1080;
		ResolveTargetDimensions( w, h );
		m_browser->Resize( w, h );
	}
	return FF_SUCCESS;
}

FFResult FFGLBrowserSource::ProcessOpenGL( ProcessOpenGLStruct* /*pGL*/ )
{
	if( m_browser )
	{
		// Sync interactive window state if the user closed the pop-out window directly
		bool actualVisible = m_browser->IsInteractiveWindowVisible();
		if( actualVisible != m_interactiveWin )
		{
			m_interactiveWin = actualVisible;
			RaiseParamEvent( PT_INTERACTIVE_WINDOW, FF_EVENT_FLAG_VALUE );
		}

		if( m_browser->CopyLatestFrame( m_frameCache, m_lastUploadedFrame ) )
		{
			if( m_frameCache.width > 0 && m_frameCache.height > 0 &&
				m_frameCache.pixels.size() >= static_cast< size_t >( m_frameCache.width ) * m_frameCache.height * 4 )
			{
				EnsureTextureAllocated( m_frameCache.width, m_frameCache.height );
				Scoped2DTextureBinding texBinding( m_textureID );
				glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
				glTexSubImage2D(
					GL_TEXTURE_2D,
					0,
					0,
					0,
					m_frameCache.width,
					m_frameCache.height,
					GL_BGRA,
					GL_UNSIGNED_BYTE,
					m_frameCache.pixels.data() );
				m_lastUploadedFrame = m_frameCache.frameNumber;
			}
		}
	}

	if( m_textureID == 0 )
		return FF_SUCCESS;

	ScopedShaderBinding shaderBinding( m_shader.GetGLID() );
	ScopedSamplerActivation sampler0( 0 );
	Scoped2DTextureBinding texBinding( m_textureID );

	// Browser frame buffer is top-left origin.
	// If host uses standard OpenGL BOTTOM_LEFT orientation, flip Y in vertex shader.
	// If host enabled TOP_LEFT texture orientation, do not flip Y.
	int flipY = ( GetTextureOrientation() == TextureOrientation::BOTTOM_LEFT ) ? 1 : 0;
	m_shader.Set( "BrowserTex", 0 );
	m_shader.Set( "FlipY", flipY );

	m_quad.Draw();

	return FF_SUCCESS;
}

FFResult FFGLBrowserSource::DeInitGL()
{
	if( m_browser )
	{
		m_browser->Shutdown();
		m_browser.reset();
	}

	if( m_textureID != 0 )
	{
		glDeleteTextures( 1, &m_textureID );
		m_textureID = 0;
	}
	m_textureWidth  = 0;
	m_textureHeight = 0;

	m_shader.FreeGLResources();
	m_quad.Release();

	return FF_SUCCESS;
}

FFResult FFGLBrowserSource::SetTextParameter( unsigned int index, const char* value )
{
	std::string newVal = value ? value : "";
	switch( index )
	{
	case PT_URL:
		if( m_url != newVal )
		{
			m_url = newVal;
			if( m_browser && !m_url.empty() )
				m_browser->Navigate( m_url );
		}
		return FF_SUCCESS;

	case PT_CUSTOM_CSS:
		if( m_customCss != newVal )
		{
			m_customCss = newVal;
			if( m_browser )
				m_browser->SetCustomCss( m_customCss );
		}
		return FF_SUCCESS;

	default:
		return FF_FAIL;
	}
}

char* FFGLBrowserSource::GetTextParameter( unsigned int index )
{
	switch( index )
	{
	case PT_URL:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%s", m_url.c_str() );
		return m_displayBuffer;
	case PT_CUSTOM_CSS:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%s", m_customCss.c_str() );
		return m_displayBuffer;
	default:
		return nullptr;
	}
}

FFResult FFGLBrowserSource::SetFloatParameter( unsigned int dwIndex, float value )
{
	switch( dwIndex )
	{
	case PT_RELOAD:
		if( value > 0.5f && m_browser )
			m_browser->Reload();
		break;

	case PT_BACK:
		if( value > 0.5f && m_browser )
			m_browser->GoBack();
		break;

	case PT_FORWARD:
		if( value > 0.5f && m_browser )
			m_browser->GoForward();
		break;

	case PT_TRANSPARENT_BG:
	{
		bool newBg = ( value > 0.5f );
		if( m_transparentBg != newBg )
		{
			m_transparentBg = newBg;
			if( m_browser )
				m_browser->SetTransparentBackground( m_transparentBg );
		}
		break;
	}

	case PT_PAGE_ZOOM:
		m_pageZoom = std::max( 0.25f, std::min( 3.0f, value ) );
		if( m_browser )
			m_browser->SetZoom( m_pageZoom );
		break;

	case PT_RES_MODE:
		if( m_resolutionMode != value )
		{
			m_resolutionMode = value;
			UpdateResolutionVisibility( true );
			if( m_browser )
			{
				uint32_t w = 1920, h = 1080;
				ResolveTargetDimensions( w, h );
				m_browser->Resize( w, h );
			}
		}
		break;

	case PT_CUSTOM_WIDTH:
		m_customWidth = std::max( 128.0f, std::min( 4096.0f, std::round( value ) ) );
		if( m_browser && static_cast< int >( std::round( m_resolutionMode ) ) == 4 )
		{
			uint32_t w = 1920, h = 1080;
			ResolveTargetDimensions( w, h );
			m_browser->Resize( w, h );
		}
		break;

	case PT_CUSTOM_HEIGHT:
		m_customHeight = std::max( 128.0f, std::min( 4096.0f, std::round( value ) ) );
		if( m_browser && static_cast< int >( std::round( m_resolutionMode ) ) == 4 )
		{
			uint32_t w = 1920, h = 1080;
			ResolveTargetDimensions( w, h );
			m_browser->Resize( w, h );
		}
		break;

	case PT_TARGET_FPS:
		m_targetFps = std::max( 1.0f, std::min( 120.0f, std::round( value ) ) );
		if( m_browser )
			m_browser->SetTargetFps( static_cast< uint32_t >( m_targetFps ) );
		break;

	case PT_AUDIO_MUTE:
	{
		bool mute = ( value > 0.5f );
		if( m_audioMute != mute )
		{
			m_audioMute = mute;
			if( m_browser )
				m_browser->SetAudioMuted( m_audioMute );
		}
		break;
	}

	case PT_AUDIO_VOLUME:
		m_audioVolume = std::max( 0.0f, std::min( 1.0f, value ) );
		if( m_browser )
			m_browser->SetAudioVolume( m_audioVolume );
		break;

	case PT_INTERACTIVE_WINDOW:
	{
		bool vis = ( value > 0.5f );
		if( m_interactiveWin != vis )
		{
			m_interactiveWin = vis;
			if( m_browser )
				m_browser->SetInteractiveWindowVisible( m_interactiveWin );
		}
		break;
	}

	case PT_MOUSE_X:
		m_mouseX = std::max( 0.0f, std::min( 1.0f, value ) );
		if( m_browser )
			m_browser->SendMouseMove( m_mouseX, m_mouseY );
		break;

	case PT_MOUSE_Y:
		m_mouseY = std::max( 0.0f, std::min( 1.0f, value ) );
		if( m_browser )
			m_browser->SendMouseMove( m_mouseX, m_mouseY );
		break;

	case PT_MOUSE_CLICK:
	{
		bool down = ( value > 0.5f );
		if( m_mouseLeftDown != down )
		{
			m_mouseLeftDown = down;
			if( m_browser )
				m_browser->SendMouseButton( m_mouseX, m_mouseY, 0, m_mouseLeftDown );
		}
		break;
	}

	case PT_MOUSE_RIGHT_CLICK:
		if( value > 0.5f && m_browser )
		{
			m_browser->SendMouseButton( m_mouseX, m_mouseY, 1, true );
			m_browser->SendMouseButton( m_mouseX, m_mouseY, 1, false );
		}
		break;

	case PT_SCROLL_Y:
	{
		float delta = ( value - m_scrollY ) * 1200.0f;
		m_scrollY   = value;
		if( m_browser && std::fabs( delta ) > 0.5f )
			m_browser->SendMouseWheel( m_mouseX, m_mouseY, -delta );
		break;
	}

	default:
		return FF_FAIL;
	}

	return FF_SUCCESS;
}

float FFGLBrowserSource::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_RELOAD:
	case PT_BACK:
	case PT_FORWARD:
	case PT_MOUSE_RIGHT_CLICK:
		return 0.0f;
	case PT_TRANSPARENT_BG:
		return m_transparentBg ? 1.0f : 0.0f;
	case PT_PAGE_ZOOM:
		return m_pageZoom;
	case PT_RES_MODE:
		return m_resolutionMode;
	case PT_CUSTOM_WIDTH:
		return m_customWidth;
	case PT_CUSTOM_HEIGHT:
		return m_customHeight;
	case PT_TARGET_FPS:
		return m_targetFps;
	case PT_AUDIO_MUTE:
		return m_audioMute ? 1.0f : 0.0f;
	case PT_AUDIO_VOLUME:
		return m_audioVolume;
	case PT_INTERACTIVE_WINDOW:
		return m_interactiveWin ? 1.0f : 0.0f;
	case PT_MOUSE_X:
		return m_mouseX;
	case PT_MOUSE_Y:
		return m_mouseY;
	case PT_MOUSE_CLICK:
		return m_mouseLeftDown ? 1.0f : 0.0f;
	case PT_SCROLL_Y:
		return m_scrollY;
	default:
		return 0.0f;
	}
}

char* FFGLBrowserSource::GetParameterDisplay( unsigned int index )
{
	switch( index )
	{
	case PT_URL:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%s", m_url.c_str() );
		return m_displayBuffer;
	case PT_CUSTOM_CSS:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%s", m_customCss.c_str() );
		return m_displayBuffer;
	case PT_PAGE_ZOOM:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%.0f%%", m_pageZoom * 100.0f );
		return m_displayBuffer;
	case PT_CUSTOM_WIDTH:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%d px", static_cast< int >( m_customWidth ) );
		return m_displayBuffer;
	case PT_CUSTOM_HEIGHT:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%d px", static_cast< int >( m_customHeight ) );
		return m_displayBuffer;
	case PT_TARGET_FPS:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%d FPS", static_cast< int >( m_targetFps ) );
		return m_displayBuffer;
	case PT_AUDIO_VOLUME:
		std::snprintf( m_displayBuffer, sizeof( m_displayBuffer ), "%.0f%%", m_audioVolume * 100.0f );
		return m_displayBuffer;
	default:
		return CFFGLPlugin::GetParameterDisplay( index );
	}
}
