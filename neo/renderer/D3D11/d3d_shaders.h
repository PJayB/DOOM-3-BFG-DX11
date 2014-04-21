#pragma once

// @pjb: holds all the relevant d3d shaders

void InitShaders();
void DestroyShaders();

struct d3dVertexShaderBlob_t
{
    void* blob;
    int len;
};

ID3D11PixelShader* LoadPixelShader( const char* name );
ID3D11VertexShader* LoadVertexShader( const char* name, d3dVertexShaderBlob_t* blobOut );
