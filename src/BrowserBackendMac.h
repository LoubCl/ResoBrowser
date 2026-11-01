#pragma once

#include "IBrowserBackend.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class BrowserBackendMac : public IBrowserBackend
{
public:
	BrowserBackendMac();
	~BrowserBackendMac() override;

	bool Initialize( uint32_t width, uint32_t height, const std::string& initialUrl ) override;
	void Shutdown() override;

	void Navigate( const std::string& url ) override;
	void Reload() override;
	void GoBack() override;
	void GoForward() override;

	void Resize( uint32_t width, uint32_t height ) override;
	void SetTargetFps( uint32_t fps ) override;
	void SetZoom( float zoomFactor ) override;
	void SetTransparentBackground( bool transparent ) override;
	void SetCustomCss( const std::string& css ) override;

	void SetAudioMuted( bool muted ) override;
	void SetAudioVolume( float gain ) override;

	void SetInteractiveWindowVisible( bool visible ) override;
	bool IsInteractiveWindowVisible() const override;
	void SendMouseMove( float normX, float normY ) override;
	void SendMouseButton( float normX, float normY, int button, bool down ) override;
	void SendMouseWheel( float normX, float normY, float deltaY ) override;

	bool CopyLatestFrame( BrowserFrameData& outFrame, uint64_t lastFrameNumber ) override;

	// Called from Objective-C background raster queue
	void OnFrameCaptured( std::vector< uint8_t >& stagingBgra, uint32_t width, uint32_t height );
	void OnInteractiveWindowClosed();

private:
	void RenderFallbackPattern( uint32_t width, uint32_t height );

	struct Impl;
	std::shared_ptr< Impl > m_impl;

	std::atomic< bool > m_interactiveVisible{ false };
	mutable std::mutex m_frameMutex;
	BrowserFrameData m_latestFrame;
};
