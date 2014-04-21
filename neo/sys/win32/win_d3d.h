#ifndef __D3D_PUBLIC_H__
#define __D3D_PUBLIC_H__

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void D3DWnd_Init( void );
void D3DWnd_Shutdown( void );
HWND D3DWnd_GetWindowHandle( void );

#ifdef __cplusplus
} // extern "C"
#endif

#endif
