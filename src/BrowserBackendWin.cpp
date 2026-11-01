#include "BrowserBackendWin.h"

#include <mmsystem.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT WM_BROWSER_TASK   = WM_USER + 101;
constexpr UINT_PTR TIMER_CAPTURE = 1;

// PW_CLIENTONLY (0x1) | PW_RENDERFULLCONTENT (0x2)
constexpr UINT kPrintWindowFlags = 0x00000001 | 0x00000002;

LPWSTR DuplicateCoTaskMemString( const std::wstring& src )
{
	if( src.empty() )
		return nullptr;
	const size_t byteCount = ( src.size() + 1 ) * sizeof( wchar_t );
	LPWSTR copy            = static_cast< LPWSTR >( CoTaskMemAlloc( byteCount ) );
	if( copy )
		std::memcpy( copy, src.c_str(), byteCount );
	return copy;
}

class WebViewEnvironmentOptions final : public ICoreWebView2EnvironmentOptions
{
public:
	explicit WebViewEnvironmentOptions( std::wstring args ) :
		m_args( std::move( args ) ),
		m_version( L"100.0.1185.39" )
	{
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement( &m_refCount );
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG count = InterlockedDecrement( &m_refCount );
		if( count == 0 )
			delete this;
		return count;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppvObject ) override
	{
		if( !ppvObject )
			return E_POINTER;
		if( riid == IID_IUnknown || riid == __uuidof( ICoreWebView2EnvironmentOptions ) )
		{
			*ppvObject = static_cast< ICoreWebView2EnvironmentOptions* >( this );
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE get_AdditionalBrowserArguments( LPWSTR* value ) override
	{
		if( !value )
			return E_POINTER;
		*value = DuplicateCoTaskMemString( m_args );
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE put_AdditionalBrowserArguments( LPCWSTR value ) override
	{
		m_args = value ? value : L"";
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_Language( LPWSTR* value ) override
	{
		if( !value )
			return E_POINTER;
		*value = DuplicateCoTaskMemString( m_language );
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE put_Language( LPCWSTR value ) override
	{
		m_language = value ? value : L"";
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_TargetCompatibleBrowserVersion( LPWSTR* value ) override
	{
		if( !value )
			return E_POINTER;
		*value = DuplicateCoTaskMemString( m_version );
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE put_TargetCompatibleBrowserVersion( LPCWSTR value ) override
	{
		m_version = value ? value : L"";
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_AllowSingleSignOnUsingOSPrimaryAccount( BOOL* allow ) override
	{
		if( !allow )
			return E_POINTER;
		*allow = m_allowSso;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE put_AllowSingleSignOnUsingOSPrimaryAccount( BOOL allow ) override
	{
		m_allowSso = allow;
		return S_OK;
	}

private:
	volatile LONG m_refCount = 1;
	std::wstring m_args;
	std::wstring m_language;
	std::wstring m_version;
	BOOL m_allowSso = FALSE;
};

template< typename Interface, typename CallbackFunc >
class WebViewCallback final : public Interface
{
public:
	explicit WebViewCallback( CallbackFunc fn ) :
		m_fn( std::move( fn ) )
	{
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement( &m_refCount );
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG count = InterlockedDecrement( &m_refCount );
		if( count == 0 )
			delete this;
		return count;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppvObject ) override
	{
		if( !ppvObject )
			return E_POINTER;
		if( riid == IID_IUnknown || riid == __uuidof( Interface ) )
		{
			*ppvObject = static_cast< Interface* >( this );
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE Invoke( HRESULT result, ICoreWebView2Environment* env ) override
	{
		return m_fn( result, env );
	}

private:
	volatile LONG m_refCount = 1;
	CallbackFunc m_fn;
};

template< typename Interface, typename Arg1, typename Arg2, typename CallbackFunc >
class WebViewCallback2 final : public Interface
{
public:
	explicit WebViewCallback2( CallbackFunc fn ) :
		m_fn( std::move( fn ) )
	{
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement( &m_refCount );
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG count = InterlockedDecrement( &m_refCount );
		if( count == 0 )
			delete this;
		return count;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppvObject ) override
	{
		if( !ppvObject )
			return E_POINTER;
		if( riid == IID_IUnknown || riid == __uuidof( Interface ) )
		{
			*ppvObject = static_cast< Interface* >( this );
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE Invoke( Arg1 a1, Arg2 a2 ) override
	{
		return m_fn( a1, a2 );
	}

private:
	volatile LONG m_refCount = 1;
	CallbackFunc m_fn;
};

template< typename Interface, typename Arg1, typename CallbackFunc >
class WebViewCallback1 final : public Interface
{
public:
	explicit WebViewCallback1( CallbackFunc fn ) :
		m_fn( std::move( fn ) )
	{
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return InterlockedIncrement( &m_refCount );
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG count = InterlockedDecrement( &m_refCount );
		if( count == 0 )
			delete this;
		return count;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppvObject ) override
	{
		if( !ppvObject )
			return E_POINTER;
		if( riid == IID_IUnknown || riid == __uuidof( Interface ) )
		{
			*ppvObject = static_cast< Interface* >( this );
			AddRef();
			return S_OK;
		}
		*ppvObject = nullptr;
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE Invoke( Arg1 a1 ) override
	{
		return m_fn( a1 );
	}

private:
	volatile LONG m_refCount = 1;
	CallbackFunc m_fn;
};

LRESULT CALLBACK HostWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	BrowserBackendWin* backend = reinterpret_cast< BrowserBackendWin* >( GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );
	if( msg == WM_NCCREATE )
	{
		CREATESTRUCTW* cs = reinterpret_cast< CREATESTRUCTW* >( lParam );
		backend           = reinterpret_cast< BrowserBackendWin* >( cs->lpCreateParams );
		SetWindowLongPtrW( hwnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( backend ) );
	}
	if( backend )
		return backend->HandleWindowMessage( hwnd, msg, wParam, lParam );
	return DefWindowProcW( hwnd, msg, wParam, lParam );
}

std::string EscapeJsString( const std::string& input )
{
	std::ostringstream ss;
	for( char c : input )
	{
		switch( c )
		{
		case '\\': ss << "\\\\"; break;
		case '\'': ss << "\\'"; break;
		case '\"': ss << "\\\""; break;
		case '\n': ss << "\\n"; break;
		case '\r': ss << "\\r"; break;
		default: ss << c; break;
		}
	}
	return ss.str();
}
}// namespace

std::unique_ptr< IBrowserBackend > CreateBrowserBackend()
{
	return std::make_unique< BrowserBackendWin >();
}

BrowserBackendWin::BrowserBackendWin() = default;

BrowserBackendWin::~BrowserBackendWin()
{
	Shutdown();
}

std::wstring BrowserBackendWin::Utf8ToWide( const std::string& str )
{
	if( str.empty() )
		return std::wstring();
	int needed = MultiByteToWideChar( CP_UTF8, 0, str.data(), static_cast< int >( str.size() ), nullptr, 0 );
	if( needed <= 0 )
		return std::wstring();
	std::wstring result( needed, L'\0' );
	MultiByteToWideChar( CP_UTF8, 0, str.data(), static_cast< int >( str.size() ), &result[ 0 ], needed );
	return result;
}

std::string BrowserBackendWin::NormalizeUrl( const std::string& rawUrl )
{
	std::string trimmed = rawUrl;
	while( !trimmed.empty() && ( trimmed.front() == ' ' || trimmed.front() == '\t' ) )
		trimmed.erase( trimmed.begin() );
	while( !trimmed.empty() && ( trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n' ) )
		trimmed.pop_back();

	if( trimmed.empty() )
		return "about:blank";

	if( trimmed.find( "://" ) == std::string::npos &&
		trimmed.rfind( "about:", 0 ) != 0 &&
		trimmed.rfind( "data:", 0 ) != 0 &&
		trimmed.rfind( "file:", 0 ) != 0 )
	{
		return "https://" + trimmed;
	}

	// Encode spaces and '#' in data: URLs so WebView2 does not truncate at '#' fragment
	std::string encoded;
	encoded.reserve( trimmed.size() );
	const bool isDataUrl = ( trimmed.rfind( "data:", 0 ) == 0 );
	for( char c : trimmed )
	{
		if( c == ' ' )
			encoded += "%20";
		else if( isDataUrl && c == '#' )
			encoded += "%23";
		else
			encoded += c;
	}
	return encoded;
}

bool BrowserBackendWin::Initialize( uint32_t width, uint32_t height, const std::string& initialUrl )
{
	if( m_running.load() )
		return true;

	m_width      = std::max< uint32_t >( 128, width );
	m_height     = std::max< uint32_t >( 128, height );
	m_currentUrl = NormalizeUrl( initialUrl );
	m_running.store( true );

	RenderFallbackPattern( "Initializing Browser Engine..." );

	m_workerThread = std::thread( &BrowserBackendWin::WorkerThreadMain, this );
	return true;
}

void BrowserBackendWin::Shutdown()
{
	if( !m_running.exchange( false ) )
		return;

	if( m_hwnd )
		PostMessageW( m_hwnd, WM_CLOSE, 0, 0 );

	if( m_workerThread.joinable() )
		m_workerThread.join();
}

void BrowserBackendWin::PostTask( std::function< void() > task )
{
	{
		std::lock_guard< std::mutex > lock( m_taskMutex );
		m_tasks.push( std::move( task ) );
	}
	if( m_hwnd )
		PostMessageW( m_hwnd, WM_BROWSER_TASK, 0, 0 );
}

void BrowserBackendWin::DrainTasks()
{
	std::queue< std::function< void() > > localTasks;
	{
		std::lock_guard< std::mutex > lock( m_taskMutex );
		std::swap( localTasks, m_tasks );
	}
	while( !localTasks.empty() )
	{
		auto& fn = localTasks.front();
		if( fn )
			fn();
		localTasks.pop();
	}
}

void BrowserBackendWin::WorkerThreadMain()
{
	// Request 1 ms system timer resolution so 60+ FPS capture timer never coalesces to 31.25 ms
	timeBeginPeriod( 1 );
	CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
	CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &m_wicFactory ) );

	WNDCLASSEXW wc   = {};
	wc.cbSize        = sizeof( WNDCLASSEXW );
	wc.lpfnWndProc   = HostWndProc;
	wc.hInstance     = GetModuleHandleW( nullptr );
	wc.hCursor       = LoadCursor( nullptr, IDC_ARROW );
	wc.lpszClassName = L"ResolumeFFGLBrowserHostWnd";
	RegisterClassExW( &wc );

	// Start as an offscreen borderless tool window (no WS_EX_LAYERED alpha attenuation)
	// so DWM composes the WebView2 DirectComposition visual tree at full 32-bit BGRA fidelity
	// and PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT) copies it in ~2 ms.
	DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
	DWORD style   = WS_POPUP | WS_VISIBLE;

	m_hwnd = CreateWindowExW(
		exStyle,
		wc.lpszClassName,
		L"Resolume Browser Source",
		style,
		-3200,
		-3200,
		static_cast< int >( m_width ),
		static_cast< int >( m_height ),
		nullptr,
		nullptr,
		wc.hInstance,
		this );

	ShowWindow( m_hwnd, SW_SHOWNOACTIVATE );
	UpdateWindow( m_hwnd );

	// Per-instance user data folder so multiple Browser Source clips in Resolume never collide on lockfiles
	wchar_t localAppData[ MAX_PATH ] = { 0 };
	std::wstring userDataDir         = L"C:\\Temp\\ResolumeBrowserSource";
	if( SUCCEEDED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData ) ) )
	{
		userDataDir = std::wstring( localAppData ) + L"\\ResolumeBrowserSource";
	}
	wchar_t instanceSuffix[ 64 ] = { 0 };
	std::swprintf( instanceSuffix, 64, L"\\Clip_%lu_%p", GetCurrentProcessId(), static_cast< void* >( this ) );
	userDataDir += instanceSuffix;

	// Disable Chromium occlusion & background throttling so offscreen/backgrounded windows render at full 60+ FPS
	auto* envOptions = new WebViewEnvironmentOptions(
		L"--disable-background-timer-throttling "
		L"--disable-renderer-backgrounding "
		L"--disable-backgrounding-occluded-windows "
		L"--disable-features=CalculateNativeWinOcclusion,IntensiveWakeUpThrottling "
		L"--enable-gpu-rasterization "
		L"--autoplay-policy=no-user-gesture-required" );

	auto envHandler = new WebViewCallback<
		ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
		std::function< HRESULT( HRESULT, ICoreWebView2Environment* ) > >(
		[ this ]( HRESULT res, ICoreWebView2Environment* env ) -> HRESULT {
			OnEnvironmentCreated( res, env );
			return S_OK;
		} );

	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
		nullptr,
		userDataDir.c_str(),
		envOptions,
		envHandler );
	envHandler->Release();
	envOptions->Release();

	if( FAILED( hr ) )
	{
		RenderFallbackPattern( "WebView2 Runtime not found. Please install Microsoft Edge WebView2." );
	}

	LARGE_INTEGER qpcFreq, nextCaptureTick;
	QueryPerformanceFrequency( &qpcFreq );
	QueryPerformanceCounter( &nextCaptureTick );

	MSG msg;
	while( m_running.load() )
	{
		while( PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) )
		{
			if( msg.message == WM_QUIT )
			{
				m_running.store( false );
				break;
			}
			TranslateMessage( &msg );
			DispatchMessageW( &msg );
		}
		if( !m_running.load() )
			break;

		DrainTasks();

		LARGE_INTEGER now;
		QueryPerformanceCounter( &now );
		if( now.QuadPart >= nextCaptureTick.QuadPart )
		{
			CaptureFrameOnWorker();

			const uint32_t fps       = std::max< uint32_t >( 1, m_targetFps );
			const LONGLONG stepTicks = qpcFreq.QuadPart / fps;
			nextCaptureTick.QuadPart += stepTicks;
			QueryPerformanceCounter( &now );
			if( nextCaptureTick.QuadPart <= now.QuadPart )
				nextCaptureTick.QuadPart = now.QuadPart;
		}
		else
		{
			LONGLONG remainMs = ( ( nextCaptureTick.QuadPart - now.QuadPart ) * 1000 ) / qpcFreq.QuadPart;
			DWORD waitMs      = ( remainMs > 1 ) ? static_cast< DWORD >( std::min< LONGLONG >( remainMs, 8 ) ) : 1;
			MsgWaitForMultipleObjectsEx( 0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE );
		}
	}

	FreeDibOnWorker();

	if( m_controller )
	{
		m_controller->Close();
		m_controller.Reset();
	}
	m_webview.Reset();
	m_env.Reset();
	m_wicFactory.Reset();

	if( m_hwnd )
	{
		DestroyWindow( m_hwnd );
		m_hwnd = nullptr;
	}

	CoUninitialize();
	timeEndPeriod( 1 );
}

bool BrowserBackendWin::EnsureDibAllocatedOnWorker( uint32_t width, uint32_t height )
{
	if( width == 0 || height == 0 )
		return false;

	if( m_memDC && m_dibSection && m_dibBits && m_dibWidth == width && m_dibHeight == height )
		return true;

	FreeDibOnWorker();

	HDC hdcScreen = GetDC( nullptr );
	m_memDC       = CreateCompatibleDC( hdcScreen );
	ReleaseDC( nullptr, hdcScreen );
	if( !m_memDC )
		return false;

	BITMAPINFO bmi              = {};
	bmi.bmiHeader.biSize        = sizeof( BITMAPINFOHEADER );
	bmi.bmiHeader.biWidth       = static_cast< LONG >( width );
	bmi.bmiHeader.biHeight      = -static_cast< LONG >( height );// Top-down BGRA
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	void* bits   = nullptr;
	m_dibSection = CreateDIBSection( m_memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0 );
	if( !m_dibSection || !bits )
	{
		DeleteDC( m_memDC );
		m_memDC      = nullptr;
		m_dibSection = nullptr;
		return false;
	}

	m_oldBitmap = SelectObject( m_memDC, m_dibSection );
	m_dibBits   = static_cast< uint8_t* >( bits );
	m_dibWidth  = width;
	m_dibHeight = height;
	return true;
}

void BrowserBackendWin::FreeDibOnWorker()
{
	if( m_memDC && m_oldBitmap )
	{
		SelectObject( m_memDC, m_oldBitmap );
		m_oldBitmap = nullptr;
	}
	if( m_dibSection )
	{
		DeleteObject( m_dibSection );
		m_dibSection = nullptr;
	}
	if( m_memDC )
	{
		DeleteDC( m_memDC );
		m_memDC = nullptr;
	}
	m_dibBits   = nullptr;
	m_dibWidth  = 0;
	m_dibHeight = 0;
}

void BrowserBackendWin::CaptureFrameOnWorker()
{
	if( !m_ready.load() || !m_navigationDone.load() || !m_hwnd )
		return;

	RECT rc = {};
	GetClientRect( m_hwnd, &rc );
	const uint32_t w = ( rc.right > rc.left ) ? static_cast< uint32_t >( rc.right - rc.left ) : m_width;
	const uint32_t h = ( rc.bottom > rc.top ) ? static_cast< uint32_t >( rc.bottom - rc.top ) : m_height;
	if( w == 0 || h == 0 )
		return;

	if( EnsureDibAllocatedOnWorker( w, h ) )
	{
		// Copy the DirectComposition GPU visual tree directly into m_dibBits (~2-4 ms)
		if( PrintWindow( m_hwnd, m_memDC, kPrintWindowFlags ) )
		{
			const size_t byteCount = static_cast< size_t >( w ) * h * 4;
			m_stagingPixels.resize( byteCount );
			std::memcpy( m_stagingPixels.data(), m_dibBits, byteCount );

			// Lock mutex only for an O(1) pointer swap so Resolume's OpenGL thread never stalls
			{
				std::lock_guard< std::mutex > lock( m_frameMutex );
				m_latestFrame.width  = w;
				m_latestFrame.height = h;
				std::swap( m_latestFrame.pixels, m_stagingPixels );
				m_latestFrame.frameNumber++;
			}
			return;
		}
	}

	// Fallback if PrintWindow is ever unsupported
	CaptureFrameFallbackOnWorker();
}

void BrowserBackendWin::OnEnvironmentCreated( HRESULT result, ICoreWebView2Environment* env )
{
	if( FAILED( result ) || !env )
	{
		RenderFallbackPattern( "Failed to initialize WebView2 Environment." );
		return;
	}

	m_env = env;

	auto ctrlHandler = new WebViewCallback2<
		ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
		HRESULT,
		ICoreWebView2Controller*,
		std::function< HRESULT( HRESULT, ICoreWebView2Controller* ) > >(
		[ this ]( HRESULT res, ICoreWebView2Controller* controller ) -> HRESULT {
			OnControllerCreated( res, controller );
			return S_OK;
		} );

	m_env->CreateCoreWebView2Controller( m_hwnd, ctrlHandler );
	ctrlHandler->Release();
}

void BrowserBackendWin::OnControllerCreated( HRESULT result, ICoreWebView2Controller* controller )
{
	if( FAILED( result ) || !controller )
	{
		RenderFallbackPattern( "Failed to create WebView2 Controller." );
		return;
	}

	m_controller = controller;

	// Drain any parameter updates queued by the host before the controller finished creating
	// so m_currentUrl, m_width, m_height, m_transparentBg, etc. reflect the clip's saved state
	DrainTasks();

	m_controller->get_CoreWebView2( &m_webview );

	RECT bounds = { 0, 0, static_cast< LONG >( m_width ), static_cast< LONG >( m_height ) };
	m_controller->put_Bounds( bounds );
	m_controller->put_IsVisible( TRUE );
	m_controller->put_ZoomFactor( static_cast< double >( m_zoomFactor ) );

	// Configure background transparency via ICoreWebView2Controller2
	ComPtr< ICoreWebView2Controller2 > ctrl2;
	if( SUCCEEDED( m_controller.As( &ctrl2 ) ) )
	{
		COREWEBVIEW2_COLOR bg = m_transparentBg ? COREWEBVIEW2_COLOR{ 0, 0, 0, 0 } : COREWEBVIEW2_COLOR{ 255, 0, 0, 0 };
		ctrl2->put_DefaultBackgroundColor( bg );
	}

	if( m_webview )
	{
		// Configure audio mute via ICoreWebView2_8
		ComPtr< ICoreWebView2_8 > wv8;
		if( SUCCEEDED( m_webview.As( &wv8 ) ) )
		{
			wv8->put_IsMuted( m_audioMuted ? TRUE : FALSE );
		}

		// Keep popups inside the same browser view so links with target="_blank" work seamlessly
		EventRegistrationToken newWinToken;
		auto newWinHandler = new WebViewCallback2<
			ICoreWebView2NewWindowRequestedEventHandler,
			ICoreWebView2*,
			ICoreWebView2NewWindowRequestedEventArgs*,
			std::function< HRESULT( ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* ) > >(
			[ this ]( ICoreWebView2* /*sender*/, ICoreWebView2NewWindowRequestedEventArgs* args ) -> HRESULT {
				LPWSTR uri = nullptr;
				if( args && SUCCEEDED( args->get_Uri( &uri ) ) && uri )
				{
					args->put_Handled( TRUE );
					m_webview->Navigate( uri );
					CoTaskMemFree( uri );
				}
				return S_OK;
			} );
		m_webview->add_NewWindowRequested( newWinHandler, &newWinToken );
		newWinHandler->Release();

		// Hook NavigationCompleted to apply custom CSS & volume settings
		EventRegistrationToken navToken;
		auto navHandler = new WebViewCallback2<
			ICoreWebView2NavigationCompletedEventHandler,
			ICoreWebView2*,
			ICoreWebView2NavigationCompletedEventArgs*,
			std::function< HRESULT( ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* ) > >(
			[ this ]( ICoreWebView2* /*sender*/, ICoreWebView2NavigationCompletedEventArgs* /*args*/ ) -> HRESULT {
				OnNavigationCompleted();
				return S_OK;
			} );
		m_webview->add_NavigationCompleted( navHandler, &navToken );
		navHandler->Release();

		std::wstring wurl = Utf8ToWide( m_currentUrl );
		m_webview->Navigate( wurl.c_str() );
	}

	m_ready.store( true );
}

void BrowserBackendWin::OnNavigationCompleted()
{
	m_navigationDone.store( true );
	ApplyCustomCssAndVolumeOnWorker();
	CaptureFrameOnWorker();
}

void BrowserBackendWin::ApplyCustomCssAndVolumeOnWorker()
{
	if( !m_webview )
		return;

	std::ostringstream js;
	js << "(function(){";
	// Inject or update custom CSS style element
	js << "let el = document.getElementById('__resolume_custom_css');";
	js << "if(!el){ el = document.createElement('style'); el.id = '__resolume_custom_css'; if(document.head) document.head.appendChild(el); }";
	js << "if(el){ el.textContent = '" << EscapeJsString( m_customCss ) << "'; }";

	// Apply HTML5 media volume scaling
	js << "const vol = " << m_audioVolume << ";";
	js << "document.querySelectorAll('video, audio').forEach(m => { try { m.volume = vol; } catch(e){} });";
	js << "})();";

	std::wstring wjs = Utf8ToWide( js.str() );
	m_webview->ExecuteScript( wjs.c_str(), nullptr );
}

void BrowserBackendWin::CaptureFrameFallbackOnWorker()
{
	if( !m_ready.load() || !m_webview || !m_wicFactory || m_captureInFlight )
		return;

	ComPtr< IStream > stream;
	if( FAILED( CreateStreamOnHGlobal( nullptr, TRUE, &stream ) ) || !stream )
		return;

	m_captureInFlight = true;

	auto captureHandler = new WebViewCallback1<
		ICoreWebView2CapturePreviewCompletedHandler,
		HRESULT,
		std::function< HRESULT( HRESULT ) > >(
		[ this, stream ]( HRESULT errCode ) -> HRESULT {
			m_captureInFlight = false;
			if( FAILED( errCode ) || !m_wicFactory )
				return S_OK;

			LARGE_INTEGER zero = {};
			stream->Seek( zero, STREAM_SEEK_SET, nullptr );

			ComPtr< IWICBitmapDecoder > decoder;
			if( FAILED( m_wicFactory->CreateDecoderFromStream( stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder ) ) )
				return S_OK;

			ComPtr< IWICBitmapFrameDecode > frame;
			if( FAILED( decoder->GetFrame( 0, &frame ) ) )
				return S_OK;

			UINT w = 0, h = 0;
			frame->GetSize( &w, &h );
			if( w == 0 || h == 0 )
				return S_OK;

			ComPtr< IWICFormatConverter > converter;
			if( FAILED( m_wicFactory->CreateFormatConverter( &converter ) ) )
				return S_OK;

			if( FAILED( converter->Initialize(
					frame.Get(),
					GUID_WICPixelFormat32bppBGRA,
					WICBitmapDitherTypeNone,
					nullptr,
					0.0,
					WICBitmapPaletteTypeCustom ) ) )
			{
				return S_OK;
			}

			const UINT stride      = w * 4;
			const size_t byteCount = static_cast< size_t >( stride ) * h;

			// Decode PNG outside m_frameMutex so we never stall Resolume's OpenGL render thread
			m_stagingPixels.resize( byteCount );
			if( SUCCEEDED( converter->CopyPixels( nullptr, stride, static_cast< UINT >( byteCount ), m_stagingPixels.data() ) ) )
			{
				std::lock_guard< std::mutex > lock( m_frameMutex );
				m_latestFrame.width  = w;
				m_latestFrame.height = h;
				std::swap( m_latestFrame.pixels, m_stagingPixels );
				m_latestFrame.frameNumber++;
			}
			return S_OK;
		} );

	HRESULT hr = m_webview->CapturePreview( COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream.Get(), captureHandler );
	captureHandler->Release();
	if( FAILED( hr ) )
		m_captureInFlight = false;
}

void BrowserBackendWin::RenderFallbackPattern( const std::string& /*statusText*/ )
{
	const uint32_t w       = std::max< uint32_t >( 128, m_width );
	const uint32_t h       = std::max< uint32_t >( 128, m_height );
	const size_t byteCount = static_cast< size_t >( w ) * h * 4;

	std::lock_guard< std::mutex > lock( m_frameMutex );
	m_latestFrame.width  = w;
	m_latestFrame.height = h;
	m_latestFrame.pixels.resize( byteCount );

	// Subtle dark slate gradient while initializing
	for( uint32_t y = 0; y < h; ++y )
	{
		uint8_t* row = m_latestFrame.pixels.data() + static_cast< size_t >( y ) * w * 4;
		uint8_t v    = static_cast< uint8_t >( 24 + ( y * 24 ) / h );
		for( uint32_t x = 0; x < w; ++x )
		{
			row[ x * 4 + 0 ] = v + 12;// B
			row[ x * 4 + 1 ] = v;     // G
			row[ x * 4 + 2 ] = v;     // R
			row[ x * 4 + 3 ] = 255;   // A
		}
	}
	m_latestFrame.frameNumber++;
}

void BrowserBackendWin::UpdateWindowPlacementOnWorker()
{
	if( !m_hwnd )
		return;

	if( m_interactiveVisible.load() )
	{
		// Convert to a standard interactive titled window on screen so user can log in / interact
		SetWindowLongPtrW( m_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE );
		SetWindowLongPtrW( m_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW );

		RECT rc = { 0, 0, static_cast< LONG >( m_width ), static_cast< LONG >( m_height ) };
		AdjustWindowRectEx( &rc, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW );
		int winW = std::min< int >( 1600, rc.right - rc.left );
		int winH = std::min< int >( 960, rc.bottom - rc.top );

		SetWindowPos( m_hwnd, HWND_TOP, 100, 100, winW, winH, SWP_FRAMECHANGED | SWP_SHOWWINDOW );
		SetForegroundWindow( m_hwnd );
	}
	else
	{
		// Return to offscreen tool window at exact target resolution (no WS_EX_LAYERED alpha attenuation)
		SetWindowLongPtrW( m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE );
		SetWindowLongPtrW( m_hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE );
		SetWindowPos(
			m_hwnd,
			HWND_BOTTOM,
			-3200,
			-3200,
			static_cast< int >( m_width ),
			static_cast< int >( m_height ),
			SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW );
	}

	if( m_controller )
	{
		RECT clientRect = {};
		GetClientRect( m_hwnd, &clientRect );
		if( clientRect.right > clientRect.left && clientRect.bottom > clientRect.top )
		{
			m_controller->put_Bounds( clientRect );
			m_controller->put_IsVisible( TRUE );
		}
	}
}

LRESULT BrowserBackendWin::HandleWindowMessage( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	switch( msg )
	{
	case WM_BROWSER_TASK:
		DrainTasks();
		return 0;

	case WM_TIMER:
		if( wParam == TIMER_CAPTURE )
		{
			DrainTasks();
			CaptureFrameOnWorker();
		}
		return 0;

	case WM_SIZE:
		if( m_controller && m_interactiveVisible.load() )
		{
			RECT clientRect = {};
			GetClientRect( hwnd, &clientRect );
			if( clientRect.right > clientRect.left && clientRect.bottom > clientRect.top )
			{
				m_controller->put_Bounds( clientRect );
			}
		}
		return 0;

	case WM_CLOSE:
		if( m_running.load() && m_interactiveVisible.load() )
		{
			// If user clicks 'X' on the interactive pop-out window, hide it instead of killing the browser
			m_interactiveVisible.store( false );
			UpdateWindowPlacementOnWorker();
			return 0;
		}
		PostQuitMessage( 0 );
		return 0;

	default:
		break;
	}
	return DefWindowProcW( hwnd, msg, wParam, lParam );
}

void BrowserBackendWin::Navigate( const std::string& url )
{
	std::string normalized = NormalizeUrl( url );
	PostTask( [ this, normalized ]() {
		m_currentUrl = normalized;
		if( m_webview )
		{
			std::wstring wurl = Utf8ToWide( m_currentUrl );
			m_webview->Navigate( wurl.c_str() );
		}
	} );
}

void BrowserBackendWin::Reload()
{
	PostTask( [ this ]() {
		if( m_webview )
			m_webview->Reload();
	} );
}

void BrowserBackendWin::GoBack()
{
	PostTask( [ this ]() {
		if( m_webview )
		{
			BOOL canGoBack = FALSE;
			m_webview->get_CanGoBack( &canGoBack );
			if( canGoBack )
				m_webview->GoBack();
		}
	} );
}

void BrowserBackendWin::GoForward()
{
	PostTask( [ this ]() {
		if( m_webview )
		{
			BOOL canGoFwd = FALSE;
			m_webview->get_CanGoForward( &canGoFwd );
			if( canGoFwd )
				m_webview->GoForward();
		}
	} );
}

void BrowserBackendWin::Resize( uint32_t width, uint32_t height )
{
	uint32_t w = std::max< uint32_t >( 128, std::min< uint32_t >( 4096, width ) );
	uint32_t h = std::max< uint32_t >( 128, std::min< uint32_t >( 4096, height ) );

	PostTask( [ this, w, h ]() {
		if( m_width == w && m_height == h )
			return;
		m_width  = w;
		m_height = h;
		if( !m_interactiveVisible.load() && m_hwnd )
		{
			SetWindowPos(
				m_hwnd,
				nullptr,
				-3200,
				-3200,
				static_cast< int >( m_width ),
				static_cast< int >( m_height ),
				SWP_NOZORDER | SWP_NOACTIVATE );
		}
		if( m_controller )
		{
			RECT bounds = { 0, 0, static_cast< LONG >( m_width ), static_cast< LONG >( m_height ) };
			m_controller->put_Bounds( bounds );
		}
	} );
}

void BrowserBackendWin::SetTargetFps( uint32_t fps )
{
	uint32_t clamped = std::max< uint32_t >( 1, std::min< uint32_t >( 120, fps ) );
	PostTask( [ this, clamped ]() {
		m_targetFps = clamped;
		if( m_hwnd )
		{
			UINT intervalMs = std::max< UINT >( 4, 1000 / m_targetFps );
			SetTimer( m_hwnd, TIMER_CAPTURE, intervalMs, nullptr );
		}
	} );
}

void BrowserBackendWin::SetZoom( float zoomFactor )
{
	PostTask( [ this, zoomFactor ]() {
		m_zoomFactor = zoomFactor;
		if( m_controller )
			m_controller->put_ZoomFactor( static_cast< double >( m_zoomFactor ) );
	} );
}

void BrowserBackendWin::SetTransparentBackground( bool transparent )
{
	PostTask( [ this, transparent ]() {
		m_transparentBg = transparent;
		if( m_controller )
		{
			ComPtr< ICoreWebView2Controller2 > ctrl2;
			if( SUCCEEDED( m_controller.As( &ctrl2 ) ) )
			{
				COREWEBVIEW2_COLOR bg = m_transparentBg ? COREWEBVIEW2_COLOR{ 0, 0, 0, 0 } : COREWEBVIEW2_COLOR{ 255, 0, 0, 0 };
				ctrl2->put_DefaultBackgroundColor( bg );
			}
		}
	} );
}

void BrowserBackendWin::SetCustomCss( const std::string& css )
{
	PostTask( [ this, css ]() {
		m_customCss = css;
		ApplyCustomCssAndVolumeOnWorker();
	} );
}

void BrowserBackendWin::SetAudioMuted( bool muted )
{
	PostTask( [ this, muted ]() {
		m_audioMuted = muted;
		if( m_webview )
		{
			ComPtr< ICoreWebView2_8 > wv8;
			if( SUCCEEDED( m_webview.As( &wv8 ) ) )
				wv8->put_IsMuted( m_audioMuted ? TRUE : FALSE );
		}
	} );
}

void BrowserBackendWin::SetAudioVolume( float gain )
{
	PostTask( [ this, gain ]() {
		m_audioVolume = gain;
		ApplyCustomCssAndVolumeOnWorker();
	} );
}

void BrowserBackendWin::SetInteractiveWindowVisible( bool visible )
{
	m_interactiveVisible.store( visible );
	PostTask( [ this ]() {
		UpdateWindowPlacementOnWorker();
	} );
}

bool BrowserBackendWin::IsInteractiveWindowVisible() const
{
	return m_interactiveVisible.load();
}

void BrowserBackendWin::SendMouseMove( float normX, float normY )
{
	PostTask( [ this, normX, normY ]() {
		if( !m_webview )
			return;
		int px = static_cast< int >( normX * m_width );
		int py = static_cast< int >( normY * m_height );

		std::ostringstream params;
		params << "{\"type\":\"mouseMoved\",\"x\":" << px << ",\"y\":" << py << "}";
		std::wstring wparams = Utf8ToWide( params.str() );
		m_webview->CallDevToolsProtocolMethod( L"Input.dispatchMouseEvent", wparams.c_str(), nullptr );
	} );
}

void BrowserBackendWin::SendMouseButton( float normX, float normY, int button, bool down )
{
	PostTask( [ this, normX, normY, button, down ]() {
		if( !m_webview )
			return;
		int px                 = static_cast< int >( normX * m_width );
		int py                 = static_cast< int >( normY * m_height );
		const char* typeStr    = down ? "mousePressed" : "mouseReleased";
		const char* buttonName = ( button == 1 ) ? "right" : "left";

		std::ostringstream params;
		params << "{\"type\":\"" << typeStr << "\",\"x\":" << px << ",\"y\":" << py
			   << ",\"button\":\"" << buttonName << "\",\"clickCount\":1}";
		std::wstring wparams = Utf8ToWide( params.str() );
		m_webview->CallDevToolsProtocolMethod( L"Input.dispatchMouseEvent", wparams.c_str(), nullptr );
	} );
}

void BrowserBackendWin::SendMouseWheel( float normX, float normY, float deltaY )
{
	PostTask( [ this, normX, normY, deltaY ]() {
		if( !m_webview )
			return;
		int px = static_cast< int >( normX * m_width );
		int py = static_cast< int >( normY * m_height );

		std::ostringstream params;
		params << "{\"type\":\"mouseWheel\",\"x\":" << px << ",\"y\":" << py
			   << ",\"deltaX\":0,\"deltaY\":" << static_cast< int >( deltaY ) << "}";
		std::wstring wparams = Utf8ToWide( params.str() );
		m_webview->CallDevToolsProtocolMethod( L"Input.dispatchMouseEvent", wparams.c_str(), nullptr );
	} );
}

bool BrowserBackendWin::CopyLatestFrame( BrowserFrameData& outFrame, uint64_t lastFrameNumber )
{
	std::lock_guard< std::mutex > lock( m_frameMutex );
	if( m_latestFrame.frameNumber == 0 || m_latestFrame.frameNumber <= lastFrameNumber || m_latestFrame.pixels.empty() )
		return false;

	outFrame.width       = m_latestFrame.width;
	outFrame.height      = m_latestFrame.height;
	outFrame.frameNumber = m_latestFrame.frameNumber;
	std::swap( outFrame.pixels, m_latestFrame.pixels );
	return true;
}
