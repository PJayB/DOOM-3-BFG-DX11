#pragma once

#include "d3d_common.h"

//----------------------------------------------------------------------------
// Consstants
//----------------------------------------------------------------------------

enum 
{
    BLENDSTATE_SRC_COUNT = 9,
    BLENDSTATE_DST_COUNT = 8,
    CULLMODE_COUNT = 3
};

enum 
{
    DEPTHSTATE_FLAG_TEST = 1,
    DEPTHSTATE_FLAG_MASK = 2,
    DEPTHSTATE_FLAG_EQUAL = 4, // as opposed to the default, LEq.
    DEPTHSTATE_COUNT = 8
};

enum 
{
    RASTERIZERSTATE_FLAG_POLY_OFFSET = 1,
    RASTERIZERSTATE_FLAG_POLY_OUTLINE = 2,
    RASTERIZERSTATE_COUNT = 4
};

//----------------------------------------------------------------------------
// Dynamic buffer layouts
//----------------------------------------------------------------------------
struct d3dQuadRenderVertex_t
{
    float coords[2];    
    float texcoords[2];
};

struct d3dQuadRenderConstantBuffer_t 
{
    float color[4];
};

// @pjb: todo: consider splitting this up if it's a perf issue
// reuploading the whole thing each time
struct d3dViewVSConstantBuffer_t
{
    float projectionMatrix[16];
    float modelViewMatrix[16];
    float depthRange[2];
    float __padding[2];
};

struct d3dViewPSConstantBuffer_t
{
    float clipPlane[4];
    float alphaClip[2];
    float __padding[2];
};

struct d3dSkyBoxVSConstantBuffer_t
{
    float eyePos[4];
};

struct d3dSkyBoxPSConstantBuffer_t
{
    float color[4];
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
    qboolean dynamic;
};

struct d3dViewRenderData_t
{
    ID3D11Buffer* vsConstantBuffer;
    ID3D11Buffer* psConstantBuffer;
};

struct d3dQuadRenderData_t
{
    // Shaders
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;

    // Vertex buffers
    ID3D11InputLayout* inputLayout;
    ID3D11Buffer* indexBuffer;
    ID3D11Buffer* vertexBuffer;

    // Constant buffers
    ID3D11Buffer* constantBuffer;
};

struct d3dSkyBoxRenderData_t
{
    // Shaders
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;

    // Vertex buffers
    ID3D11InputLayout* inputLayout;
    ID3D11Buffer* vertexBuffer;

    // Constant buffers
    ID3D11Buffer* vsConstantBuffer;
    ID3D11Buffer* psConstantBuffer;
};

// @pjb: for the generic stage rendering
struct d3dGenericStageRenderData_t 
{
    // Single-texture
    ID3D11VertexShader* vertexShaderST;
    ID3D11PixelShader* pixelShaderST;
    ID3D11InputLayout* inputLayoutST;

    // Multi-texture
    ID3D11VertexShader* vertexShaderMT;
    ID3D11PixelShader* pixelShaderMT;
    ID3D11InputLayout* inputLayoutMT;
};

struct d3dCircularBuffer_t
{
    ID3D11Buffer* buffer;
    unsigned currentOffset;
    unsigned nextOffset;
    unsigned size;
};

// @pjb: represents the GPU caches for stageVars_t
struct d3dTessStageBuffers_t {
    d3dCircularBuffer_t texCoords[NUM_TEXTURE_BUNDLES];
    d3dCircularBuffer_t colors;
};

// @pjb: represents the GPU caches for stageVars_t
struct d3dTessFogBuffers_t {
    d3dCircularBuffer_t texCoords;
    d3dCircularBuffer_t colors;
};

// @pjb: represents the dynamic light rendering information
struct d3dTessLightProjBuffers_t {
    d3dCircularBuffer_t indexes;
    d3dCircularBuffer_t texCoords;
    d3dCircularBuffer_t colors;
};

// @pjb: represents the GPU caches for shaderCommands_t
struct d3dTessBuffers_t {
    d3dCircularBuffer_t indexes;
    d3dCircularBuffer_t xyz;
    d3dTessStageBuffers_t stages[MAX_SHADER_STAGES];
    d3dTessLightProjBuffers_t dlights[MAX_DLIGHTS];
    d3dTessFogBuffers_t fog;
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
    ID3D11DepthStencilState* states[DEPTHSTATE_COUNT];
};

// @pjb: stores common blend states
struct d3dBlendStates_t
{
    ID3D11BlendState* opaque;
    ID3D11BlendState* states[BLENDSTATE_SRC_COUNT][BLENDSTATE_DST_COUNT];
};

// @pjb: stores draw info like samplers and buffers
struct d3dDrawState_t
{
    d3dQuadRenderData_t quadRenderData;
    d3dSkyBoxRenderData_t skyBoxRenderData;
    d3dViewRenderData_t viewRenderData;
    
    d3dTessBuffers_t tessBufs;
    d3dGenericStageRenderData_t genericStage;

    d3dRasterStates_t rasterStates;
    d3dDepthStates_t depthStates;
    d3dBlendStates_t blendStates;

    ID3D11Query* frameQuery;
};

// @pjb: stores the run-time game state. The game is set up like a state machine so we'll be doing the same.
struct d3dRunState_t {
    d3dViewVSConstantBuffer_t vsConstants;
    d3dViewPSConstantBuffer_t psConstants;
    unsigned long stateMask; // combination of GLS_* flags
    int cullMode; // CT_ flag
    unsigned long depthStateMask;
    qboolean vsDirtyConstants;
    qboolean psDirtyConstants;
};

//----------------------------------------------------------------------------
// Imports from d3d_device.cpp
//----------------------------------------------------------------------------
extern HRESULT g_hrLastError;
extern d3dBackBufferState_t g_BufferState;
extern d3dRunState_t g_RunState;
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

void DrawQuad( 
    const d3dQuadRenderData_t* qrd, 
    const d3dImage_t* texture, 
    const float* coords, 
    const float* texcoords, 
    const float* color );

// cullmode = CT_ flags
void CommitRasterizerState( int cullMode, qboolean polyOffset, qboolean outline );

ID3D11RasterizerState* GetRasterizerState( D3D11_CULL_MODE cullmode, unsigned long mask );
ID3D11DepthStencilState* GetDepthState( unsigned long mask ); // DEPTHSTATE_FLAG_ enum
ID3D11BlendState* GetBlendState( int src, int dst );
D3D11_BLEND GetSrcBlendConstant( int qConstant );
D3D11_BLEND GetDestBlendConstant( int qConstant );
D3D11_BLEND GetSrcBlendAlphaConstant( int qConstant );
D3D11_BLEND GetDestBlendAlphaConstant( int qConstant );

