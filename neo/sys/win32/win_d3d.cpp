#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../../renderer/D3D11/d3d_common.h"
#include "../../renderer/D3D11/d3d_device.h"
#include "../../renderer/D3D11/d3d_backend.h"
#include "win_local.h"
#include "rc/doom_resource.h"
#include "../../renderer/tr_local.h"

#define	WINDOW_CLASS_NAME	"Doom 3: BFG Edition (Direct3D)"

//----------------------------------------------------------------------------
// WndProc: Intercepts window events before passing them on to the game.
//----------------------------------------------------------------------------
static LONG WINAPI Direct3DWndProc (
    HWND    hWnd,
    UINT    uMsg,
    WPARAM  wParam,
    LPARAM  lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
        // If our window was closed, lose our cached handle
		if (hWnd == win32.hWnd) {
            win32.hWnd = NULL;
        }
		
        // We want to pass this message on to the MainWndProc
        break;
	case WM_SIZE:

        // @pjb: todo: recreate swapchain?
        // @pjb: actually fuck that. disable window sizing.

        break;

    default:
        break;
    }

    return MainWndProc( hWnd, uMsg, wParam, lParam );
}

//----------------------------------------------------------------------------
// Register the window class.
//----------------------------------------------------------------------------
static BOOL RegisterWindowClass()
{
    WNDCLASS wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = (WNDPROC) Direct3DWndProc;
    wc.cbClsExtra = 0;
    wc.hInstance = win32.hInstance;
    wc.hIcon = LoadIcon( win32.hInstance, MAKEINTRESOURCE(IDI_ICON1) );
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    return ::RegisterClass( &wc ) != 0;
}

//----------------------------------------------------------------------------
// Creates a window to render our game in.
//----------------------------------------------------------------------------
static HWND CreateGameWindow( int x, int y, int width, int height, bool fullscreen )
{
    UINT exStyle;
    UINT style;

	if ( fullscreen )
	{ 
		exStyle = WS_EX_TOPMOST;
		style = WS_POPUP|WS_VISIBLE|WS_SYSMENU;
	}
	else
	{
		exStyle = 0;
		style = WS_OVERLAPPED|WS_BORDER|WS_CAPTION|WS_VISIBLE|WS_SYSMENU;
	}
  
    RECT rect = { x, y, x + width, y + height };
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    // Make sure it's on-screen 
    if ( rect.top < 0 ) 
    {
        rect.bottom -= rect.top;
        rect.top = 0;
    }
    if ( rect.left < 0 ) 
    {
        rect.right -= rect.left;
        rect.left = 0;
    }

    // @pjb: todo: right and bottom edges of the monitor

    return CreateWindowEx(
        exStyle, 
        WINDOW_CLASS_NAME,
        "Doom 3: BFG Edition",
        style, 
        rect.left, 
        rect.top, 
        rect.right - rect.left, 
        rect.bottom - rect.top, 
        NULL, 
        NULL, 
        win32.hInstance, 
        NULL );
}

//----------------------------------------------------------------------------
// Creates a window, initializes the driver and sets up Direct3D state.
//----------------------------------------------------------------------------
bool D3DWnd_Init( glimpParms_t parms )
{
	common->Printf( "Initializing D3D11 subsystem\n" );

    if ( !RegisterWindowClass() )
    {
        //ri.Error( ERR_FATAL, "Failed to register D3D11 window class.\n" );
        //return;
    }

    win32.hWnd = CreateGameWindow( 
        parms.x,
        parms.y,
        parms.width, 
        parms.height,
        parms.fullScreen != 0);
    if ( !win32.hWnd )
    {
        return false;
    }

	QD3D11Device* device = InitDevice();

    DXGI_SWAP_CHAIN_DESC1 scDesc;
	ZeroMemory( &scDesc, sizeof(scDesc) );
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    
    GetSwapChainDescFromConfig( &scDesc );
    
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsd;
    ZeroMemory( &fsd, sizeof(fsd) );
    fsd.Windowed = !parms.fullScreen;
    fsd.Scaling = DXGI_MODE_SCALING_STRETCHED;

    IDXGISwapChain1* swapChain = nullptr;
    HRESULT hr = QD3D::CreateSwapChain(device, win32.hWnd, &scDesc, &fsd, &swapChain);
    if (FAILED(hr))
    {
        return false;
    }

    InitSwapChain( swapChain );

    SAFE_RELEASE( swapChain );
    SAFE_RELEASE( device );

    D3DDrv_DriverInit();

    ::ShowWindow( win32.hWnd, SW_SHOW );
    ::UpdateWindow( win32.hWnd );
	::SetForegroundWindow( win32.hWnd );
	::SetFocus( win32.hWnd );

    return true;
}

//----------------------------------------------------------------------------
// Cleans up and stops D3D, and closes the window.
//----------------------------------------------------------------------------
void D3DWnd_Shutdown( void )
{
    D3DDrv_Shutdown();

    DestroyDevice();
    DestroySwapChain();

    ::UnregisterClass( WINDOW_CLASS_NAME, win32.hInstance );
    ::DestroyWindow( win32.hWnd );

    win32.hWnd = NULL;
}
