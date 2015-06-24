#ifndef __D3D_DEVICE_H__
#define __D3D_DEVICE_H__

bool DeviceStarted();
QD3D11Device* InitDevice(); // release when done
QD3D11Device* GetDevice();
void DestroyDevice();
void InitSwapChain( IDXGISwapChain1* swapChain ); // adds ref
void DestroySwapChain();

void GetSwapChainDescFromConfig( DXGI_SWAP_CHAIN_DESC1* desc, UINT maxMultisamples );

void CreateBuffers();
void DestroyBuffers();

void SetupVideoConfig();

#endif
