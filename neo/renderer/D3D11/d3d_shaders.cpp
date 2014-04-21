#include "d3d_common.h"
#include "d3d_shaders.h"
#include "d3d_state.h"

#define HLSL_PATH "hlsl"
#define HLSL_EXTENSION "cso"

ID3D11PixelShader* LoadPixelShader( const char* name )
{
    void* blob = nullptr;
    int len = FS_ReadFile( va( "%s/%s.%s", HLSL_PATH, name, HLSL_EXTENSION ), &blob );
    if ( !len || !blob  ) {
        ri.Error( ERR_FATAL, "Failed to load pixel shader '%s'\n", name );
    }

    ID3D11PixelShader* shader = nullptr;
    HRESULT hr = g_pDevice->CreatePixelShader(
        blob,
        len,
        nullptr,
        &shader );
    if ( FAILED( hr ) ) {
        ri.Error( ERR_FATAL, "Failed to load pixel shader '%s': 0x%08X\n", name, hr );
    }

    FS_FreeFile( blob );

    return shader;
}

ID3D11VertexShader* LoadVertexShader( const char* name, d3dVertexShaderBlob_t* blobOut )
{
    void* blob = nullptr;
    int len = FS_ReadFile( va( "%s/%s.%s", HLSL_PATH, name, HLSL_EXTENSION ), &blob );
    if ( !len || !blob ) {
        ri.Error( ERR_FATAL, "Failed to load vertex shader '%s'\n", name );
    }

    ID3D11VertexShader* shader = nullptr;
    HRESULT hr = g_pDevice->CreateVertexShader(
        blob,
        len,
        nullptr,
        &shader );
    if ( FAILED( hr ) ) {
        ri.Error( ERR_FATAL, "Failed to create vertex shader '%s': 0x%08X\n", name, hr );
    }

    if ( blobOut )
    {
        blobOut->blob = blob;
        blobOut->len = len;
    }
    else
    {
        FS_FreeFile( blob );
    }

    return shader;
}

void InitShaders()
{

}

void DestroyShaders()
{

}
