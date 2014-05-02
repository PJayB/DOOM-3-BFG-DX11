#pragma hdrstop
#include "../idlib/precompiled.h"
#include "RenderProgs.h"

ID3D11InputLayout* idLayoutManager::idVertexLayout<idDrawVert>::s_layout = nullptr;
ID3D11InputLayout* idLayoutManager::idVertexLayout<idShadowVert>::s_layout = nullptr;
ID3D11InputLayout* idLayoutManager::idVertexLayout<idShadowVertSkinned>::s_layout = nullptr;

ID3D11InputLayout* CreateDrawVertInputLayout()
{
    D3D11_INPUT_ELEMENT_DESC els[] = 
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,   0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT,      0, 12,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL"  , 0, DXGI_FORMAT_R8G8B8A8_UNORM,    0, 16,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT" , 0, DXGI_FORMAT_R8G8B8A8_UNORM,    0, 20,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR"   , 0, DXGI_FORMAT_R8G8B8A8_UNORM,    0, 24,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR"   , 1, DXGI_FORMAT_R8G8B8A8_UNORM,    0, 28,  D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    const void* pBlob = nullptr;
    uint blobSize = 0;
    renderProgManager.GetBuiltInVertexShaderByteCode( 
        BUILTIN_SHADER_BUMPY_ENVIRONMENT_SKINNED, 
        &pBlob, 
        &blobSize );
    assert( pBlob );
    assert( blobSize );

    ID3D11InputLayout* layout = nullptr;
    D3DDrv_GetDevice()->CreateInputLayout(
        els,
        _countof(els),
        pBlob,
        blobSize,
        &layout );
    assert(layout);

    return layout;
}

ID3D11InputLayout* CreateShadowVertInputLayout()
{
    D3D11_INPUT_ELEMENT_DESC els[] = 
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, SHADOWVERT_XYZW_OFFSET, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    const void* pBlob = nullptr;
    uint blobSize = 0;
    renderProgManager.GetBuiltInVertexShaderByteCode( 
        BUILTIN_SHADER_SHADOW, 
        &pBlob, 
        &blobSize );
    assert( pBlob );
    assert( blobSize );

    ID3D11InputLayout* layout = nullptr;
    D3DDrv_GetDevice()->CreateInputLayout(
        els,
        _countof(els),
        pBlob,
        blobSize,
        &layout );
    assert(layout);

    return layout;
}

ID3D11InputLayout* CreateShadowVertSkinnedInputLayout()
{
    D3D11_INPUT_ELEMENT_DESC els[] = 
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, SHADOWVERTSKINNED_XYZW_OFFSET,    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR"   , 0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, SHADOWVERTSKINNED_COLOR_OFFSET,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR"   , 1, DXGI_FORMAT_R8G8B8A8_UNORM,     0, SHADOWVERTSKINNED_COLOR2_OFFSET,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    const void* pBlob = nullptr;
    uint blobSize = 0;
    renderProgManager.GetBuiltInVertexShaderByteCode( 
        BUILTIN_SHADER_BUMPY_ENVIRONMENT_SKINNED, 
        &pBlob, 
        &blobSize );
    assert( pBlob );
    assert( blobSize );

    ID3D11InputLayout* layout = nullptr;
    D3DDrv_GetDevice()->CreateInputLayout(
        els,
        _countof(els),
        pBlob,
        blobSize,
        &layout );
    assert(layout);

    return layout;
}

void idLayoutManager::Init()
{
    idVertexLayout<idDrawVert>::s_layout = CreateDrawVertInputLayout();
    idVertexLayout<idShadowVert>::s_layout = CreateShadowVertInputLayout();
    idVertexLayout<idShadowVertSkinned>::s_layout = CreateShadowVertSkinnedInputLayout();
}

void idLayoutManager::Shutdown()
{
    SAFE_RELEASE( idVertexLayout<idDrawVert>::s_layout );
    SAFE_RELEASE( idVertexLayout<idShadowVert>::s_layout );
    SAFE_RELEASE( idVertexLayout<idShadowVertSkinned>::s_layout );
}

