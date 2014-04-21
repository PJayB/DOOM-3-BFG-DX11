#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../../renderer/D3D11/d3d_common.h"
#include "../../renderer/D3D11/d3d_device.h"
#include "../../renderer/D3D11/d3d_backend.h"
#include "win_local.h"
#include "rc/doom_resource.h"
#include "../../renderer/tr_local.h"

#define	WINDOW_CLASS_NAME	"Doom 3: BFG Edition (Direct3D)"

bool GLW_GetWindowDimensions( const glimpParms_t parms, int &x, int &y, int &w, int &h );

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
static HWND CreateGameWindow( const glimpParms_t* parms )
{
	int				x, y, w, h;
	if ( !GLW_GetWindowDimensions( *parms, x, y, w, h ) ) {
		return false;
	}

    UINT exStyle;
    UINT style;

	if ( parms->fullScreen )
	{ 
		exStyle = WS_EX_TOPMOST;
		style = WS_POPUP|WS_VISIBLE|WS_SYSMENU;
	}
	else
	{
		exStyle = 0;
		style = WS_OVERLAPPED|WS_BORDER|WS_CAPTION|WS_VISIBLE|WS_SYSMENU;
	}

    return CreateWindowEx(
        exStyle, 
        WINDOW_CLASS_NAME,
        "Doom 3: BFG Edition",
        style, 
        x, 
        y, 
        w, 
        h, 
        NULL, 
        NULL, 
        win32.hInstance, 
        NULL );
}

static IDXGISwapChain1* CreateSwapChain( QD3D11Device* device, const glimpParms_t* parms )
{
    DXGI_SWAP_CHAIN_DESC1 scDesc;
	ZeroMemory( &scDesc, sizeof(scDesc) );
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    scDesc.Width = parms->width;
    scDesc.Height = parms->height;
    
    GetSwapChainDescFromConfig( &scDesc );
    
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsd;
    ZeroMemory( &fsd, sizeof(fsd) );
    fsd.Windowed = !parms->fullScreen;
    fsd.Scaling = DXGI_MODE_SCALING_STRETCHED;

    IDXGISwapChain1* swapChain = nullptr;
    HRESULT hr = QD3D::CreateSwapChain(device, win32.hWnd, &scDesc, &fsd, &swapChain);
    if (FAILED(hr))
    {
        return nullptr;
    }

    InitSwapChain( swapChain );

    return swapChain;
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

    win32.hWnd = CreateGameWindow(&parms);
    if ( !win32.hWnd )
    {
		common->Printf( "^3D3DWnd_CreateWindow() - Couldn't create window\n" );
        return false;
    }

	QD3D11Device* device = InitDevice();
    IDXGISwapChain1* swapChain = CreateSwapChain( device, &parms );
    if ( !swapChain )
    {
        SAFE_RELEASE( device );
        return false;
    }

    SAFE_RELEASE( swapChain );
    SAFE_RELEASE( device );

    SetupVideoConfig();

	glConfig.isFullscreen = parms.fullScreen;
	glConfig.pixelAspect = 1.0f;	// FIXME: some monitor modes may be distorted

	glConfig.isFullscreen = parms.fullScreen;
	glConfig.nativeScreenWidth = parms.width;
	glConfig.nativeScreenHeight = parms.height;

    D3DDrv_DriverInit();

    ::ShowWindow( win32.hWnd, SW_SHOW );
    ::UpdateWindow( win32.hWnd );
	::SetForegroundWindow( win32.hWnd );
	::SetFocus( win32.hWnd );

    return true;
}

bool D3DWnd_SetScreenParms( glimpParms_t parms ) 
{
    DestroyBuffers();
    DestroySwapChain();

	int x, y, w, h;
	if ( !GLW_GetWindowDimensions( parms, x, y, w, h ) ) {
		return false;
	}

    int exstyle;
	int stylebits;

	if ( parms.fullScreen ) {
		exstyle = WS_EX_TOPMOST;
		stylebits = WS_POPUP|WS_VISIBLE|WS_SYSMENU;
	} else {
		exstyle = 0;
		stylebits = WINDOW_STYLE|WS_SYSMENU;
	}

	SetWindowLong( win32.hWnd, GWL_STYLE, stylebits );
	SetWindowLong( win32.hWnd, GWL_EXSTYLE, exstyle );

	SetWindowPos( win32.hWnd, 
        parms.fullScreen ? HWND_TOPMOST : HWND_NOTOPMOST, 
        x, y, w, h, 
        SWP_SHOWWINDOW );

    IDXGISwapChain1* swapChain = CreateSwapChain( GetDevice(), &parms );
    if ( !swapChain )
    {
        return false;
    }

    SetupVideoConfig();

	glConfig.isFullscreen = parms.fullScreen;
	glConfig.pixelAspect = 1.0f;	// FIXME: some monitor modes may be distorted

	glConfig.isFullscreen = parms.fullScreen;
	glConfig.nativeScreenWidth = parms.width;
	glConfig.nativeScreenHeight = parms.height;

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
