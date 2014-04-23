#pragma hdrstop
#include "../../idlib/precompiled.h"
#include "../tr_local.h"
#include "../../framework/Common_local.h"

#include "d3d_common.h"
#include "d3d_state.h"
#include "d3d_backend.h"

extern const float s_identityMatrix[16];

//----------------------------------------------------------------------------
// Locals
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Globals
//----------------------------------------------------------------------------
d3dBackBufferState_t g_BufferState;
d3dRunState_t g_RunState;
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
// Set the culling mode depending on whether it's a mirror or not
//----------------------------------------------------------------------------
void CommitRasterizerState( int cullType, bool polyOffset, bool outline )
{
    polyOffset |= g_RunState.polyOffsetEnabled;
    outline |= g_RunState.lineMode;

    int maskBits = 
        ( ( outline & 1 ) << 5 ) |
        ( ( polyOffset & 1 ) << 4 ) |
        ( cullType & 3 );

	if ( g_RunState.cullMode == maskBits ) {
		return;
	}

	g_RunState.cullMode = maskBits;

    // Resolve which direction we're culling in
    D3D11_CULL_MODE cullMode = D3D11_CULL_NONE;
	if ( cullType != CT_TWO_SIDED ) 
    {
		if ( cullType == CT_BACK_SIDED )
		{
			cullMode = D3D11_CULL_BACK;
		}
		else
		{
			cullMode = D3D11_CULL_FRONT;
		}
	}

    int rasterFlags = 0;
    if ( polyOffset ) { rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OFFSET; }
    if ( outline ) { rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OUTLINE; }

    g_pImmediateContext->RSSetState( GetRasterizerState( cullMode, rasterFlags ) );
}

//----------------------------------------------------------------------------
// Set the scissor rect
//----------------------------------------------------------------------------
void D3DDrv_SetScissor( int left, int top, int width, int height )
{
    RECT r = { left, top, left + width, top + height };
    g_pImmediateContext->RSSetScissorRects( 1, &r );
}

//----------------------------------------------------------------------------
// Set the viewport
//----------------------------------------------------------------------------
void D3DDrv_SetViewport( int left, int top, int width, int height )
{
    D3D11_VIEWPORT viewport;
    viewport.TopLeftX = __max( 0, left );
    viewport.TopLeftY = __max( 0, (int)g_BufferState.backBufferDesc.Height - top - height );
    viewport.Width = __min( (int)g_BufferState.backBufferDesc.Width - viewport.TopLeftX, width );
    viewport.Height = __min( (int)g_BufferState.backBufferDesc.Height - viewport.TopLeftY, height );
    viewport.MinDepth = 0;
    viewport.MaxDepth = 1;
    g_pImmediateContext->RSSetViewports( 1, &viewport );
}

//----------------------------------------------------------------------------
// Set the default state
//----------------------------------------------------------------------------
void D3DDrv_SetDefaultState()
{
    CommitRasterizerState( CT_FRONT_SIDED, false, false );
}

//----------------------------------------------------------------------------
// Get the depth state based on the mask (no stencil)
//----------------------------------------------------------------------------
ID3D11DepthStencilState* D3DDrv_GetDepthState( uint64 stateBits )
{
    // Reject anything with stencil
    if ( stateBits & GLS_STENCIL_FUNC_BITS )
        return nullptr;

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
void D3DDrv_SetRasterizerOptions( uint64 stateBits )
{
	//
	// fill/line mode
	//
	if ( stateBits & GLS_POLYMODE_LINE ) 
    {
		g_RunState.lineMode = ( stateBits & GLS_POLYMODE_LINE ) != 0;
	}

	//
	// polygon offset
	//
    bool shouldEnable = ( stateBits & GLS_POLYGON_OFFSET ) != 0;
    bool alreadyEnabled = ( stateBits & GLS_POLYGON_OFFSET ) != 0;
	if ( shouldEnable != alreadyEnabled ) {
		g_RunState.polyOffsetEnabled = shouldEnable;
	    g_RunState.polyOffset[0] = backEnd.glState.polyOfsScale;
        g_RunState.polyOffset[1] = backEnd.glState.polyOfsBias;
	}
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
// Get the depth stencil state based on a mask
//----------------------------------------------------------------------------
ID3D11DepthStencilState* GetDepthState( uint64 mask )
{
    ASSERT( mask < DEPTHSTATE_COUNT );

    uint64 rmask = ( mask & GLS_DEPTHFUNC_BITS ) >> 13;

    assert( rmask <= DEPTHSTATE_FUNC_MASK );

    if ( !( mask & GLS_DEPTHMASK ) )
    {
        rmask |= DEPTHSTATE_FLAG_MASK;
    }

    return g_DrawState.depthStates.states[rmask];
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
            if ( rasterMode & RASTERIZERSTATE_FLAG_POLY_OFFSET ) {
                rd.DepthBias = r_offsetFactor.GetFloat();
                rd.SlopeScaledDepthBias = r_offsetUnits.GetFloat();
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
    uint64 stencilFunc = mask & GLS_STENCIL_FUNC_BITS;

    dsd->DepthEnable = TRUE;
    dsd->StencilEnable = stencilFunc != 0;

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

    // @pjb: TODO: stencil
}

static ID3D11DepthStencilState* CreateDepthStencilStateFromMask( uint64 mask )
{
    ID3D11DepthStencilState* state = nullptr;

    // Convert the mask to a GLS code
    uint64 gls = ( mask & DEPTHSTATE_FUNC_MASK ) << 13;

    if ( !( mask & DEPTHSTATE_FLAG_MASK ) ) {
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

    for ( uint64 i = 0; i < _countof( ds->states ); ++i )
    {
        ds->states[i] = CreateDepthStencilStateFromMask( i );
    }
}

void DestroyDepthStates( d3dDepthStates_t* ds )
{
    for ( int i = 0; i < _countof( ds->states ); ++i )
    {
        SAFE_RELEASE( ds->states[i] );
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

//----------------------------------------------------------------------------
//
// ENTRY POINTS
//
//----------------------------------------------------------------------------

void InitDrawState()
{
    // Don't memset g_BufferState here.
    memset( &g_RunState, 0, sizeof( g_RunState ) );
    memset( &g_DrawState, 0, sizeof( g_DrawState ) );

    // Set up default state
    g_RunState.vsDirtyConstants = true;
    g_RunState.psDirtyConstants = true;
    g_RunState.cullMode = -1;
    
    // Create D3D objects
    DestroyBuffers();
    CreateBuffers();

    InitRasterStates( &g_DrawState.rasterStates );
    InitDepthStates( &g_DrawState.depthStates );
    InitBlendStates( &g_DrawState.blendStates );

    // Set up some default state
    g_pImmediateContext->OMSetRenderTargets( 1, &g_BufferState.backBufferView, g_BufferState.depthBufferView );
    D3DDrv_SetViewport( 0, 0, g_BufferState.backBufferDesc.Width, g_BufferState.backBufferDesc.Height );
    D3DDrv_SetDefaultState();

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

    memset( &g_RunState, 0, sizeof( g_RunState ) );
    memset( &g_DrawState, 0, sizeof( g_DrawState ) );
}

