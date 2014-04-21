#ifndef __D3D_DRIVER_H__
#define __D3D_DRIVER_H__

//----------------------------------------------------------------------------
// Driver entry points
//----------------------------------------------------------------------------

void D3DDrv_SetViewport( int left, int top, int width, int height );
void D3DDrv_SetState( unsigned long stateMask );
void D3DDrv_Init( void );
void D3DDrv_Shutdown( void );

QD3D11Device* D3DDrv_GetDevice();
ID3D11DeviceContext1* D3DDrv_GetImmediateContext();

#endif
