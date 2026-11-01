#include "BrowserBackendMac.h"

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace
{
std::string NormalizeUrl( const std::string& rawUrl )
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

@interface ResolumeBrowserHelper : NSObject <WKNavigationDelegate, WKUIDelegate, NSWindowDelegate>
{
@public
	BrowserBackendMac* cppBackend;
	NSWindow* hostWindow;
	WKWebView* webView;
	NSTimer* captureTimer;
	id<NSObject> activityToken;
	dispatch_queue_t rasterQueue;
	std::vector< uint8_t > stagingBgra;
	BOOL snapshotInFlight;
	BOOL navigationReady;
	uint32_t targetWidth;
	uint32_t targetHeight;
	uint32_t targetFps;
	float zoomFactor;
	BOOL transparentBg;
	BOOL audioMuted;
	float audioVolume;
	NSString* customCss;
}
- (instancetype)initWithBackend:(BrowserBackendMac*)backend
						  width:(uint32_t)w
						 height:(uint32_t)h
							url:(NSString*)url;
- (void)shutdown;
- (void)restartTimer;
- (void)captureTick:(NSTimer*)timer;
- (void)applyJsSettings;
- (void)updateWindowVisibility:(BOOL)visible;
@end

@implementation ResolumeBrowserHelper

- (instancetype)initWithBackend:(BrowserBackendMac*)backend
						  width:(uint32_t)w
						 height:(uint32_t)h
							url:(NSString*)url
{
	self = [super init];
	if( self )
	{
		cppBackend       = backend;
		targetWidth      = w;
		targetHeight     = h;
		targetFps        = 60;
		zoomFactor       = 1.0f;
		transparentBg    = YES;
		audioMuted       = NO;
		audioVolume      = 1.0f;
		snapshotInFlight = NO;
		navigationReady  = NO;
		customCss        = [@"" retain];

		dispatch_queue_attr_t qosAttr = dispatch_queue_attr_make_with_qos_class(
			DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0 );
		rasterQueue = dispatch_queue_create( "com.resolume.ffgl.browsersource.raster", qosAttr );

		// Prevent macOS App Nap from throttling WKWebView timers, WebGL, or video playback
		activityToken = [[[NSProcessInfo processInfo]
			beginActivityWithOptions:( NSActivityUserInitiated | NSActivityLatencyCritical )
							  reason:@"Resolume FFGL 60FPS Live Browser Source"] retain];

		// Keep window on-screen with micro-alpha (0.005) and click-through (ignoresMouseEvents=YES)
		// so macOS WindowServer keeps NSWindowOcclusionStateVisible active and WKWebView renders at full 60+ FPS.
		NSRect frame = NSMakeRect( 0, 0, w, h );
		hostWindow   = [[NSWindow alloc]
			initWithContentRect:frame
					  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
						backing:NSBackingStoreBuffered
						  defer:NO];
		[hostWindow setTitle:@"Resolume Browser Source"];
		[hostWindow setReleasedWhenClosed:NO];
		[hostWindow setDelegate:self];
		[hostWindow setOpaque:NO];
		[hostWindow setBackgroundColor:[NSColor clearColor]];
		[hostWindow setHasShadow:NO];
		[hostWindow setIgnoresMouseEvents:YES];
		[hostWindow setAlphaValue:0.005];
		[hostWindow setCollectionBehavior:( NSWindowCollectionBehaviorCanJoinAllSpaces |
											NSWindowCollectionBehaviorStationary |
											NSWindowCollectionBehaviorIgnoresCycle )];

		WKWebViewConfiguration* config = [[[WKWebViewConfiguration alloc] init] autorelease];
		if( [config respondsToSelector:@selector( setMediaTypesRequiringUserActionForPlayback: )] )
		{
			config.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
		}

		webView                    = [[WKWebView alloc] initWithFrame:NSMakeRect( 0, 0, w, h ) configuration:config];
		webView.navigationDelegate = self;
		webView.UIDelegate         = self;
		webView.autoresizingMask   = NSViewWidthSizable | NSViewHeightSizable;

		// Enable transparent background on macOS WKWebView
		[webView setValue:@( NO ) forKey:@"drawsBackground"];

		[hostWindow setContentView:webView];
		[hostWindow orderBack:nil];

		NSURL* nsUrl = [NSURL URLWithString:url];
		if( nsUrl )
		{
			NSURLRequest* req = [NSURLRequest requestWithURL:nsUrl];
			[webView loadRequest:req];
		}

		[self restartTimer];
	}
	return self;
}

- (void)shutdown
{
	cppBackend = nullptr;
	if( captureTimer )
	{
		[captureTimer invalidate];
		captureTimer = nil;
	}
	if( activityToken )
	{
		[[NSProcessInfo processInfo] endActivity:activityToken];
		[activityToken release];
		activityToken = nil;
	}
	if( webView )
	{
		[webView stopLoading];
		webView.navigationDelegate = nil;
		webView.UIDelegate         = nil;
		[webView removeFromSuperview];
		[webView release];
		webView = nil;
	}
	if( hostWindow )
	{
		[hostWindow setDelegate:nil];
		[hostWindow close];
		[hostWindow release];
		hostWindow = nil;
	}
	if( customCss )
	{
		[customCss release];
		customCss = nil;
	}
	if( rasterQueue )
	{
		dispatch_release( rasterQueue );
		rasterQueue = nullptr;
	}
}

- (void)restartTimer
{
	if( captureTimer )
	{
		[captureTimer invalidate];
		captureTimer = nil;
	}
	NSTimeInterval interval = 1.0 / (double)std::max< uint32_t >( 1, targetFps );
	captureTimer            = [NSTimer timerWithTimeInterval:interval
                                           target:self
                                         selector:@selector( captureTick: )
                                         userInfo:nil
                                          repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:captureTimer forMode:NSRunLoopCommonModes];
}

- (void)captureTick:(NSTimer*)timer
{
	if( !cppBackend || !webView || !navigationReady || snapshotInFlight )
		return;

	const uint32_t w = targetWidth;
	const uint32_t h = targetHeight;
	if( w == 0 || h == 0 )
		return;

	snapshotInFlight                  = YES;
	WKSnapshotConfiguration* snapConf = [[[WKSnapshotConfiguration alloc] init] autorelease];
	snapConf.snapshotWidth            = @( w );
	if( [snapConf respondsToSelector:@selector( setAfterScreenUpdates: )] )
	{
		snapConf.afterScreenUpdates = NO;
	}

	[webView takeSnapshotWithConfiguration:snapConf
						 completionHandler:^( NSImage* image, NSError* error ) {
							 if( error || !image || !self->cppBackend || !self->rasterQueue )
							 {
								 self->snapshotInFlight = NO;
								 return;
							 }

							 NSRect proposedRect = NSMakeRect( 0, 0, w, h );
							 CGImageRef cgImage  = [image CGImageForProposedRect:&proposedRect context:nil hints:nil];
							 if( !cgImage )
							 {
								 self->snapshotInFlight = NO;
								 return;
							 }

							 CGImageRetain( cgImage );
							 // Offload CGImage -> 32-bit BGRA rasterization to a high-priority background queue
							 // so the macOS main thread and Resolume UI thread are never stalled.
							 dispatch_async( self->rasterQueue, ^{
								 if( self->cppBackend )
								 {
									 const size_t byteCount = static_cast< size_t >( w ) * h * 4;
									 self->stagingBgra.resize( byteCount );
									 if( self->transparentBg )
										 std::memset( self->stagingBgra.data(), 0, byteCount );

									 CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
									 CGContextRef ctx           = CGBitmapContextCreate(
                                         self->stagingBgra.data(),
                                         w,
                                         h,
                                         8,
                                         w * 4,
                                         colorSpace,
                                         kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst );
									 CGColorSpaceRelease( colorSpace );

									 if( ctx )
									 {
										 CGContextSetInterpolationQuality( ctx, kCGInterpolationLow );
										 CGContextDrawImage( ctx, CGRectMake( 0, 0, w, h ), cgImage );
										 CGContextRelease( ctx );
										 self->cppBackend->OnFrameCaptured( self->stagingBgra, w, h );
									 }
								 }
								 CGImageRelease( cgImage );
								 dispatch_async( dispatch_get_main_queue(), ^{
									 self->snapshotInFlight = NO;
								 } );
							 } );
						 }];
}

- (void)webView:(WKWebView*)wv didFinishNavigation:(WKNavigation*)navigation
{
	navigationReady = YES;
	[self applyJsSettings];
	[self captureTick:nil];
}

- (WKWebView*)webView:(WKWebView*)wv
	createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration
			   forNavigationAction:(WKNavigationAction*)navigationAction
					windowFeatures:(WKWindowFeatures*)windowFeatures
{
	if( !navigationAction.targetFrame.isMainFrame && navigationAction.request )
	{
		[wv loadRequest:navigationAction.request];
	}
	return nil;
}

- (void)applyJsSettings
{
	if( !webView )
		return;

	std::string cssUtf8 = customCss ? [customCss UTF8String] : "";
	std::ostringstream js;
	js << "(function(){";
	js << "let el = document.getElementById('__resolume_custom_css');";
	js << "if(!el){ el = document.createElement('style'); el.id = '__resolume_custom_css'; if(document.head) document.head.appendChild(el); }";
	js << "if(el){ el.textContent = '" << EscapeJsString( cssUtf8 ) << "'; }";
	js << "const muted = " << ( audioMuted ? "true" : "false" ) << ";";
	js << "const vol = " << audioVolume << ";";
	js << "document.querySelectorAll('video, audio').forEach(m => { try { m.muted = muted; m.volume = vol; } catch(e){} });";
	js << "})();";

	NSString* nsJs = [NSString stringWithUTF8String:js.str().c_str()];
	[webView evaluateJavaScript:nsJs completionHandler:nil];
}

- (void)updateWindowVisibility:(BOOL)visible
{
	if( !hostWindow )
		return;
	if( visible )
	{
		[hostWindow setHasShadow:YES];
		[hostWindow setIgnoresMouseEvents:NO];
		[hostWindow setAlphaValue:1.0];
		[hostWindow setCollectionBehavior:NSWindowCollectionBehaviorDefault];
		[hostWindow setFrame:NSMakeRect( 120, 120, std::min< uint32_t >( 1600, targetWidth ), std::min< uint32_t >( 960, targetHeight ) )
					 display:YES];
		[hostWindow makeKeyAndOrderFront:nil];
	}
	else
	{
		[hostWindow setHasShadow:NO];
		[hostWindow setIgnoresMouseEvents:YES];
		[hostWindow setAlphaValue:0.005];
		[hostWindow setCollectionBehavior:( NSWindowCollectionBehaviorCanJoinAllSpaces |
											NSWindowCollectionBehaviorStationary |
											NSWindowCollectionBehaviorIgnoresCycle )];
		[hostWindow setContentSize:NSMakeSize( targetWidth, targetHeight )];
		[hostWindow setFrameOrigin:NSMakePoint( 0, 0 )];
		[hostWindow orderBack:nil];
	}
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
	[self updateWindowVisibility:NO];
	if( cppBackend )
		cppBackend->OnInteractiveWindowClosed();
	return NO;
}

@end

struct BrowserBackendMac::Impl
{
	ResolumeBrowserHelper* helper = nil;
};

std::unique_ptr< IBrowserBackend > CreateBrowserBackend()
{
	return std::make_unique< BrowserBackendMac >();
}

BrowserBackendMac::BrowserBackendMac() :
	m_impl( std::make_shared< Impl >() )
{
}

BrowserBackendMac::~BrowserBackendMac()
{
	Shutdown();
}

void BrowserBackendMac::RenderFallbackPattern( uint32_t width, uint32_t height )
{
	const uint32_t w       = std::max< uint32_t >( 128, width );
	const uint32_t h       = std::max< uint32_t >( 128, height );
	const size_t byteCount = static_cast< size_t >( w ) * h * 4;

	std::lock_guard< std::mutex > lock( m_frameMutex );
	m_latestFrame.width  = w;
	m_latestFrame.height = h;
	m_latestFrame.pixels.resize( byteCount );

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

bool BrowserBackendMac::Initialize( uint32_t width, uint32_t height, const std::string& initialUrl )
{
	uint32_t w      = std::max< uint32_t >( 128, width );
	uint32_t h      = std::max< uint32_t >( 128, height );
	std::string url = NormalizeUrl( initialUrl );

	RenderFallbackPattern( w, h );

	std::shared_ptr< Impl > impl = m_impl;
	BrowserBackendMac* selfPtr   = this;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && !impl->helper )
		{
			NSString* nsUrl = [NSString stringWithUTF8String:url.c_str()];
			impl->helper    = [[ResolumeBrowserHelper alloc] initWithBackend:selfPtr width:w height:h url:nsUrl];
		}
	} );
	return true;
}

void BrowserBackendMac::Shutdown()
{
	std::shared_ptr< Impl > impl = m_impl;
	if( impl )
	{
		dispatch_async( dispatch_get_main_queue(), ^{
			if( impl->helper )
			{
				ResolumeBrowserHelper* h = impl->helper;
				impl->helper             = nil;
				[h shutdown];
				[h release];
			}
		} );
	}
}

void BrowserBackendMac::Navigate( const std::string& url )
{
	std::string normalized       = NormalizeUrl( url );
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper && impl->helper->webView )
		{
			NSString* nsUrl = [NSString stringWithUTF8String:normalized.c_str()];
			NSURL* u        = [NSURL URLWithString:nsUrl];
			if( u )
				[impl->helper->webView loadRequest:[NSURLRequest requestWithURL:u]];
		}
	} );
}

void BrowserBackendMac::Reload()
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper && impl->helper->webView )
			[impl->helper->webView reload];
	} );
}

void BrowserBackendMac::GoBack()
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper && impl->helper->webView && [impl->helper->webView canGoBack] )
			[impl->helper->webView goBack];
	} );
}

void BrowserBackendMac::GoForward()
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper && impl->helper->webView && [impl->helper->webView canGoForward] )
			[impl->helper->webView goForward];
	} );
}

void BrowserBackendMac::Resize( uint32_t width, uint32_t height )
{
	uint32_t w                   = std::max< uint32_t >( 128, std::min< uint32_t >( 4096, width ) );
	uint32_t h                   = std::max< uint32_t >( 128, std::min< uint32_t >( 4096, height ) );
	std::shared_ptr< Impl > impl = m_impl;
	bool interactive             = m_interactiveVisible.load();
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper )
		{
			impl->helper->targetWidth  = w;
			impl->helper->targetHeight = h;
			if( !interactive && impl->helper->hostWindow )
				[impl->helper->hostWindow setContentSize:NSMakeSize( w, h )];
		}
	} );
}

void BrowserBackendMac::SetTargetFps( uint32_t fps )
{
	uint32_t clamped             = std::max< uint32_t >( 1, std::min< uint32_t >( 120, fps ) );
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper )
		{
			impl->helper->targetFps = clamped;
			[impl->helper restartTimer];
		}
	} );
}

void BrowserBackendMac::SetZoom( float zoomFactor )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper && impl->helper->webView )
		{
			impl->helper->zoomFactor = zoomFactor;
			if( [impl->helper->webView respondsToSelector:@selector( setPageZoom: )] )
				impl->helper->webView.pageZoom = zoomFactor;
		}
	} );
}

void BrowserBackendMac::SetTransparentBackground( bool transparent )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper && impl->helper->webView )
		{
			impl->helper->transparentBg = transparent ? YES : NO;
			[impl->helper->webView setValue:@( !transparent ) forKey:@"drawsBackground"];
		}
	} );
}

void BrowserBackendMac::SetCustomCss( const std::string& css )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper )
		{
			if( impl->helper->customCss )
				[impl->helper->customCss release];
			impl->helper->customCss = [[NSString stringWithUTF8String:css.c_str()] retain];
			[impl->helper applyJsSettings];
		}
	} );
}

void BrowserBackendMac::SetAudioMuted( bool muted )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper )
		{
			impl->helper->audioMuted = muted ? YES : NO;
			[impl->helper applyJsSettings];
		}
	} );
}

void BrowserBackendMac::SetAudioVolume( float gain )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper )
		{
			impl->helper->audioVolume = gain;
			[impl->helper applyJsSettings];
		}
	} );
}

void BrowserBackendMac::SetInteractiveWindowVisible( bool visible )
{
	m_interactiveVisible.store( visible );
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( impl && impl->helper )
			[impl->helper updateWindowVisibility:( visible ? YES : NO )];
	} );
}

bool BrowserBackendMac::IsInteractiveWindowVisible() const
{
	return m_interactiveVisible.load();
}

void BrowserBackendMac::OnInteractiveWindowClosed()
{
	m_interactiveVisible.store( false );
}

void BrowserBackendMac::SendMouseMove( float normX, float normY )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( !impl || !impl->helper || !impl->helper->webView )
			return;
		int px       = static_cast< int >( normX * impl->helper->targetWidth );
		int py       = static_cast< int >( normY * impl->helper->targetHeight );
		NSString* js = [NSString stringWithFormat:
                                     @"(function(){ var el = document.elementFromPoint(%d, %d); if(el) el.dispatchEvent(new MouseEvent('mousemove', {bubbles:true, clientX:%d, clientY:%d})); })();",
                                     px,
                                     py,
                                     px,
                                     py];
		[impl->helper->webView evaluateJavaScript:js completionHandler:nil];
	} );
}

void BrowserBackendMac::SendMouseButton( float normX, float normY, int button, bool down )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( !impl || !impl->helper || !impl->helper->webView )
			return;
		int px       = static_cast< int >( normX * impl->helper->targetWidth );
		int py       = static_cast< int >( normY * impl->helper->targetHeight );
		NSString* ev = down ? @"mousedown" : @"mouseup";
		NSString* js = [NSString stringWithFormat:
                                     @"(function(){ var el = document.elementFromPoint(%d, %d); if(el){ el.dispatchEvent(new MouseEvent('%@', {bubbles:true, button:%d, clientX:%d, clientY:%d})); if(!%d && %d===0) el.click(); } })();",
                                     px,
                                     py,
                                     ev,
                                     button,
                                     px,
                                     py,
                                     down ? 1 : 0,
                                     button];
		[impl->helper->webView evaluateJavaScript:js completionHandler:nil];
	} );
}

void BrowserBackendMac::SendMouseWheel( float /*normX*/, float /*normY*/, float deltaY )
{
	std::shared_ptr< Impl > impl = m_impl;
	dispatch_async( dispatch_get_main_queue(), ^{
		if( !impl || !impl->helper || !impl->helper->webView )
			return;
		NSString* js = [NSString stringWithFormat:@"window.scrollBy(0, %d);", static_cast< int >( deltaY )];
		[impl->helper->webView evaluateJavaScript:js completionHandler:nil];
	} );
}

void BrowserBackendMac::OnFrameCaptured( std::vector< uint8_t >& stagingBgra, uint32_t width, uint32_t height )
{
	std::lock_guard< std::mutex > lock( m_frameMutex );
	m_latestFrame.width  = width;
	m_latestFrame.height = height;
	std::swap( m_latestFrame.pixels, stagingBgra );
	m_latestFrame.frameNumber++;
}

bool BrowserBackendMac::CopyLatestFrame( BrowserFrameData& outFrame, uint64_t lastFrameNumber )
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
