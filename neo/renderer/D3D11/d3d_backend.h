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

void D3DDrv_Flush();
void D3DDrv_Clear( unsigned long bits, const float* clearCol, unsigned long stencil, float depth );
void D3DDrv_EndFrame( int frequency );
void D3DDrv_SetDefaultState();
void D3DDrv_SetScissor( int left, int top, int width, int height );
void D3DDrv_SetViewport( int left, int top, int width, int height );

ID3D11DepthStencilState* D3DDrv_GetDepthState( uint64 stateBits );
ID3D11BlendState* D3DDrv_GetBlendState( uint64 stateBits );
void D3DDrv_SetRasterizerOptions( uint64 stateBits );

ID3D11DepthStencilState* D3DDrv_CreateDepthStencilState( uint64 stateBits );

HRESULT D3DDrv_LastError( void );

#endif
