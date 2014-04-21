#include "d3d_common.h"
#include "d3d_device.h"
#include "d3d_state.h"

QD3D11Device* g_pDevice = nullptr;
ID3D11DeviceContext1* g_pImmediateContext = nullptr;
IDXGISwapChain1* g_pSwapChain = nullptr;

bool DeviceStarted()
{
    return g_pDevice != nullptr;
}

QD3D11Device* InitDevice()
{
    memset( &g_BufferState, 0, sizeof( g_BufferState ) );

#ifdef WIN8
    g_BufferState.featureLevel = D3D_FEATURE_LEVEL_9_1;
#else
    g_BufferState.featureLevel = D3D_FEATURE_LEVEL_11_1; 
#endif
	HRESULT hr = QD3D::CreateDefaultDevice(
		D3D_DRIVER_TYPE_HARDWARE, 
		&g_pDevice, 
		&g_pImmediateContext, 
		&g_BufferState.featureLevel);
    if (FAILED(hr) || !g_pDevice || !g_pImmediateContext)
	{
        common->FatalError( "Failed to create Direct3D 11 device: 0x%08x.\n", hr );
        return nullptr;
	}

    common->Printf( "... feature level %d\n", g_BufferState.featureLevel );

    g_pDevice->AddRef();
    return g_pDevice;
}

void InitSwapChain( IDXGISwapChain1* swapChain )
{
    swapChain->AddRef();
    g_pSwapChain = swapChain;
    g_pSwapChain->GetDesc1( &g_BufferState.swapChainDesc );
    
    // Create D3D objects
    DestroyBuffers();
    CreateBuffers();

    // Clear the targets
    FLOAT clearCol[4] = { 0, 0, 0, 0 };
    g_pImmediateContext->ClearRenderTargetView( g_BufferState.backBufferView, clearCol );
    g_pImmediateContext->ClearDepthStencilView( g_BufferState.depthBufferView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0 );

    // Clear the screen immediately
    g_pSwapChain->Present( 0, 0 );
}

void DestroyDevice()
{
    SAFE_RELEASE(g_pImmediateContext);
    SAFE_RELEASE(g_pDevice);
}

void DestroySwapChain()
{
    SAFE_RELEASE(g_pSwapChain);
    memset( &g_BufferState, 0, sizeof( g_BufferState ) );
}

QD3D11Device* GetDevice() 
{
    return g_pDevice;
}

static idCVar d3d_multisamples( "d3d_multisamples", "32", CVAR_ARCHIVE | CVAR_INTEGER | CVAR_RENDERER, "the number of multisamples to use");

void GetSwapChainDescFromConfig( DXGI_SWAP_CHAIN_DESC1* scDesc )
{
    // Get best possible swapchain first
    QD3D::GetBestQualitySwapChainDesc( g_pDevice, scDesc );

#ifndef _ARM_
    // Clamp the max MSAA to user settings
    if ( d3d_multisamples.GetInteger() > 0 && scDesc->SampleDesc.Count > (UINT) d3d_multisamples.GetInteger() )
        scDesc->SampleDesc.Count = d3d_multisamples.GetInteger();
#endif
}

void SetupVideoConfig()
{
    // Set up a bunch of default state
    switch ( g_BufferState.featureLevel )
    {
    case D3D_FEATURE_LEVEL_9_1 : glConfig.version_string = "v9.1 (Compatibility)"; break;
    case D3D_FEATURE_LEVEL_9_2 : glConfig.version_string = "v9.2 (Compatibility)"; break;
    case D3D_FEATURE_LEVEL_9_3 : glConfig.version_string = "v9.3 (Compatibility)"; break;
    case D3D_FEATURE_LEVEL_10_0: glConfig.version_string = "v10.0 (Compatibility)"; break;
    case D3D_FEATURE_LEVEL_10_1: glConfig.version_string = "v10.1 (Compatibility)"; break;
    case D3D_FEATURE_LEVEL_11_0: glConfig.version_string = "v11.0"; break;
    case D3D_FEATURE_LEVEL_11_1: glConfig.version_string = "v11.1"; break;
    default: glConfig.version_string = "Direct3D 11"; break;
    }
    glConfig.renderer_string = "Direct3D 11";
    glConfig.vendor_string = "Microsoft Corporation";
    glConfig.extensions_string = "";
    glConfig.wgl_extensions_string = "";
    glConfig.shading_language_string = "HLSL";
    glConfig.glVersion = 0;

    // TODO: 
    // glConfig.vendor?
    glConfig.uniformBufferOffsetAlignment = 0;
    

    D3D11_DEPTH_STENCIL_VIEW_DESC depthBufferViewDesc;
    g_BufferState.depthBufferView->GetDesc( &depthBufferViewDesc );

    DWORD colorDepth = 0, depthDepth = 0, stencilDepth = 0;
    if ( FAILED( QD3D::GetBitDepthForFormat( g_BufferState.swapChainDesc.Format, &colorDepth ) ) )
        common->Error( "Bad bit depth supplied for color channel (%x)\n", g_BufferState.swapChainDesc.Format );

    if ( FAILED( QD3D::GetBitDepthForDepthStencilFormat( depthBufferViewDesc.Format, &depthDepth, &stencilDepth ) ) )
        common->Error( "Bad bit depth supplied for depth-stencil (%x)\n", depthBufferViewDesc.Format );

    glConfig.maxTextureSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    glConfig.maxTextureCoords = D3D11_REQ_SAMPLER_OBJECT_COUNT_PER_DEVICE;
    glConfig.maxTextureImageUnits = D3D11_REQ_SAMPLER_OBJECT_COUNT_PER_DEVICE;
    glConfig.maxTextureAnisotropy = D3D11_REQ_MAXANISOTROPY;
    glConfig.colorBits = (int) colorDepth;
    glConfig.depthBits = (int) depthDepth;
    glConfig.stencilBits = (int) stencilDepth;

    glConfig.multitextureAvailable = true;
    glConfig.directStateAccess = false;
    glConfig.textureCompressionAvailable = true;
    glConfig.anisotropicFilterAvailable = true;
    glConfig.textureLODBiasAvailable = true;
    glConfig.seamlessCubeMapAvailable = true;
    glConfig.sRGBFramebufferAvailable = true;
    glConfig.vertexBufferObjectAvailable = true;
    glConfig.mapBufferRangeAvailable = true;
    glConfig.drawElementsBaseVertexAvailable = true;
    glConfig.fragmentProgramAvailable = true;
    glConfig.glslAvailable = true;
    glConfig.uniformBufferAvailable = true;
    glConfig.twoSidedStencilAvailable = true;
    glConfig.depthBoundsTestAvailable = false; // @pjb: todo, check me
    glConfig.syncAvailable = true;
    glConfig.timerQueryAvailable = true;
    glConfig.occlusionQueryAvailable = true;
    glConfig.debugOutputAvailable = true;
    glConfig.swapControlTearAvailable = true;

    glConfig.stereo3Dmode = STEREO3D_OFF;
    glConfig.nativeScreenWidth = g_BufferState.swapChainDesc.Width;
    glConfig.nativeScreenHeight = g_BufferState.swapChainDesc.Height;
    glConfig.isStereoPixelFormat = false;
    glConfig.stereoPixelFormatAvailable = false;
    glConfig.multisamples = g_BufferState.swapChainDesc.SampleDesc.Count;
    glConfig.pixelAspect = glConfig.nativeScreenWidth / (float)glConfig.nativeScreenHeight;
    glConfig.global_vao = 0;

#if !defined(_ARM_) && !defined(D3D_NO_ENUM_DISPLAY)
	DEVMODE dm;
    memset( &dm, 0, sizeof( dm ) );
	dm.dmSize = sizeof( dm );
	if ( EnumDisplaySettingsEx( NULL, ENUM_CURRENT_SETTINGS, &dm, 0 ) )
	{
		glConfig.displayFrequency = dm.dmDisplayFrequency;
	}
#else
    // @pjb: todo: EnumDisplaySettingsEx doesn't exist.
    glConfig.displayFrequency = 60;
#endif
}



