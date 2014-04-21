#include "d3d_common.h"
#include "d3d_state.h"
#include "d3d_image.h"
#include "d3d_shaders.h"
#include "d3d_drawdata.h"
#include "d3d_driver.h"

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
void CommitRasterizerState( int cullType, qboolean polyOffset, qboolean outline )
{
    int maskBits = 
        ( ( outline & 1 ) << 5 ) |
        ( ( polyOffset & 1 ) << 4 ) |
        ( ( backEnd.viewParms.isMirror & 1 ) << 3 ) | 
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
			if ( backEnd.viewParms.isMirror )
			{
                cullMode = D3D11_CULL_FRONT;
			}
			else
			{
                cullMode = D3D11_CULL_BACK;
			}
		}
		else
		{
			if ( backEnd.viewParms.isMirror )
			{
                cullMode = D3D11_CULL_BACK;
			}
			else
			{
                cullMode = D3D11_CULL_FRONT;
			}
		}
	}

    int rasterFlags = 0;
    if ( polyOffset ) { rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OFFSET; }
    if ( outline ) { rasterFlags |= RASTERIZERSTATE_FLAG_POLY_OUTLINE; }

    g_pImmediateContext->RSSetState( GetRasterizerState( cullMode, rasterFlags ) );
}

//----------------------------------------------------------------------------
// Set the culling mode depending on whether it's a mirror or not
//----------------------------------------------------------------------------
void D3DDrv_SetState( unsigned long stateBits )
{
	unsigned long diff = stateBits ^ g_RunState.stateMask;

	if ( !diff )
	{
		return;
	}

    // TODO: polymode: line
    
    //float blendFactor[4] = {0, 0, 0, 0};
    //g_pImmediateContext->OMSetDepthStencilState( g_DrawState.depthStates.none, 0 );
    //g_pImmediateContext->RSSetState( g_DrawState.rasterStates.cullNone );
    //g_pImmediateContext->OMSetBlendState( g_DrawState.blendStates.opaque, blendFactor, ~0U );

    unsigned long newDepthStateMask = 0;
	if ( stateBits & GLS_DEPTHFUNC_EQUAL )
	{
        newDepthStateMask |= DEPTHSTATE_FLAG_EQUAL;
    }
    if ( stateBits & GLS_DEPTHMASK_TRUE )
    {
        newDepthStateMask |= DEPTHSTATE_FLAG_MASK;
    }
    if ( !( stateBits & GLS_DEPTHTEST_DISABLE ) )
    {
        newDepthStateMask |= DEPTHSTATE_FLAG_TEST;
    }

    if ( newDepthStateMask != g_RunState.depthStateMask )
    {
        g_pImmediateContext->OMSetDepthStencilState( GetDepthState( newDepthStateMask ), 0 );
        g_RunState.depthStateMask = newDepthStateMask;
    }

	if ( diff & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) )
	{
		int srcFactor = stateBits & GLS_SRCBLEND_BITS;
        int dstFactor = stateBits & GLS_DSTBLEND_BITS; 

        float blendFactor[4] = {0, 0, 0, 0};
        g_pImmediateContext->OMSetBlendState( 
            GetBlendState( srcFactor, dstFactor ),
            blendFactor,
            ~0U );
    }

    //
    // In our shader we need to convert these operations:
    //  pass if > 0
    //  pass if < 0.5
    //  pass if >= 0.5
    // to: 
    //  fail if <= 0
    //  fail if >= 0.5
    //  fail if < 0.5
    // clip() will kill any alpha < 0.
    if ( diff & GLS_ATEST_BITS ) 
    {
        const float alphaEps = 0.00001f; // @pjb: HACK HACK HACK
        switch ( stateBits & GLS_ATEST_BITS )
        {
        case 0:
            g_RunState.psConstants.alphaClip[0] = 1;
            g_RunState.psConstants.alphaClip[1] = 0;
            break;
        case GLS_ATEST_GT_0:
            g_RunState.psConstants.alphaClip[0] = 1;
            g_RunState.psConstants.alphaClip[1] = alphaEps;
            break;
        case GLS_ATEST_LT_80:
            g_RunState.psConstants.alphaClip[0] = -1;
            g_RunState.psConstants.alphaClip[1] = 0.5f;
            break;
        case GLS_ATEST_GE_80:
            g_RunState.psConstants.alphaClip[0] = 1;
            g_RunState.psConstants.alphaClip[1] = 0.5f;
            break;
        default:
            ASSERT(0);
            break;
        }

        g_RunState.psDirtyConstants = qtrue;
    }

    g_RunState.stateMask = stateBits;
}

//----------------------------------------------------------------------------
// Get the depth stencil state based on a mask
//----------------------------------------------------------------------------
ID3D11DepthStencilState* GetDepthState( unsigned long mask )
{
    ASSERT( mask < 8 );
    return g_DrawState.depthStates.states[mask];
}

//----------------------------------------------------------------------------
// Get the blend state based on a mask
//----------------------------------------------------------------------------
ID3D11BlendState* GetBlendState( int src, int dst )
{
    // Special-case zero
    if ( src == 0 && dst == 0 )
        return g_DrawState.blendStates.opaque;
    else 
    {
        src--;
        dst = (dst >> 4) - 1;
        ASSERT( src < BLENDSTATE_SRC_COUNT );
        ASSERT( dst < BLENDSTATE_DST_COUNT );
        return g_DrawState.blendStates.states[src][dst];
    }
}

//----------------------------------------------------------------------------
// Get the blend state based on a mask
//----------------------------------------------------------------------------
ID3D11RasterizerState* GetRasterizerState( D3D11_CULL_MODE cullMode, unsigned long rmask )
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
	case GLS_SRCBLEND_ALPHA_SATURATE:
		return D3D11_BLEND_SRC_ALPHA_SAT;
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
	case GLS_SRCBLEND_ALPHA_SATURATE:
		return D3D11_BLEND_SRC_ALPHA_SAT;
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

//----------------------------------------------------------------------------
//
// ENTRY POINTS
//
//----------------------------------------------------------------------------

void InitDrawState()
{
    // Don't memset g_BufferState here.
    Com_Memset( &g_RunState, 0, sizeof( g_RunState ) );
    Com_Memset( &g_DrawState, 0, sizeof( g_DrawState ) );

    // Set up default state
    Com_Memcpy( g_RunState.vsConstants.modelViewMatrix, s_identityMatrix, sizeof(float) * 16 );
    Com_Memcpy( g_RunState.vsConstants.projectionMatrix, s_identityMatrix, sizeof(float) * 16 );
    g_RunState.vsConstants.depthRange[0] = 0;
    g_RunState.vsConstants.depthRange[1] = 1;
    g_RunState.stateMask = 0;
    g_RunState.vsDirtyConstants = qtrue;
    g_RunState.psDirtyConstants = qtrue;
    g_RunState.cullMode = -1;
    
    // Create D3D objects
    DestroyBuffers();
    CreateBuffers();
    InitImages();
    InitShaders();

    InitQuadRenderData( &g_DrawState.quadRenderData );
    InitSkyBoxRenderData( &g_DrawState.skyBoxRenderData );
    InitViewRenderData( &g_DrawState.viewRenderData );
    InitGenericStageRenderData( &g_DrawState.genericStage );
    InitTessBuffers( &g_DrawState.tessBufs );
    InitRasterStates( &g_DrawState.rasterStates );
    InitDepthStates( &g_DrawState.depthStates );
    InitBlendStates( &g_DrawState.blendStates );

    // Set up some default state
    g_pImmediateContext->OMSetRenderTargets( 1, &g_BufferState.backBufferView, g_BufferState.depthBufferView );
    D3DDrv_SetViewport( 0, 0, g_BufferState.backBufferDesc.Width, g_BufferState.backBufferDesc.Height );
    D3DDrv_SetState( GLS_DEFAULT );

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
    DestroyTessBuffers( &g_DrawState.tessBufs );
    DestroyGenericStageRenderData( &g_DrawState.genericStage );
    DestroyQuadRenderData( &g_DrawState.quadRenderData );
    DestroySkyBoxRenderData( &g_DrawState.skyBoxRenderData );
    DestroyViewRenderData( &g_DrawState.viewRenderData );
    DestroyShaders();
    DestroyImages();
    DestroyBuffers();

    Com_Memset( &g_RunState, 0, sizeof( g_RunState ) );
    Com_Memset( &g_DrawState, 0, sizeof( g_DrawState ) );
}

