#ifndef __D3D_PUBLIC_H__
#define __D3D_PUBLIC_H__

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

bool D3DWnd_Init( glimpParms_t parms );
bool D3DWnd_SetScreenParms( glimpParms_t parms );
void D3DWnd_Shutdown( void );
HWND D3DWnd_GetWindowHandle( void );

#endif
