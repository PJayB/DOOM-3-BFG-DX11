#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../tr_local.h"
#include "../../framework/Common_local.h"

// D3D headers
#include "d3d_common.h"
#include "d3d_backend.h"
#include "d3d_state.h"

//----------------------------------------------------------------------------
// Local data
//----------------------------------------------------------------------------
HRESULT g_hrLastError = S_OK;

//----------------------------------------------------------------------------
//
// DRIVER INTERFACE
//
//----------------------------------------------------------------------------
void D3DDrv_Shutdown( void )
{
    DestroyDrawState();
}

void D3DDrv_Init( void )
{
    InitDrawState();
}

HRESULT D3DDrv_LastError( void )
{
    return (size_t) g_hrLastError;
}

QD3D11Device* D3DDrv_GetDevice()
{
    assert(idLib::IsMainThread());
    return g_pDevice;
}

ID3D11DeviceContext2* D3DDrv_GetImmediateContext()
{
    assert(idLib::IsMainThread());
    assert(g_pImmediateContext);
    return g_pImmediateContext;
}

void D3DDrv_GetBackBufferTexture(ID3D11Resource** ppResource)
{
    assert(idLib::IsMainThread());
    assert(g_pImmediateContext);
    assert(g_BufferState.backBufferView);

    g_BufferState.backBufferView->GetResource(ppResource);
}


//----------------------------------------------------------------------------
// Clear the back buffers
//----------------------------------------------------------------------------
void D3DDrv_Clear( ID3D11DeviceContext2* pContext, unsigned long bits, const float* clearCol, unsigned long stencil, float depth )
{
    if ( bits & CLEAR_COLOR )
    {
        static float defaultCol[] = { 0, 0, 0, 0 };
        if ( !clearCol ) { clearCol = defaultCol; }

        pContext->ClearRenderTargetView( g_BufferState.backBufferView, clearCol );
    }

    if ( bits & ( CLEAR_DEPTH | CLEAR_STENCIL ) )
    {
        DWORD clearBits = 0;
        if ( bits & CLEAR_DEPTH ) { clearBits |= D3D11_CLEAR_DEPTH; }
        if ( bits & CLEAR_STENCIL ) { clearBits |= D3D11_CLEAR_STENCIL; }
        pContext->ClearDepthStencilView( g_BufferState.depthBufferView, clearBits, depth, (UINT8) stencil );
    }
}

//----------------------------------------------------------------------------
// Flush the command buffer
//----------------------------------------------------------------------------
void D3DDrv_Flush( ID3D11DeviceContext2* pContext )
{
    pContext->End( g_DrawState.frameQuery );

    BOOL finished = FALSE;
    HRESULT hr;
    do
    {
        YieldProcessor();
        hr = pContext->GetData( g_DrawState.frameQuery, &finished, sizeof(finished), 0 );
    }
    while ( ( hr == S_OK || hr == S_FALSE ) && finished == FALSE );

    //assert( SUCCEEDED( hr ) );
}

//----------------------------------------------------------------------------
// Present the frame
//----------------------------------------------------------------------------
HRESULT D3DDrv_EndFrame( int frequency )
{
    //int frequency = 0;
	//if ( r_swapInterval->integer > 0 ) 
    //{
	//	frequency = min( glConfig.displayFrequency, 60 / r_swapInterval->integer );
    //}

    DXGI_PRESENT_PARAMETERS parameters = {0};
	parameters.DirtyRectsCount = 0;
	parameters.pDirtyRects = nullptr;
	parameters.pScrollRect = nullptr;
	parameters.pScrollOffset = nullptr;
    
    HRESULT hr = S_OK;
    switch (frequency)
    {
    case 60: hr = g_pSwapChain->Present1( 1, 0, &parameters ); break; 
    case 30: hr = g_pSwapChain->Present1( 2, 0, &parameters ); break;
    default: hr = g_pSwapChain->Present1( 0, 0, &parameters ); break; 
    }

    if ( FAILED( hr ) )
        return hr;

#ifdef WIN8
	// Discard the contents of the render target.
	// This is a valid operation only when the existing contents will be entirely
	// overwritten. If dirty or scroll rects are used, this call should be removed.
	g_pImmediateContext->DiscardView( g_BufferState.backBufferView );

	// Discard the contents of the depth stencil.
	g_pImmediateContext->DiscardView( g_BufferState.depthBufferView );

    // Present unbinds the rendertarget, so let's put it back (FFS)
    g_pImmediateContext->OMSetRenderTargets( 1, &g_BufferState.backBufferView, g_BufferState.depthBufferView );
#endif

    return S_OK;
}















#if 0 
void D3DDrv_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
    // @pjb: todo?
    if ( glConfig.deviceSupportsGamma )
    {
        common->FatalError( "D3D11 hardware gamma ramp not implemented." );
    }
}

void D3DDrv_SetProjection( const float* projMatrix )
{
    memcpy( g_RunState.vsConstants.projectionMatrix, projMatrix, sizeof(float) * 16 );
    g_RunState.vsDirtyConstants = true;
}

void D3DDrv_GetProjection( float* projMatrix )
{
    memcpy( projMatrix, g_RunState.vsConstants.projectionMatrix, sizeof(float) * 16 );
}

void D3DDrv_SetModelView( const float* modelViewMatrix )
{
    memcpy( g_RunState.vsConstants.modelViewMatrix, modelViewMatrix, sizeof(float) * 16 );
    g_RunState.vsDirtyConstants = true;
}

void D3DDrv_GetModelView( float* modelViewMatrix )
{
    memcpy( modelViewMatrix, g_RunState.vsConstants.modelViewMatrix, sizeof(float) * 16 );
}

void D3DDrv_ResetState2D( void )
{
    D3DDrv_SetModelView( s_identityMatrix );
    D3DDrv_SetState( GLS_DEPTHTEST_DISABLE |
			  GLS_SRCBLEND_SRC_ALPHA |
			  GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );

    CommitRasterizerState( CT_TWO_SIDED, false, false );

    D3DDrv_SetPortalRendering( false, NULL, NULL );
    D3DDrv_SetDepthRange( 0, 0 );
}

void D3DDrv_ResetState3D( void )
{
    D3DDrv_SetModelView( s_identityMatrix );
    D3DDrv_SetState( GLS_DEFAULT );
    D3DDrv_SetDepthRange( 0, 1 );
}

void D3DDrv_SetPortalRendering( bool enabled, const float* flipMatrix, const float* plane )
{
    if ( enabled ) 
    {
        // Transform the supplied plane by the INVERSE of the flipMatrix
        // We can just transpose the flipMatrix because it's orthogonal
        // To clip, dot(vertex, plane) < 0
        g_RunState.psConstants.clipPlane[0] = flipMatrix[ 0] * plane[0] + flipMatrix[ 4] * plane[1] + flipMatrix[ 8] * plane[2] + flipMatrix[12] * plane[3];
        g_RunState.psConstants.clipPlane[1] = flipMatrix[ 1] * plane[0] + flipMatrix[ 5] * plane[1] + flipMatrix[ 9] * plane[2] + flipMatrix[13] * plane[3]; 
        g_RunState.psConstants.clipPlane[2] = flipMatrix[ 2] * plane[0] + flipMatrix[ 6] * plane[1] + flipMatrix[10] * plane[2] + flipMatrix[14] * plane[3]; 
        g_RunState.psConstants.clipPlane[3] = flipMatrix[ 3] * plane[0] + flipMatrix[ 7] * plane[1] + flipMatrix[11] * plane[2] + flipMatrix[15] * plane[3]; 
    }
    else
    {
        // Reset the clip plane
        g_RunState.psConstants.clipPlane[0] = 
        g_RunState.psConstants.clipPlane[1] = 
        g_RunState.psConstants.clipPlane[2] = 
        g_RunState.psConstants.clipPlane[3] = 0;
    }

    g_RunState.psDirtyConstants = true;
}

void D3DDrv_SetDepthRange( float minRange, float maxRange )
{
    g_RunState.vsConstants.depthRange[0] = minRange;
    g_RunState.vsConstants.depthRange[1] = maxRange - minRange;
    g_RunState.vsDirtyConstants = true;
}

void D3DDrv_SetDrawBuffer( int buffer )
{

}

void D3DDrv_MakeCurrent( bool current )
{
    
}

void D3DDrv_DebugSetOverdrawMeasureEnabled( bool enabled )
{

}

void D3DDrv_DebugSetTextureMode( const char* mode )
{

}

#endif
