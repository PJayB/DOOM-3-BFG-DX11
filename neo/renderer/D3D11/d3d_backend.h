#ifndef __D3D_DRIVER_H__
#define __D3D_DRIVER_H__

#include "d3d_common.h"

enum 
{
    CLEAR_COLOR   = 1,
    CLEAR_DEPTH   = 2,
    CLEAR_STENCIL = 4
};

//----------------------------------------------------------------------------
// Driver entry points
//----------------------------------------------------------------------------

void D3DDrv_Init( void );
void D3DDrv_Shutdown( void );

QD3D11Device* D3DDrv_GetDevice();
ID3D11DeviceContext1* D3DDrv_GetImmediateContext();

HRESULT D3DDrv_EndFrame( int frequency );

void D3DDrv_Flush( ID3D11DeviceContext1* pContext );
void D3DDrv_Clear( ID3D11DeviceContext1* pContext, unsigned long bits, const float* clearCol, unsigned long stencil, float depth );
void D3DDrv_SetScissor( ID3D11DeviceContext1* pContext, int left, int top, int width, int height );
void D3DDrv_SetViewport( ID3D11DeviceContext1* pContext, int left, int top, int width, int height );

// This only handles depth, no stencil. You'll have to make your own if you want to 
// handle stencil.
ID3D11DepthStencilState* D3DDrv_GetDepthState( uint64 stateBits );

// This only handles blend states and masks. If you need alpha-to-coverage or
// independent blending you'll have to make your own.
ID3D11BlendState* D3DDrv_GetBlendState( uint64 stateBits );

// This only handles the default polyoffset. You'll have to make your own if you want
// to override the polyoffset.
ID3D11RasterizerState* D3DDrv_GetRasterizerState( int cullMode, uint64 stateBits );

ID3D11DepthStencilState* D3DDrv_CreateDepthStencilState( uint64 stateBits );
ID3D11RasterizerState* D3DDrv_CreateRasterizerState( 
    int cullMode, 
    uint64 stateBits,
    float polyOffsetBias = 0,           // r_offsetFactor is automatically added to this
    float polyOffsetSlopeBias = 0 );    // r_offsetUnits is automatically added to this

HRESULT D3DDrv_LastError( void );

//----------------------------------------------------------------------------
// Set state shortcuts
//----------------------------------------------------------------------------
void D3DDrv_SetBlendStateFromMask( ID3D11DeviceContext1* pContext, uint64 stateBits );
void D3DDrv_SetDepthStateFromMask( ID3D11DeviceContext1* pContext, uint64 stateBits );
void D3DDrv_SetRasterizerStateFromMask( ID3D11DeviceContext1* pContext, int cullMode, uint64 stateBits );


#endif
