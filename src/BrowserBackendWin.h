#pragma once

#include "IBrowserBackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "WebView2.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class BrowserBackendWin : public IBrowserBackend
{
public:
	BrowserBackendWin();
	~BrowserBackendWin() override;

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

	// Internal window procedure handler
	LRESULT HandleWindowMessage( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam );

	// Invoked by WebView2 async callbacks on the worker STA thread
	void OnEnvironmentCreated( HRESULT result, ICoreWebView2Environment* env );
	void OnControllerCreated( HRESULT result, ICoreWebView2Controller* controller );
	void OnNavigationCompleted();

private:
	void WorkerThreadMain();
	void PostTask( std::function< void() > task );
	void DrainTasks();
	bool EnsureDibAllocatedOnWorker( uint32_t width, uint32_t height );
	void FreeDibOnWorker();
	void CaptureFrameOnWorker();
	void CaptureFrameFallbackOnWorker();
	void UpdateWindowPlacementOnWorker();
	void ApplyCustomCssAndVolumeOnWorker();
	void RenderFallbackPattern( const std::string& statusText );
	static std::wstring Utf8ToWide( const std::string& str );
	static std::string NormalizeUrl( const std::string& rawUrl );

	std::thread m_workerThread;
	std::atomic< bool > m_running{ false };
	std::atomic< bool > m_ready{ false };
	std::atomic< bool > m_navigationDone{ false };
	std::atomic< bool > m_interactiveVisible{ false };

	HWND m_hwnd = nullptr;

	// Persistent GDI DIBSection for ultra-fast (<2 ms) DWM PrintWindow(PW_RENDERFULLCONTENT) capture
	HDC m_memDC          = nullptr;
	HBITMAP m_dibSection = nullptr;
	HGDIOBJ m_oldBitmap  = nullptr;
	uint8_t* m_dibBits   = nullptr;
	uint32_t m_dibWidth  = 0;
	uint32_t m_dibHeight = 0;

	// Fallback WIC state if PrintWindow is unavailable
	bool m_captureInFlight = false;
	Microsoft::WRL::ComPtr< IWICImagingFactory > m_wicFactory;

	// WebView2 COM pointers (accessed only on WorkerThreadMain)
	Microsoft::WRL::ComPtr< ICoreWebView2Environment > m_env;
	Microsoft::WRL::ComPtr< ICoreWebView2Controller > m_controller;
	Microsoft::WRL::ComPtr< ICoreWebView2 > m_webview;

	// Configuration state (on worker thread)
	uint32_t m_width         = 1920;
	uint32_t m_height        = 1080;
	uint32_t m_targetFps     = 60;
	float m_zoomFactor       = 1.0f;
	bool m_transparentBg     = true;
	bool m_audioMuted        = false;
	float m_audioVolume      = 1.0f;
	std::string m_currentUrl = "https://resolume.com";
	std::string m_customCss;

	// Task queue for cross-thread commands
	std::mutex m_taskMutex;
	std::queue< std::function< void() > > m_tasks;

	// Triple-buffered pixel storage (staging buffer on worker thread + latest frame under mutex + GL frame cache)
	std::vector< uint8_t > m_stagingPixels;
	mutable std::mutex m_frameMutex;
	BrowserFrameData m_latestFrame;
};
