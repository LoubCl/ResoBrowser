#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct BrowserFrameData
{
	std::vector< uint8_t > pixels;// BGRA8888 format, top-left origin
	uint32_t width       = 0;
	uint32_t height      = 0;
	uint64_t frameNumber = 0;
};

class IBrowserBackend
{
public:
	virtual ~IBrowserBackend() = default;

	// Initialize browser engine with initial URL and dimensions
	virtual bool Initialize( uint32_t width, uint32_t height, const std::string& initialUrl ) = 0;

	// Shut down browser engine and release resources
	virtual void Shutdown() = 0;

	// Navigation controls
	virtual void Navigate( const std::string& url ) = 0;
	virtual void Reload()                           = 0;
	virtual void GoBack()                           = 0;
	virtual void GoForward()                        = 0;

	// Viewport / rendering settings
	virtual void Resize( uint32_t width, uint32_t height ) = 0;
	virtual void SetTargetFps( uint32_t fps )              = 0;
	virtual void SetZoom( float zoomFactor )               = 0;
	virtual void SetTransparentBackground( bool transparent ) = 0;
	virtual void SetCustomCss( const std::string& css )    = 0;

	// Audio controls (system audio playback + mute + volume)
	virtual void SetAudioMuted( bool muted )  = 0;
	virtual void SetAudioVolume( float gain ) = 0;// 0.0 to 1.0

	// Interactive window & parameter-driven mouse/scroll input
	virtual void SetInteractiveWindowVisible( bool visible )                        = 0;
	virtual bool IsInteractiveWindowVisible() const                                 = 0;
	virtual void SendMouseMove( float normX, float normY )                          = 0;// 0..1 normalized
	virtual void SendMouseButton( float normX, float normY, int button, bool down ) = 0;// button: 0=left, 1=right
	virtual void SendMouseWheel( float normX, float normY, float deltaY )           = 0;

	// Copy latest rendered frame if newer than lastFrameNumber. Returns true if updated.
	virtual bool CopyLatestFrame( BrowserFrameData& outFrame, uint64_t lastFrameNumber ) = 0;
};

// Factory function implemented per-platform (BrowserBackendWin.cpp / BrowserBackendMac.mm)
std::unique_ptr< IBrowserBackend > CreateBrowserBackend();
