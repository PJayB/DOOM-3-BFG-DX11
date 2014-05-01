/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").  

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#pragma hdrstop
#include "../idlib/precompiled.h"

#include "tr_local.h"

idRenderProgManager renderProgManager;

/*
================================================================================================
idRenderProgManager::idRenderProgManager()
================================================================================================
*/
idRenderProgManager::idRenderProgManager() {
}

/*
================================================================================================
idRenderProgManager::~idRenderProgManager()
================================================================================================
*/
idRenderProgManager::~idRenderProgManager() {
}

/*
================================================================================================
R_ReloadShaders
================================================================================================
*/
static void R_ReloadShaders( const idCmdArgs &args ) {	
	renderProgManager.KillAllShaders();
	renderProgManager.LoadAllShaders();
}

/*
================================================================================================
idRenderProgManager::Init()
================================================================================================
*/
void idRenderProgManager::Init() {
	common->Printf( "----- Initializing Render Shaders -----\n" );

	vertexShaders.SetNum( MAX_BUILTIN_SHADERS );
	fragmentShaders.SetNum( MAX_BUILTIN_SHADERS );

	for ( int i = 0; i < MAX_BUILTIN_SHADERS; i++ ) {
        vertexShaders[i].pShader = nullptr;
        fragmentShaders[i].pShader = nullptr;
	}

	struct builtinShaders_t {
		int index;
		const char * name;
	};
    
    builtinShaders_t builtinVs[] = {
		{ BUILTIN_SHADER_GUI, "gui.vfp" },
		{ BUILTIN_SHADER_COLOR, "color.vfp" },
		{ BUILTIN_SHADER_TEXTURED, "texture.vfp" },
		{ BUILTIN_SHADER_TEXTURE_VERTEXCOLOR, "texture_color.vfp" },
		{ BUILTIN_SHADER_TEXTURE_VERTEXCOLOR_SKINNED, "texture_color_skinned.vfp" },
		{ BUILTIN_SHADER_TEXTURE_TEXGEN_VERTEXCOLOR, "texture_color_texgen.vfp" },
		{ BUILTIN_SHADER_INTERACTION, "interaction.vfp" },
		{ BUILTIN_SHADER_INTERACTION_SKINNED, "interaction_skinned.vfp" },
		{ BUILTIN_SHADER_INTERACTION_AMBIENT, "interactionAMBIENT.vfp" },
		{ BUILTIN_SHADER_INTERACTION_AMBIENT_SKINNED, "interactionAMBIENT_skinned.vfp" },
		{ BUILTIN_SHADER_ENVIRONMENT, "environment.vfp" },
		{ BUILTIN_SHADER_ENVIRONMENT_SKINNED, "environment_skinned.vfp" },
		{ BUILTIN_SHADER_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
		{ BUILTIN_SHADER_BUMPY_ENVIRONMENT_SKINNED, "bumpyEnvironment_skinned.vfp" },

		{ BUILTIN_SHADER_DEPTH, "depth.vfp" },
		{ BUILTIN_SHADER_DEPTH_SKINNED, "depth_skinned.vfp" },

		{ BUILTIN_SHADER_SKYBOX, "skybox.vfp" },
		{ BUILTIN_SHADER_WOBBLESKY, "wobblesky.vfp" },
		{ BUILTIN_SHADER_POSTPROCESS, "postprocess.vfp" },
		{ BUILTIN_SHADER_BINK, "bink.vfp" },
		{ BUILTIN_SHADER_BINK_GUI, "bink_gui.vfp" },
		{ BUILTIN_SHADER_MOTION_BLUR, "motionBlur.vfp" },
	};

    builtinShaders_t builtinFs[] = {
		{ BUILTIN_SHADER_GUI, "gui.vfp" },
		{ BUILTIN_SHADER_COLOR, "color.vfp" },
		{ BUILTIN_SHADER_TEXTURED, "texture.vfp" },
		{ BUILTIN_SHADER_TEXTURE_VERTEXCOLOR, "texture_color.vfp" },
		{ BUILTIN_SHADER_TEXTURE_TEXGEN_VERTEXCOLOR, "texture_color_texgen.vfp" },
		{ BUILTIN_SHADER_INTERACTION, "interaction.vfp" },
		{ BUILTIN_SHADER_INTERACTION_AMBIENT, "interactionAMBIENT.vfp" },
		{ BUILTIN_SHADER_ENVIRONMENT, "environment.vfp" },
		{ BUILTIN_SHADER_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
		{ BUILTIN_SHADER_DEPTH, "depth.vfp" },
		{ BUILTIN_SHADER_SKYBOX, "skybox.vfp" },
		{ BUILTIN_SHADER_WOBBLESKY, "wobblesky.vfp" },
		{ BUILTIN_SHADER_POSTPROCESS, "postprocess.vfp" },
		{ BUILTIN_SHADER_BINK, "bink.vfp" },
		{ BUILTIN_SHADER_BINK_GUI, "bink_gui.vfp" },
		{ BUILTIN_SHADER_MOTION_BLUR, "motionBlur.vfp" },
	};

    BUILTIN_SHADER builtinSkinned[] = {
        BUILTIN_SHADER_TEXTURE_VERTEXCOLOR_SKINNED,
        BUILTIN_SHADER_INTERACTION_SKINNED,
        BUILTIN_SHADER_INTERACTION_AMBIENT_SKINNED,
        BUILTIN_SHADER_ENVIRONMENT_SKINNED,
        BUILTIN_SHADER_BUMPY_ENVIRONMENT_SKINNED,
        BUILTIN_SHADER_DEPTH_SKINNED
    };

	int numBuiltinVs = sizeof( builtinVs ) / sizeof( builtinVs[0] );
	int numBuiltinFs = sizeof( builtinFs ) / sizeof( builtinFs[0] );
	for ( int i = 0; i < numBuiltinVs; i++ ) {
        int index = builtinVs[i].index;
		vertexShaders[index].name = builtinVs[i].name;
		LoadVertexShader( index );
	}

	for ( int i = 0; i < numBuiltinFs; i++ ) {
        int index = builtinFs[i].index;
		fragmentShaders[index].name = builtinFs[i].name;
		LoadFragmentShader( index );
	}

    for ( int i = 0; i < _countof(builtinSkinned); ++i ) {
        vertexShaders[builtinSkinned[i]].usesJoints = true;
    }

	cmdSystem->AddCommand( "reloadShaders", R_ReloadShaders, CMD_FL_RENDERER, "reloads shaders" );

    // Create the constant buffers
    InitConstantBuffer( &builtinCbuffer, RENDERPARM_TOTAL * sizeof(float) * 4 );
    InitConstantBuffer( &userCbuffer, RENDERPARM_USER_COUNT * sizeof(float) * 4 );
}

/*
================================================================================================
idRenderProgManager::LoadAllShaders()
================================================================================================
*/
void idRenderProgManager::LoadAllShaders() {
	for ( int i = 0; i < vertexShaders.Num(); i++ ) {
		LoadVertexShader( i );
	}
	for ( int i = 0; i < fragmentShaders.Num(); i++ ) {
		LoadFragmentShader( i );
	}
}

/*
================================================================================================
idRenderProgManager::KillAllShaders()
================================================================================================
*/
void idRenderProgManager::KillAllShaders() {
	for ( int i = 0; i < vertexShaders.Num(); i++ ) {
		SAFE_RELEASE( vertexShaders[i].pShader );
		Mem_Free( vertexShaders[i].pByteCode );
	}
	for ( int i = 0; i < fragmentShaders.Num(); i++ ) {
		SAFE_RELEASE( fragmentShaders[i].pShader );
	}
}

/*
================================================================================================
idRenderProgManager::Shutdown()
================================================================================================
*/
void idRenderProgManager::Shutdown() {
	KillAllShaders();

    DestroyConstantBuffer( &builtinCbuffer );
    DestroyConstantBuffer( &userCbuffer );
}

/*
================================================================================================
idRenderProgManager::FindVertexShader
================================================================================================
*/
int idRenderProgManager::FindVertexShader( const char * name ) {
	for ( int i = 0; i < vertexShaders.Num(); i++ ) {
		if ( vertexShaders[i].name.Icmp( name ) == 0 ) {
			LoadVertexShader( i );
			return i;
		}
	}
	vertexShader_t shader;
	shader.name = name;
	int index = vertexShaders.Append( shader );
	LoadVertexShader( index );

	// FIXME: we should really scan the program source code for using rpEnableSkinning but at this
	// point we directly load a binary and the program source code is not available on the consoles
	if (	idStr::Icmp( name, "heatHaze.vfp" ) == 0 ||
			idStr::Icmp( name, "heatHazeWithMask.vfp" ) == 0 ||
			idStr::Icmp( name, "heatHazeWithMaskAndVertex.vfp" ) == 0 ) {
		vertexShaders[index].usesJoints = true;
		vertexShaders[index].optionalSkinning = true;
	}

	return index;
}

/*
================================================================================================
idRenderProgManager::FindFragmentShader
================================================================================================
*/
int idRenderProgManager::FindFragmentShader( const char * name ) {
	for ( int i = 0; i < fragmentShaders.Num(); i++ ) {
		if ( fragmentShaders[i].name.Icmp( name ) == 0 ) {
			LoadFragmentShader( i );
			return i;
		}
	}
	fragmentShader_t shader;
	shader.name = name;
	int index = fragmentShaders.Append( shader );
	LoadFragmentShader( index );
	return index;
}


/*
================================================================================================
idRenderProgManager::LoadShaderBlob
================================================================================================
*/
int idRenderProgManager::LoadShaderBlob( const char* name, void** ppOut, shaderType_t shaderType ) const {
    idStr inFile;
    inFile.Format( "compiled_shaders\\%s", name );
    inFile.StripFileExtension();
    
    switch (shaderType) {
    case SHADERTYPE_VERTEX:
        inFile += ".vertex.cso";
        break;
    case SHADERTYPE_PIXEL:
        inFile += ".pixel.cso";
        break;
    }

    return fileSystem->ReadFile( inFile.c_str(), ppOut, nullptr );
}

/*
================================================================================================
idRenderProgManager::LoadVertexShader
================================================================================================
*/
void idRenderProgManager::LoadVertexShader( int index ) {

    vertexShader_t* vshader = &vertexShaders[index];

	if ( vshader->pShader != NULL || vshader->name.Size() == 0 ) {
		return; // Already loaded or not enabled
	}
	
    void* blob = nullptr;
    int blobLen = LoadShaderBlob( vshader->name, &blob, SHADERTYPE_VERTEX );
    if ( blobLen == -1 ) {
        common->FatalError( "Couldn't load vertex shader '%s'", vshader->name.c_str() );
    }

    auto device = D3DDrv_GetDevice();

    vshader->pShader = nullptr;
    HRESULT hr = device->CreateVertexShader( 
        blob,
        blobLen,
        nullptr,
        &vshader->pShader );
    if ( FAILED( hr ) ) {
        common->FatalError( "Failed to create vertex shader '%s': %08X", vshader->name, hr );
    }

    vshader->pByteCode = blob;
    vshader->ByteCodeSize = blobLen;
}

/*
================================================================================================
idRenderProgManager::LoadFragmentShader
================================================================================================
*/
void idRenderProgManager::LoadFragmentShader( int index ) {
    fragmentShader_t* pshader = &fragmentShaders[index];

	if ( fragmentShaders[index].pShader != NULL || pshader->name.Size() == 0 ) {
		return; // Already loaded or not enabled
	}
	
    void* blob = nullptr;
    int blobLen = LoadShaderBlob( pshader->name, &blob, SHADERTYPE_PIXEL );
    if ( blobLen == -1 ) {
        common->FatalError( "Couldn't load pixel shader '%s'", pshader->name.c_str() );
    }

    auto device = D3DDrv_GetDevice();

    pshader->pShader = nullptr;
    HRESULT hr = device->CreatePixelShader( 
        blob,
        blobLen,
        nullptr,
        &pshader->pShader );
    if ( FAILED( hr ) ) {
        common->FatalError( "Failed to create pixel shader '%s': %08X", pshader->name, hr );
    }
}

/*
================================================================================================
idRenderProgManager::SetRenderParms
================================================================================================
*/
void idRenderProgManager::SetRenderParms( renderParm_t parm, const float * value, int num ) {
    int i = parm;
    size_t size = sizeof(float) * 4 * num;
    cbufferInfo_t* cbuffer;
    if ( i >= RENDERPARM_USER ) {
        cbuffer = &userCbuffer;
        i -= RENDERPARM_USER;
    } else {
        cbuffer = &builtinCbuffer;
    }

    assert( sizeof(float) * 4 * i + size <= cbuffer->size );
    memcpy( cbuffer->pData + 4 * i, value, size );
    cbuffer->dirty = true;
}

/*
================================================================================================
idRenderProgManager::SetRenderParm
================================================================================================
*/
void idRenderProgManager::SetRenderParm( renderParm_t parm, const float * value ) {
    int i = parm;
    size_t size = sizeof(float) * 4;
    cbufferInfo_t* cbuffer;
    if ( i >= RENDERPARM_USER ) {
        cbuffer = &userCbuffer;
        i -= RENDERPARM_USER;
    } else {
        cbuffer = &builtinCbuffer;
    }

    assert( sizeof(float) * 4 * i + size <= cbuffer->size );
    memcpy( cbuffer->pData + 4 * i, value, size );
    cbuffer->dirty = true;
}

/*
================================================================================================
idRenderProgManager::UpdateConstantBuffer
================================================================================================
*/
void idRenderProgManager::UpdateConstantBuffer( cbufferInfo_t* cbuffer, ID3D11DeviceContext1* pContext )
{
    D3D11_MAPPED_SUBRESOURCE map;
    pContext->Map( cbuffer->pBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map );
    memcpy( map.pData, cbuffer->pData, cbuffer->size );
    pContext->Unmap( cbuffer->pBuffer, 0 );
    cbuffer->dirty = false;
}

/*
================================================================================================
idRenderProgManager::UpdateConstantBuffers
================================================================================================
*/
void idRenderProgManager::InitConstantBuffer( cbufferInfo_t* cbuffer, size_t size )
{
    cbuffer->dirty = true;
    cbuffer->size = sizeof(float) * 4 * RENDERPARM_TOTAL;
    cbuffer->pData = new float[cbuffer->size / sizeof(float)];

    memset( cbuffer->pData, 0, cbuffer->size );

    cbuffer->pBuffer = QD3D::CreateDynamicBuffer( 
        D3DDrv_GetDevice(),
        D3D11_BIND_CONSTANT_BUFFER,
        cbuffer->size );
}

/*
================================================================================================
idRenderProgManager::DestroyConstantBuffer
================================================================================================
*/
void idRenderProgManager::DestroyConstantBuffer( cbufferInfo_t* cbuffer )
{
    SAFE_RELEASE( cbuffer->pBuffer );
    SAFE_DELETE_ARRAY( cbuffer->pData );
}

/*
================================================================================================
idRenderProgManager::UpdateConstantBuffers
================================================================================================
*/
void idRenderProgManager::UpdateConstantBuffers( ID3D11DeviceContext1* pContext )
{
    if ( builtinCbuffer.dirty )
        UpdateConstantBuffer( &builtinCbuffer, pContext );
    if ( userCbuffer.dirty )
        UpdateConstantBuffer( &userCbuffer, pContext );
}
