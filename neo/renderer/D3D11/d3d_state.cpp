#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../tr_local.h"
#include "../../framework/Common_local.h"

#include "d3d_common.h"
#include "d3d_state.h"
#include "d3d_backend.h"

extern const float s_identityMatrix[16];

static_assert( STENCILPACKAGE_COUNT == GLS_DEPTH_STENCIL_PACKAGE_COUNT, "Update the STENCILPACKAGE_COUNT constant in d3d_state.h" );

//----------------------------------------------------------------------------
// Locals
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Globals
//----------------------------------------------------------------------------
d3dBackBufferState_t g_BufferState;
d3dDrawState_t g_DrawState;

//----------------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------------
// @pjb: todo: replace these with defaults from CVars
static const DXGI_FORMAT DEPTH_TEXTURE_FORMAT = DXGI_FORMAT_R24G8_TYPELESS;
static const DXGI_FORMAT DEPTH_DEPTH_VIEW_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
static const DXGI_FORMAT DEPTH_SHADER_VIEW_FORMAT = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

//----------------------------------------------------------------------------
//
// LOCAL FUNCTIONS
//
//----------------------------------------------------------------------------

static void ConfigureDepthStencilState( D3D11_DEPTH_STENCIL_DESC* dsd, uint64 mask );

//----------------------------------------------------------------------------
// Get the back buffer and depth/stencil buffer.
//----------------------------------------------------------------------------
void CreateBuffers()
{
    g_BufferState.backBufferView = QD3D::CreateBackBufferView(g_pSwapChain, g_pDevice, &g_BufferState.backBufferDesc);
    ASSERT(g_BufferState.backBufferView);

    g_BufferState.depthBufferView = QD3D::CreateDepthBufferView(
        g_pDevice,
        g_BufferState.backBufferDesc.Width,
        g_BufferState.backBufferDesc.Height,
        // @pjb: todo: make these dependent on Cvars
        DEPTH_TEXTURE_FORMAT,
        DEPTH_DEPTH_VIEW_FORMAT,
        g_BufferState.backBufferDesc.SampleDesc.Count,
        g_BufferState.backBufferDesc.SampleDesc.Quality,
        D3D11_BIND_SHADER_RESOURCE);
    ASSERT(g_BufferState.depthBufferView);
}

//----------------------------------------------------------------------------
// Release references to the back buffer and depth
//----------------------------------------------------------------------------
void DestroyBuffers()
{
    SAFE_RELEASE(g_BufferState.backBufferView);
    SAFE_RELEASE(g_BufferState.depthBufferView);
}

//----------------------------------------------------------------------------
// Set state shortcuts
//----------------------------------------------------------------------------
void D3DDrv_SetBlendStateFromMask( ID3D11DeviceContext1* pContext, uint64 stateBits )
{
    UINT mask = ~0U;
    pContext->OMSetBlendState( 
        D3DDrv_GetBlendState( stateBits ),
        NULL, 
        mask );
}

void D3DDrv_SetDepthStateFromMask( ID3D11DeviceContext1* pContext, uint64 stateBits )
{
    uint stencilRef = GLS_STENCIL_GET_REF( stateBits );
    pContext->OMSetDepthStencilState( D3DDrv_GetDepthState( stateBits ), stencilRef );
}

void D3DDrv_SetRasterizerStateFromMask( ID3D11DeviceContext1* pContext, int cullMode, uint64 stateBits )
{
    pContext->RSSetState( D3DDrv_GetRasterizerState( cullMode, stateBits ) );
}

//----------------------------------------------------------------------------
// Set the scissor rect
//----------------------------------------------------------------------------
void D3DDrv_SetScissor( ID3D11DeviceContext1* pContext, int left, int top, int width, int height )
{
    RECT r = 
    { 
        left, 
        (int)g_BufferState.backBufferDesc.Height - top - height ,
        left + width, 
        (int)g_BufferState.backBufferDesc.Height - top
    };
    pContext->RSSetScissorRects( 1, &r );
}

//----------------------------------------------------------------------------
// Set the viewport
//----------------------------------------------------------------------------
void D3DDrv_SetViewport( ID3D11DeviceContext1* pContext, int left, int top, int width, int height )
{
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = __max( 0, left );
    viewport.TopLeftY = __max( 0, (int)g_BufferState.backBufferDesc.Height - top - height );
    viewport.Width = __min( (int)g_BufferState.backBufferDesc.Width - viewport.TopLeftX, width );
    viewport.Height = __min( (int)g_BufferState.backBufferDesc.Height - viewport.TopLeftY, height );
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;
    pContext->RSSetViewports( 1, &viewport );
}

//----------------------------------------------------------------------------
// Get the depth state based on the mask (no stencil)
//----------------------------------------------------------------------------
ID3D11DepthStencilState* D3DDrv_GetDepthState( uint64 stateBits )
{
    return GetDepthState( stateBits );
}

//----------------------------------------------------------------------------
// Get the blend state based on the mask
//----------------------------------------------------------------------------
ID3D11BlendState* D3DDrv_GetBlendState( uint64 stateBits )
{
    uint64 colorMask = stateBits & GLS_COLORALPHAMASK;
    uint64 srcFactor = stateBits & GLS_SRCBLEND_BITS;
    uint64 dstFactor = stateBits & GLS_DSTBLEND_BITS;

    return GetBlendState(
                colorMask, 
                srcFactor, 
                dstFactor );
}

//----------------------------------------------------------------------------
// Set the rasterizer mode based on the mask, but defer until later
//----------------------------------------------------------------------------
ID3D11RasterizerState* D3DDrv_GetRasterizerState( int cullType, uint64 stateBits )
{
    // Resolve which direction we're culling in
    D3D11_CULL_MODE cullMode = D3D11_CULL_NONE;
	if ( cullType != CT_TWO_SIDED ) 
    {
		if ( cullType == CT_BACK_SIDED )
			cullMode = D3D11_CULL_BACK;
		else if ( cullType == CT_FRONT_SIDED )
			cullMode = D3D11_CULL_FRONT;
        else
            assert(0);
	}

    int rasterFlags = 0;
    switch ( stateBits & GLS_POLYGON_OFFSET_MASK )
    {
    case 0:
        break;
    case GLS_POLYGON_OFFSET_DECAL:
        rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OFFSET_DECAL;
        break;
    case GLS_POLYGON_OFFSET_SHADOW:
        rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OFFSET_SHADOW;
        break;
    default:
        assert(0);
    }    

    if ( stateBits & GLS_POLYMODE_LINE ) { 
        rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OUTLINE; 
    }

    return GetRasterizerState( cullMode, rasterFlags );
}

//----------------------------------------------------------------------------
// Create a depth stencil with stencil parameters
//----------------------------------------------------------------------------
ID3D11DepthStencilState* D3DDrv_CreateDepthStencilState( uint64 stateBits )
{
    D3D11_DEPTH_STENCIL_DESC dsd;
    ZeroMemory( &dsd, sizeof( dsd ) );

    ConfigureDepthStencilState( &dsd, stateBits );

    ID3D11DepthStencilState* pDSS = nullptr;
    g_pDevice->CreateDepthStencilState( &dsd, &pDSS );
    return pDSS;
}

//----------------------------------------------------------------------------
// Create a depth stencil with stencil parameters
//----------------------------------------------------------------------------
ID3D11RasterizerState* D3DDrv_CreateRasterizerState( int cullType, uint64 stateBits, float polyOffsetBias, float polyOffsetSlopeBias )
{
    D3D11_RASTERIZER_DESC rd;
    ZeroMemory( &rd, sizeof( rd ) );

    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    rd.DepthBiasClamp = 0;

    switch (cullType)
    {
    case CT_BACK_SIDED:
        rd.CullMode = D3D11_CULL_FRONT;
        break;
    case CT_FRONT_SIDED:
        rd.CullMode = D3D11_CULL_BACK;
        break;
    case CT_TWO_SIDED:
        rd.CullMode = D3D11_CULL_NONE;
        break;
    default:
        assert(0);
        return nullptr;
    }

    if ( stateBits & GLS_POLYMODE_LINE ) 
    {
        rd.FillMode = D3D11_FILL_WIREFRAME;
    }
    else
    {
        rd.FillMode = D3D11_FILL_SOLID;
    }

    switch ( stateBits & GLS_POLYGON_OFFSET_MASK )
    {
    case GLS_POLYGON_OFFSET_DECAL:
        rd.DepthBias = r_offsetFactor.GetFloat() + polyOffsetBias;
        rd.SlopeScaledDepthBias = r_offsetUnits.GetFloat() + polyOffsetSlopeBias;
        break;
    case GLS_POLYGON_OFFSET_SHADOW:
        rd.DepthBias = r_shadowPolygonFactor.GetFloat() + polyOffsetBias;
        rd.SlopeScaledDepthBias = -r_shadowPolygonOffset.GetFloat() + polyOffsetSlopeBias;
        break;
    default:
        break;
    }

    ID3D11RasterizerState* pRS = nullptr;
    g_pDevice->CreateRasterizerState( &rd, &pRS );
    return pRS;
}

//----------------------------------------------------------------------------
// Get the depth stencil state based on a mask
//----------------------------------------------------------------------------
ID3D11DepthStencilState* GetDepthState( uint64 mask )
{
    uint64 stencilPackage = ( mask & GLS_DEPTH_STENCIL_PACKAGE_BITS ) >> 37;
    uint64 rmask = ( mask & GLS_DEPTHFUNC_BITS ) >> 14;

    if ( !( mask & GLS_DEPTHMASK ) )
    {
        rmask |= DEPTHSTATE_FLAG_MASK;
    }

    assert( rmask < DEPTHSTATE_COUNT );
    assert( stencilPackage < STENCILPACKAGE_COUNT );

    return g_DrawState.depthStates.states[stencilPackage][rmask];
}

//----------------------------------------------------------------------------
// Get the blend state based on a mask
//----------------------------------------------------------------------------
ID3D11BlendState* GetBlendState( uint64 cmask, uint64 src, uint64 dst )
{
    dst >>= 3;
    cmask = ~( cmask & GLS_COLORALPHAMASK );

    int cindex = 0;
    if ( cmask ) 
    {
        if ( cmask & GLS_REDMASK )      cindex |= D3D11_COLOR_WRITE_ENABLE_RED;
        if ( cmask & GLS_GREENMASK )    cindex |= D3D11_COLOR_WRITE_ENABLE_GREEN;
        if ( cmask & GLS_BLUEMASK )     cindex |= D3D11_COLOR_WRITE_ENABLE_BLUE;
        if ( cmask & GLS_ALPHAMASK )    cindex |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    }

    ASSERT( cindex < COLORMASK_COUNT );
    ASSERT( src < BLENDSTATE_SRC_COUNT );
    ASSERT( dst < BLENDSTATE_DST_COUNT );
    return g_DrawState.blendStates.states[cindex][src][dst];
}

//----------------------------------------------------------------------------
// Get the blend state based on a mask
//----------------------------------------------------------------------------
ID3D11RasterizerState* GetRasterizerState( D3D11_CULL_MODE cullMode, uint64 rmask )
{
    ASSERT( cullMode > 0 && cullMode <= CULLMODE_COUNT );
    ASSERT( rmask < RASTERIZERSTATE_COUNT );
    return g_DrawState.rasterStates.states[cullMode-1][rmask];
}

//----------------------------------------------------------------------------
// Get the blend constants
//----------------------------------------------------------------------------
D3D11_BLEND GetSrcBlendConstant( int qConstant )
{
	switch ( qConstant )
	{
	case GLS_SRCBLEND_ZERO:
		return D3D11_BLEND_ZERO;
	case GLS_SRCBLEND_ONE:
		return D3D11_BLEND_ONE;
	case GLS_SRCBLEND_DST_COLOR:
		return D3D11_BLEND_DEST_COLOR;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
		return D3D11_BLEND_INV_DEST_COLOR;
	case GLS_SRCBLEND_SRC_ALPHA:
		return D3D11_BLEND_SRC_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
		return D3D11_BLEND_INV_SRC_ALPHA;
	case GLS_SRCBLEND_DST_ALPHA:
		return D3D11_BLEND_DEST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
		return D3D11_BLEND_INV_DEST_ALPHA;
    default:
        ASSERT(0);
        return D3D11_BLEND_ONE;
	}
}

D3D11_BLEND GetDestBlendConstant( int qConstant )
{
	switch ( qConstant )
	{
	case GLS_DSTBLEND_ZERO:
		return D3D11_BLEND_ZERO;
	case GLS_DSTBLEND_ONE:
		return D3D11_BLEND_ONE;
	case GLS_DSTBLEND_SRC_COLOR:
		return D3D11_BLEND_SRC_COLOR;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
		return D3D11_BLEND_INV_SRC_COLOR;
	case GLS_DSTBLEND_SRC_ALPHA:
		return D3D11_BLEND_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
		return D3D11_BLEND_INV_SRC_ALPHA;
	case GLS_DSTBLEND_DST_ALPHA:
		return D3D11_BLEND_DEST_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
		return D3D11_BLEND_INV_DEST_ALPHA;
    default:
        ASSERT(0);
        return D3D11_BLEND_ONE;
	}
}

D3D11_BLEND GetSrcBlendAlphaConstant( int qConstant )
{
	switch ( qConstant )
	{
	case GLS_SRCBLEND_ZERO:
		return D3D11_BLEND_ZERO;
	case GLS_SRCBLEND_ONE:
		return D3D11_BLEND_ONE;
	case GLS_SRCBLEND_DST_COLOR:
		return D3D11_BLEND_DEST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
		return D3D11_BLEND_INV_DEST_ALPHA;
	case GLS_SRCBLEND_SRC_ALPHA:
		return D3D11_BLEND_SRC_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
		return D3D11_BLEND_INV_SRC_ALPHA;
	case GLS_SRCBLEND_DST_ALPHA:
		return D3D11_BLEND_DEST_ALPHA;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
		return D3D11_BLEND_INV_DEST_ALPHA;
    default:
        ASSERT(0);
        return D3D11_BLEND_ONE;
	}
}

D3D11_BLEND GetDestBlendAlphaConstant( int qConstant )
{
	switch ( qConstant )
	{
	case GLS_DSTBLEND_ZERO:
		return D3D11_BLEND_ZERO;
	case GLS_DSTBLEND_ONE:
		return D3D11_BLEND_ONE;
	case GLS_DSTBLEND_SRC_COLOR:
		return D3D11_BLEND_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
		return D3D11_BLEND_INV_SRC_ALPHA;
	case GLS_DSTBLEND_SRC_ALPHA:
		return D3D11_BLEND_SRC_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
		return D3D11_BLEND_INV_SRC_ALPHA;
	case GLS_DSTBLEND_DST_ALPHA:
		return D3D11_BLEND_DEST_ALPHA;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
		return D3D11_BLEND_INV_DEST_ALPHA;
    default:
        ASSERT(0);
        return D3D11_BLEND_ONE;
	}
}

void InitRasterStates( d3dRasterStates_t* rs )
{
    memset( rs, 0, sizeof( d3dRasterStates_t ) );

    D3D11_RASTERIZER_DESC rd;
    ZeroMemory( &rd, sizeof( rd ) );
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    rd.DepthBiasClamp = 0;

    for ( int cullMode = 0; cullMode < CULLMODE_COUNT; ++cullMode )
    {
        rd.CullMode = (D3D11_CULL_MODE)( cullMode + 1 );

        for ( int rasterMode = 0; rasterMode < RASTERIZERSTATE_COUNT; ++rasterMode )
        {
            // Skip invalid poly offset modes
            if ((rasterMode & RASTERIZERSTATE_FLAG_POLY_OFFSET_MASK) == RASTERIZERSTATE_FLAG_POLY_OFFSET_MASK)
                continue;

            uint64 polyOffsetMode = rasterMode & RASTERIZERSTATE_FLAG_POLY_OFFSET_MASK;
            if ( polyOffsetMode == RASTERIZERSTATE_FLAG_POLY_OFFSET_DECAL ) {
                rd.DepthBias = r_offsetFactor.GetFloat();
                rd.SlopeScaledDepthBias = r_offsetUnits.GetFloat();
            } else if ( polyOffsetMode == RASTERIZERSTATE_FLAG_POLY_OFFSET_SHADOW ) {
                rd.DepthBias = r_shadowPolygonFactor.GetFloat();
                rd.SlopeScaledDepthBias = -r_shadowPolygonOffset.GetFloat();
            } else {
                rd.DepthBias = 0;
                rd.SlopeScaledDepthBias = 0;
            }

            if ( rasterMode & RASTERIZERSTATE_FLAG_POLY_OUTLINE ) {
                rd.FillMode = D3D11_FILL_WIREFRAME;
            } else {
                rd.FillMode = D3D11_FILL_SOLID;
            }

            g_pDevice->CreateRasterizerState( &rd, &rs->states[cullMode][rasterMode] );
        }
    }
}

void DestroyRasterStates( d3dRasterStates_t* rs )
{
    for ( int cullMode = 0; cullMode < CULLMODE_COUNT; ++cullMode )
    {
        for ( int rasterMode = 0; rasterMode < RASTERIZERSTATE_COUNT; ++rasterMode )
        {
            SAFE_RELEASE( rs->states[cullMode][rasterMode] );
        }
    }

    memset( rs, 0, sizeof( d3dRasterStates_t ) );
}

static void ConfigureDepthStencilState( D3D11_DEPTH_STENCIL_DESC* dsd, uint64 mask )
{
    ZeroMemory( dsd, sizeof( *dsd ) );

    uint64 depthFunc = mask & GLS_DEPTHFUNC_BITS;
    uint64 stencilPackage = mask & GLS_DEPTH_STENCIL_PACKAGE_BITS;

    dsd->DepthEnable = TRUE;

    if ( !( mask & GLS_DEPTHMASK ) ) {
        dsd->DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    }
    
    switch ( depthFunc )
    {
    case GLS_DEPTHFUNC_EQUAL:
        dsd->DepthFunc = D3D11_COMPARISON_EQUAL;
        break;
    case GLS_DEPTHFUNC_ALWAYS:
        dsd->DepthFunc = D3D11_COMPARISON_ALWAYS; 
        break;
    case GLS_DEPTHFUNC_LESS:
        dsd->DepthFunc = D3D11_COMPARISON_LESS_EQUAL; 
        break;
    case GLS_DEPTHFUNC_GREATER:
        dsd->DepthFunc = D3D11_COMPARISON_GREATER_EQUAL; 
        break;
    }

    if ( stencilPackage != GLS_DEPTH_STENCIL_PACKAGE_NONE )
    {
        dsd->StencilEnable = TRUE;
        dsd->StencilReadMask = 255; // @pjb: todo: Never anything else right now.
        dsd->StencilWriteMask = 255; // @pjb: todo: Never anything else right now.

        dsd->FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        dsd->BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

        switch ( stencilPackage )
        {
        case GLS_DEPTH_STENCIL_PACKAGE_REF_EQUAL:
            dsd->FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
            dsd->FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
            break;
        case GLS_DEPTH_STENCIL_PACKAGE_INC:
        case GLS_DEPTH_STENCIL_PACKAGE_Z:
            dsd->FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
            break;
        case GLS_DEPTH_STENCIL_PACKAGE_DEC:
            dsd->FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
            break;
        case GLS_DEPTH_STENCIL_PACKAGE_PRELOAD_Z:
            dsd->FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilFailOp = D3D11_STENCIL_OP_DECR;
            dsd->FrontFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
            break;
        case GLS_DEPTH_STENCIL_PACKAGE_TWO_SIDED:
            dsd->FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
            dsd->FrontFace.StencilPassOp = D3D11_STENCIL_OP_ZERO;
            break;
        }

        switch ( stencilPackage ) 
        {
        case GLS_DEPTH_STENCIL_PACKAGE_Z:
            dsd->BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->BackFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
            break;
        case GLS_DEPTH_STENCIL_PACKAGE_PRELOAD_Z:
            dsd->BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->BackFace.StencilFailOp = D3D11_STENCIL_OP_INCR;
            dsd->BackFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
            break;
        case GLS_DEPTH_STENCIL_PACKAGE_TWO_SIDED:
            dsd->BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
            dsd->BackFace.StencilFailOp = D3D11_STENCIL_OP_ZERO;
            dsd->BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
            break;
        default:
            dsd->BackFace = dsd->FrontFace;
            break;
        }
    }
}

static ID3D11DepthStencilState* CreateDepthStencilStateFromMask( uint64 stencilBits, uint64 depthBits )
{
    ID3D11DepthStencilState* state = nullptr;

    assert( stencilBits < STENCILPACKAGE_COUNT );
    assert( depthBits < DEPTHSTATE_COUNT );

    // Convert the mask to a GLS code
    uint64 gls = 
        ( stencilBits << 37 ) |
        ( depthBits & DEPTHSTATE_FUNC_MASK ) << 14;

    if ( !( depthBits & DEPTHSTATE_FLAG_MASK ) ) {
        gls |= GLS_DEPTHMASK; 
    }

    D3D11_DEPTH_STENCIL_DESC dsd;
    ConfigureDepthStencilState( &dsd, gls );

    g_pDevice->CreateDepthStencilState( &dsd, &state );
    if ( !state ) {
        common->FatalError( "Failed to create DepthStencilState of mask %x\n", gls );
    }

    return state;
}

void InitDepthStates( d3dDepthStates_t* ds )
{
    memset( ds, 0, sizeof( d3dDepthStates_t ) );

    for ( uint64 sp = 0; sp < STENCILPACKAGE_COUNT; ++sp )
    {
        for ( uint64 i = 0; i < DEPTHSTATE_COUNT; ++i )
        {
            ds->states[sp][i] = CreateDepthStencilStateFromMask( sp, i );
        }
    }
}

void DestroyDepthStates( d3dDepthStates_t* ds )
{
    for ( uint64 sp = 0; sp < STENCILPACKAGE_COUNT; ++sp )
    {
        for ( uint64 i = 0; i < DEPTHSTATE_COUNT; ++i )
        {
            SAFE_RELEASE( ds->states[sp][i] );
        }
    }

    memset( ds, 0, sizeof( d3dDepthStates_t ) );
}

void InitBlendStates( d3dBlendStates_t* bs )
{
    memset( bs, 0, sizeof( d3dBlendStates_t ) );

    //
    // Blend-mode matrix
    //
    D3D11_BLEND_DESC bsd;
    ZeroMemory( &bsd, sizeof( bsd ) );
    bsd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bsd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    for ( int cmask = 0; cmask < COLORMASK_COUNT; ++cmask )
    {
        bsd.RenderTarget[0].RenderTargetWriteMask = cmask;
        for ( int src = 0; src < BLENDSTATE_SRC_COUNT; ++src )
        {
            for ( int dst = 0; dst < BLENDSTATE_DST_COUNT; ++dst )
            {
                int qSrc = src;
                int qDst = dst << 3;
                bsd.RenderTarget[0].SrcBlend = GetSrcBlendConstant( qSrc );
                bsd.RenderTarget[0].DestBlend = GetDestBlendConstant( qDst );
                bsd.RenderTarget[0].SrcBlendAlpha = GetSrcBlendAlphaConstant( qSrc );
                bsd.RenderTarget[0].DestBlendAlpha = GetDestBlendAlphaConstant( qDst );

                // disable blending in the case of src one dst zero
                bsd.RenderTarget[0].BlendEnable = ( qSrc != GLS_SRCBLEND_ONE || qDst != GLS_DSTBLEND_ZERO );

                g_pDevice->CreateBlendState( &bsd, &bs->states[cmask][src][dst] );
            }
        }
    }
}

void DestroyBlendStates( d3dBlendStates_t* bs )
{
    for ( int c = 0; c < COLORMASK_COUNT; ++c )
    {
        for ( int src = 0; src < BLENDSTATE_SRC_COUNT; ++src )
        {
            for ( int dst = 0; dst < BLENDSTATE_DST_COUNT; ++dst )
            {
                SAFE_RELEASE( bs->states[c][src][dst] );
            }
        }
    }

    memset( bs, 0, sizeof( d3dBlendStates_t ) );
}

void D3DDrv_RegenerateStateBlocks()
{
    DestroyRasterStates( &g_DrawState.rasterStates );
    DestroyDepthStates( &g_DrawState.depthStates );
    DestroyBlendStates( &g_DrawState.blendStates );
    InitRasterStates( &g_DrawState.rasterStates );
    InitDepthStates( &g_DrawState.depthStates );
    InitBlendStates( &g_DrawState.blendStates );
}


//----------------------------------------------------------------------------
//
// ENTRY POINTS
//
//----------------------------------------------------------------------------

void InitDrawState()
{
    // Don't memset g_BufferState here.
    memset( &g_DrawState, 0, sizeof( g_DrawState ) );
    
    // Create D3D objects
    DestroyBuffers();
    CreateBuffers();

    InitRasterStates( &g_DrawState.rasterStates );
    InitDepthStates( &g_DrawState.depthStates );
    InitBlendStates( &g_DrawState.blendStates );

    // Set up some default state
    g_pImmediateContext->OMSetRenderTargets( 1, &g_BufferState.backBufferView, g_BufferState.depthBufferView );
    D3DDrv_SetViewport( g_pImmediateContext, 0, 0, g_BufferState.backBufferDesc.Width, g_BufferState.backBufferDesc.Height );

    // Clear the targets
    FLOAT clearCol[4] = { 0, 0, 0, 0 };
    g_pImmediateContext->ClearRenderTargetView( g_BufferState.backBufferView, clearCol );
    g_pImmediateContext->ClearDepthStencilView( g_BufferState.depthBufferView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0 );
    
    // Create the frame query
    SAFE_RELEASE( g_DrawState.frameQuery );
    D3D11_QUERY_DESC qd;
    ZeroMemory( &qd, sizeof( qd ) );
    qd.Query = D3D11_QUERY_EVENT;
    g_pDevice->CreateQuery( &qd, &g_DrawState.frameQuery );    
}

void DestroyDrawState()
{
    DestroyRasterStates( &g_DrawState.rasterStates );
    DestroyDepthStates( &g_DrawState.depthStates );
    DestroyBlendStates( &g_DrawState.blendStates );
    DestroyBuffers();

    memset( &g_DrawState, 0, sizeof( g_DrawState ) );
}