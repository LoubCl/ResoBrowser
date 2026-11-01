#pragma once

#include "IBrowserBackend.h"
#include "ffgl/FFGLPluginSDK.h"
#include "ffglex/FFGLScreenQuad.h"
#include "ffglex/FFGLShader.h"

#include <memory>
#include <string>

class FFGLBrowserSource : public CFFGLPlugin
{
public:
	FFGLBrowserSource();
	~FFGLBrowserSource() override;

	// CFFGLPlugin overrides
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;
	unsigned int Resize( const FFGLViewportStruct* vp ) override;

	FFResult SetFloatParameter( unsigned int dwIndex, float value ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

private:
	void ResolveTargetDimensions( uint32_t& outWidth, uint32_t& outHeight ) const;
	void UpdateResolutionVisibility( bool raiseEvent );
	void EnsureTextureAllocated( uint32_t width, uint32_t height );

	std::unique_ptr< IBrowserBackend > m_browser;

	// OpenGL resources
	ffglex::FFGLShader m_shader;
	ffglex::FFGLScreenQuad m_quad;
	GLuint m_textureID       = 0;
	uint32_t m_textureWidth  = 0;
	uint32_t m_textureHeight = 0;
	GLint m_texLocation      = -1;
	GLint m_flipYLocation    = -1;

	// Frame tracking
	BrowserFrameData m_frameCache;
	uint64_t m_lastUploadedFrame = 0;

	// Clip Option Parameters
	std::string m_url       = "https://resolume.com";
	std::string m_customCss = "";

	float m_resolutionMode = 0.0f;// 0=Auto (Match Comp), 1=1080p, 2=720p, 3=4K, 4=Custom
	float m_customWidth    = 1920.0f;
	float m_customHeight   = 1080.0f;
	float m_targetFps      = 60.0f;
	float m_pageZoom       = 1.0f;
	bool m_transparentBg   = true;

	bool m_audioMute       = false;
	float m_audioVolume    = 1.0f;

	bool m_interactiveWin  = false;
	float m_mouseX         = 0.5f;
	float m_mouseY         = 0.5f;
	bool m_mouseLeftDown   = false;
	float m_scrollY        = 0.5f;

	char m_displayBuffer[ 512 ] = { 0 };
};
