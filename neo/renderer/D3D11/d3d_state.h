#pragma once

#include "d3d_common.h"

//----------------------------------------------------------------------------
// Consstants
//----------------------------------------------------------------------------

enum 
{
    BLENDSTATE_SRC_COUNT = 8,
    BLENDSTATE_DST_COUNT = 8,
    CULLMODE_COUNT = 3
};

enum 
{
    DEPTHSTATE_FUNC_MASK = 3,
    DEPTHSTATE_FLAG_MASK = 4, // "mask" being a depth mode in this case
    DEPTHSTATE_COUNT = 8,
    STENCILPACKAGE_COUNT = 4,
    COLORMASK_COUNT = 16
};

enum 
{
    RASTERIZERSTATE_FLAG_POLY_OFFSET = 1,
    RASTERIZERSTATE_FLAG_POLY_OUTLINE = 2,
    RASTERIZERSTATE_COUNT = 4
};

//----------------------------------------------------------------------------
// Internal structures
//----------------------------------------------------------------------------
// @pjb: back-end representation of an image_t
struct d3dImage_t
{
    ID3D11Texture2D* pTexture;
    ID3D11ShaderResourceView* pSRV;
    ID3D11SamplerState* pSampler;
    DXGI_FORMAT internalFormat;
    int width;
    int height;
    int frameUsed;
    bool dynamic;
};

// @pjb: stores the D3D state and only changes every WndInit
struct d3dBackBufferState_t {
    D3D11_TEXTURE2D_DESC backBufferDesc;
    ID3D11RenderTargetView* backBufferView;
    ID3D11DepthStencilView* depthBufferView;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    D3D_FEATURE_LEVEL featureLevel;
};

// @pjb: stores common raster states
struct d3dRasterStates_t
{
    ID3D11RasterizerState* states[CULLMODE_COUNT][RASTERIZERSTATE_COUNT];
};

// @pjb: stores common depth states
struct d3dDepthStates_t
{
    ID3D11DepthStencilState* states[STENCILPACKAGE_COUNT][DEPTHSTATE_COUNT];
};

// @pjb: stores common blend states
struct d3dBlendStates_t
{
    ID3D11BlendState* states[COLORMASK_COUNT][BLENDSTATE_SRC_COUNT][BLENDSTATE_DST_COUNT];
};

// @pjb: stores draw info like samplers and buffers
struct d3dDrawState_t
{
    d3dRasterStates_t rasterStates;
    d3dDepthStates_t depthStates;
    d3dBlendStates_t blendStates;

    ID3D11Query* frameQuery;
};

//----------------------------------------------------------------------------
// Imports from d3d_device.cpp
//----------------------------------------------------------------------------
extern HRESULT g_hrLastError;
extern d3dBackBufferState_t g_BufferState;
extern d3dDrawState_t g_DrawState;

//----------------------------------------------------------------------------
// Imports from d3d_wnd.cpp
//----------------------------------------------------------------------------
extern QD3D11Device* g_pDevice;
extern ID3D11DeviceContext1* g_pImmediateContext;
extern IDXGISwapChain1* g_pSwapChain;

//----------------------------------------------------------------------------
// Internal APIs
//----------------------------------------------------------------------------

void InitDrawState();
void DestroyDrawState();

ID3D11RasterizerState* GetRasterizerState( D3D11_CULL_MODE cullmode, uint64 mask );
ID3D11DepthStencilState* GetDepthState( uint64 mask ); // DEPTHSTATE_FLAG_ enum
ID3D11BlendState* GetBlendState( uint64 cmask, uint64 src, uint64 dst );
D3D11_BLEND GetSrcBlendConstant( int qConstant );
D3D11_BLEND GetDestBlendConstant( int qConstant );
D3D11_BLEND GetSrcBlendAlphaConstant( int qConstant );
D3D11_BLEND GetDestBlendAlphaConstant( int qConstant );

