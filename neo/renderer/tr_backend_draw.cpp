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
#include "../../framework/Common_local.h"

idCVar r_drawEyeColor( "r_drawEyeColor", "0", CVAR_RENDERER | CVAR_BOOL, "Draw a colored box, red = left eye, blue = right eye, grey = non-stereo" );
idCVar r_motionBlur( "r_motionBlur", "0", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "1 - 5, log2 of the number of motion blur samples" );
idCVar r_forceZPassStencilShadows( "r_forceZPassStencilShadows", "0", CVAR_RENDERER | CVAR_BOOL, "force Z-pass rendering for performance testing" );
idCVar r_useStencilShadowPreload( "r_useStencilShadowPreload", "1", CVAR_RENDERER | CVAR_BOOL, "use stencil shadow preload algorithm instead of Z-fail" );
idCVar r_skipShaderPasses( "r_skipShaderPasses", "0", CVAR_RENDERER | CVAR_BOOL, "" );
idCVar r_skipInteractionFastPath( "r_skipInteractionFastPath", "1", CVAR_RENDERER | CVAR_BOOL, "" );
idCVar r_useLightStencilSelect( "r_useLightStencilSelect", "0", CVAR_RENDERER | CVAR_BOOL, "use stencil select pass" );

backEndState_t	backEnd;

/*
================
RB_ResetColor
================
*/
void RB_ResetColor()
{
    static const float c[] = { 1, 1, 1, 1 };
    renderProgManager.SetRenderParm( RENDERPARM_COLOR, c );
}

/*
================
RB_BindImages
================
*/
void RB_BindImages( ID3D11DeviceContext2* pContext, idImage** pImages, int offset, int numImages )
{
    ID3D11ShaderResourceView* pSRVs[16];
    ID3D11SamplerState* pSamplers[16];

    assert( numImages < _countof( pSRVs ) );

    for ( int i = 0; i < numImages; ++i )
    {
        ID3D11ShaderResourceView* srv = pImages[i]->GetSRV();

        // If it's null, get the default image
        // @pjb: todo: this will b0rk if it's supposed to be a normal map or something
        if ( srv == nullptr )
            srv = globalImages->defaultImage->GetSRV();

        pSRVs[i] = srv;
        pSamplers[i] = pImages[i]->GetSampler();
    }

    pContext->PSSetShaderResources( offset, numImages, pSRVs );
    pContext->PSSetSamplers( offset, numImages, pSamplers );
}

/*
================
RB_SetMVP
================
*/
void RB_SetMVP( const idRenderMatrix & mvp ) { 
	renderProgManager.SetRenderParms( RENDERPARM_MVPMATRIX_X, mvp[0], 4 );
}

/*
================
RB_SetVertexColorParms
================
*/
static void RB_SetVertexColorParms( stageVertexColor_t svc ) {
    static const float zero[4] = { 0, 0, 0, 0 };
    static const float one[4] = { 1, 1, 1, 1 };
    static const float negOne[4] = { -1, -1, -1, -1 };

	switch ( svc ) {
		case SVC_IGNORE:
			renderProgManager.SetRenderParm( RENDERPARM_VERTEXCOLOR_MODULATE, zero );
			renderProgManager.SetRenderParm( RENDERPARM_VERTEXCOLOR_ADD, one );
			break;
		case SVC_MODULATE:
			renderProgManager.SetRenderParm( RENDERPARM_VERTEXCOLOR_MODULATE, one );
			renderProgManager.SetRenderParm( RENDERPARM_VERTEXCOLOR_ADD, zero );
			break;
		case SVC_INVERSE_MODULATE:
			renderProgManager.SetRenderParm( RENDERPARM_VERTEXCOLOR_MODULATE, negOne );
			renderProgManager.SetRenderParm( RENDERPARM_VERTEXCOLOR_ADD, one );
			break;
	}
}

/*
================
RB_DrawElementsWithCounters
================
*/
void RB_DrawElementsWithCounters( ID3D11DeviceContext2* pContext, const drawSurf_t *surf ) {
    // get vertex buffer
	const vertCacheHandle_t vbHandle = surf->ambientCache;
	idVertexBuffer * vertexBuffer;
	if ( vertexCache.CacheIsStatic( vbHandle ) ) {
		vertexBuffer = &vertexCache.staticData.vertexBuffer;
	} else {
		const uint64 frameNum = (int)( vbHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
		if ( frameNum != ( ( vertexCache.currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
			idLib::Warning( "RB_DrawElementsWithCounters, vertexBuffer == NULL" );
			return;
		}
		vertexBuffer = &vertexCache.frameData[vertexCache.drawListNum].vertexBuffer;
	}
	const int vertOffset = (int)( vbHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

	// get index buffer
	const vertCacheHandle_t ibHandle = surf->indexCache;
	idIndexBuffer * indexBuffer;
	if ( vertexCache.CacheIsStatic( ibHandle ) ) {
		indexBuffer = &vertexCache.staticData.indexBuffer;
	} else {
		const uint64 frameNum = (int)( ibHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
		if ( frameNum != ( ( vertexCache.currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
			idLib::Warning( "RB_DrawElementsWithCounters, indexBuffer == NULL" );
			return;
		}
		indexBuffer = &vertexCache.frameData[vertexCache.drawListNum].indexBuffer;
	}
	const int indexOffset = (int)( ibHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

	RENDERLOG_PRINTF( "Binding Buffers: %p:%i %p:%i\n", vertexBuffer, vertOffset, indexBuffer, indexOffset );

    /*
    @pjb: todo
	if ( surf->jointCache ) {
		if ( !verify( renderProgManager.ShaderUsesJoints() ) ) {
			return;
		}
	} else {
		if ( !verify( !renderProgManager.ShaderUsesJoints() || renderProgManager.ShaderHasOptionalSkinning() ) ) {
			return;
		}
	}
    */

    UINT numCBuffers = 2;
    ID3D11Buffer* constantBuffers[] = {
        renderProgManager.GetRenderParmConstantBuffer(),
        renderProgManager.GetUserParmConstantBuffer(),
        nullptr
    };
    
	if ( surf->jointCache ) {
		idJointBuffer jointBuffer;
		if ( !vertexCache.GetJointBuffer( surf->jointCache, &jointBuffer ) ) {
			idLib::Warning( "RB_DrawElementsWithCounters, jointBuffer == NULL" );
			return;
		}
		assert( ( jointBuffer.GetOffset() & ( glConfig.uniformBufferOffsetAlignment - 1 ) ) == 0 );

        ID3D11Buffer* pJointBuffer = jointBuffer.GetBuffer();
        constantBuffers[numCBuffers++] = pJointBuffer;

        uint offset[4] = { jointBuffer.GetOffset() / ( sizeof( float ) * 4 ), 0, 0, 0 };
        renderProgManager.SetRenderParm( RENDERPARM_JOINT_OFFSET, (float*) offset );
	}

	renderProgManager.UpdateConstantBuffers( pContext );

    pContext->VSSetConstantBuffers( 0, numCBuffers, constantBuffers );
    pContext->PSSetConstantBuffers( 0, numCBuffers, constantBuffers );

    ID3D11Buffer* pVertexBuffer = vertexBuffer->GetBuffer();
    UINT vbOffset = 0;
    UINT vbStride = sizeof( idDrawVert );

    pContext->IASetInputLayout( idLayoutManager::GetLayout<idDrawVert>() );
    pContext->IASetIndexBuffer( indexBuffer->GetBuffer(), DXGI_FORMAT_R16_UINT, 0 );
    pContext->IASetVertexBuffers( 0, 1, &pVertexBuffer, &vbStride, &vbOffset );
    pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

    pContext->DrawIndexed(
		r_singleTriangle.GetBool() ? 3 : surf->numIndexes,
        indexOffset / sizeof( triIndex_t ),
        vertOffset / sizeof ( idDrawVert ) );
}

/*
======================
RB_GetShaderTextureMatrix
======================
*/
static void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, float matrix[16] ) {
	matrix[0*4+0] = shaderRegisters[ texture->matrix[0][0] ];
	matrix[1*4+0] = shaderRegisters[ texture->matrix[0][1] ];
	matrix[2*4+0] = 0.0f;
	matrix[3*4+0] = shaderRegisters[ texture->matrix[0][2] ];

	matrix[0*4+1] = shaderRegisters[ texture->matrix[1][0] ];
	matrix[1*4+1] = shaderRegisters[ texture->matrix[1][1] ];
	matrix[2*4+1] = 0.0f;
	matrix[3*4+1] = shaderRegisters[ texture->matrix[1][2] ];

	// we attempt to keep scrolls from generating incredibly large texture values, but
	// center rotations and center scales can still generate offsets that need to be > 1
	if ( matrix[3*4+0] < -40.0f || matrix[12] > 40.0f ) {
		matrix[3*4+0] -= (int)matrix[3*4+0];
	}
	if ( matrix[13] < -40.0f || matrix[13] > 40.0f ) {
		matrix[13] -= (int)matrix[13];
	}

	matrix[0*4+2] = 0.0f;
	matrix[1*4+2] = 0.0f;
	matrix[2*4+2] = 1.0f;
	matrix[3*4+2] = 0.0f;

	matrix[0*4+3] = 0.0f;
	matrix[1*4+3] = 0.0f;
	matrix[2*4+3] = 0.0f;
	matrix[3*4+3] = 1.0f;
}

/*
======================
RB_LoadShaderTextureMatrix
======================
*/
static void RB_LoadShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture ) {	
	float texS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float texT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

	if ( texture->hasMatrix ) {
		float matrix[16];
		RB_GetShaderTextureMatrix( shaderRegisters, texture, matrix );
		texS[0] = matrix[0*4+0];
		texS[1] = matrix[1*4+0];
		texS[2] = matrix[2*4+0];
		texS[3] = matrix[3*4+0];
	
		texT[0] = matrix[0*4+1];
		texT[1] = matrix[1*4+1];
		texT[2] = matrix[2*4+1];
		texT[3] = matrix[3*4+1];

		RENDERLOG_PRINTF( "Setting Texture Matrix\n");
		renderLog.Indent();
		RENDERLOG_PRINTF( "Texture Matrix S : %4.3f, %4.3f, %4.3f, %4.3f\n", texS[0], texS[1], texS[2], texS[3] );
		RENDERLOG_PRINTF( "Texture Matrix T : %4.3f, %4.3f, %4.3f, %4.3f\n", texT[0], texT[1], texT[2], texT[3] );
		renderLog.Outdent();
	} 

	renderProgManager.SetRenderParm( RENDERPARM_TEXTUREMATRIX_S, texS );
	renderProgManager.SetRenderParm( RENDERPARM_TEXTUREMATRIX_T, texT );
}

/*
=====================
RB_BakeTextureMatrixIntoTexgen
=====================
*/
static void RB_BakeTextureMatrixIntoTexgen( idPlane lightProject[3], const float *textureMatrix ) {
	float genMatrix[16];
	float final[16];

	genMatrix[0*4+0] = lightProject[0][0];
	genMatrix[1*4+0] = lightProject[0][1];
	genMatrix[2*4+0] = lightProject[0][2];
	genMatrix[3*4+0] = lightProject[0][3];

	genMatrix[0*4+1] = lightProject[1][0];
	genMatrix[1*4+1] = lightProject[1][1];
	genMatrix[2*4+1] = lightProject[1][2];
	genMatrix[3*4+1] = lightProject[1][3];

	genMatrix[0*4+2] = 0.0f;
	genMatrix[1*4+2] = 0.0f;
	genMatrix[2*4+2] = 0.0f;
	genMatrix[3*4+2] = 0.0f;

	genMatrix[0*4+3] = lightProject[2][0];
	genMatrix[1*4+3] = lightProject[2][1];
	genMatrix[2*4+3] = lightProject[2][2];
	genMatrix[3*4+3] = lightProject[2][3];

	R_MatrixMultiply( genMatrix, textureMatrix, final );

	lightProject[0][0] = final[0*4+0];
	lightProject[0][1] = final[1*4+0];
	lightProject[0][2] = final[2*4+0];
	lightProject[0][3] = final[3*4+0];

	lightProject[1][0] = final[0*4+1];
	lightProject[1][1] = final[1*4+1];
	lightProject[1][2] = final[2*4+1];
	lightProject[1][3] = final[3*4+1];
}

/*
================
RB_PrepareStageTexturing
================
*/
static int RB_PrepareStageTexturing( 
    const shaderStage_t *pStage, 
    const drawSurf_t *surf, 
    BUILTIN_SHADER *shaderToUse,
    idImage* pImages[] ) {

    int numImages = 1;

	if ( pStage->texture.cinematic ) {
		cinData_t cin;

		if ( r_skipDynamicTextures.GetBool() ) {
			pImages[0] = globalImages->defaultImage;
		} else {
		    // offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
		    // We make no attempt to optimize for multiple identical cinematics being in view, or
		    // for cinematics going at a lower framerate than the renderer.
		    cin = pStage->texture.cinematic->ImageForTime( backEnd.viewDef->renderView.time[0] + idMath::Ftoi( 1000.0f * backEnd.viewDef->renderView.shaderParms[11] ) );
		    if ( cin.imageY != NULL ) {
			    pImages[0] = cin.imageY;
			    pImages[0] = cin.imageCr;
			    pImages[0] = cin.imageCb;
		    } else {
			    pImages[0] = globalImages->blackImage;
			    // because the shaders may have already been set - we need to make sure we are not using a bink shader which would 
			    // display incorrectly.  We may want to get rid of RB_BindVariableStageImage and inline the code so that the
			    // SWF GUI case is handled better, too

                // @pjb: todo: kill this
			    *shaderToUse = BUILTIN_SHADER_TEXTURE_VERTEXCOLOR;
		    }
        }
	} else {
		// FIXME: see why image is invalid
		if ( pStage->texture.image != NULL ) {
			pImages[0] = pStage->texture.image;
		}
	}

    float useTexGenParm[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// set the texture matrix if needed
	RB_LoadShaderTextureMatrix( surf->shaderRegisters, &pStage->texture );

	// texgens
	if ( pStage->texture.texgen == TG_REFLECT_CUBE ) {

		// see if there is also a bump map specified
		const shaderStage_t *bumpStage = surf->material->GetBumpStage();
		if ( bumpStage != NULL ) {
			// per-pixel reflection mapping with bump mapping
            pImages[numImages++] = bumpStage->texture.image;

			RENDERLOG_PRINTF( "TexGen: TG_REFLECT_CUBE: Bumpy Environment\n" );
			if ( surf->jointCache ) {
				*shaderToUse = BUILTIN_SHADER_BUMPY_ENVIRONMENT_SKINNED;
			} else {
				*shaderToUse = BUILTIN_SHADER_BUMPY_ENVIRONMENT;
			}
		} else {
			RENDERLOG_PRINTF( "TexGen: TG_REFLECT_CUBE: Environment\n" );
			if ( surf->jointCache ) {
                *shaderToUse = BUILTIN_SHADER_ENVIRONMENT_SKINNED;
			} else {
                *shaderToUse = BUILTIN_SHADER_ENVIRONMENT;
			}
		}

	} else if ( pStage->texture.texgen == TG_SKYBOX_CUBE ) {

        *shaderToUse = BUILTIN_SHADER_SKYBOX;

	} else if ( pStage->texture.texgen == TG_WOBBLESKY_CUBE ) {

		const int * parms = surf->material->GetTexGenRegisters();

		float wobbleDegrees = surf->shaderRegisters[ parms[0] ] * ( idMath::PI / 180.0f );
		float wobbleSpeed = surf->shaderRegisters[ parms[1] ] * ( 2.0f * idMath::PI / 60.0f );
		float rotateSpeed = surf->shaderRegisters[ parms[2] ] * ( 2.0f * idMath::PI / 60.0f );

		idVec3 axis[3];
		{
			// very ad-hoc "wobble" transform
			float s, c;
			idMath::SinCos( wobbleSpeed * backEnd.viewDef->renderView.time[0] * 0.001f, s, c );

			float ws, wc;
			idMath::SinCos( wobbleDegrees, ws, wc );

			axis[2][0] = ws * c;
			axis[2][1] = ws * s;
			axis[2][2] = wc;

			axis[1][0] = -s * s * ws;
			axis[1][2] = -s * ws * ws;
			axis[1][1] = idMath::Sqrt( idMath::Fabs( 1.0f - ( axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2] ) ) );

			// make the second vector exactly perpendicular to the first
			axis[1] -= ( axis[2] * axis[1] ) * axis[2];
			axis[1].Normalize();

			// construct the third with a cross
			axis[0].Cross( axis[1], axis[2] );
		}

		// add the rotate
		float rs, rc;
		idMath::SinCos( rotateSpeed * backEnd.viewDef->renderView.time[0] * 0.001f, rs, rc );

		float transform[12];
		transform[0*4+0] = axis[0][0] * rc + axis[1][0] * rs;
		transform[0*4+1] = axis[0][1] * rc + axis[1][1] * rs;
		transform[0*4+2] = axis[0][2] * rc + axis[1][2] * rs;
		transform[0*4+3] = 0.0f;

		transform[1*4+0] = axis[1][0] * rc - axis[0][0] * rs;
		transform[1*4+1] = axis[1][1] * rc - axis[0][1] * rs;
		transform[1*4+2] = axis[1][2] * rc - axis[0][2] * rs;
		transform[1*4+3] = 0.0f;

		transform[2*4+0] = axis[2][0];
		transform[2*4+1] = axis[2][1];
		transform[2*4+2] = axis[2][2];
		transform[2*4+3] = 0.0f;

		renderProgManager.SetRenderParms( RENDERPARM_WOBBLESKY_X, transform, 3 );		
        *shaderToUse = BUILTIN_SHADER_WOBBLESKY;

	} else if ( ( pStage->texture.texgen == TG_SCREEN ) || ( pStage->texture.texgen == TG_SCREEN2 ) ) {

		useTexGenParm[0] = 1.0f;
		useTexGenParm[1] = 1.0f;
		useTexGenParm[2] = 1.0f;
		useTexGenParm[3] = 1.0f;

		float mat[16];
		R_MatrixMultiply( surf->space->modelViewMatrix, backEnd.viewDef->projectionMatrix, mat );

		RENDERLOG_PRINTF( "TexGen : %s\n", ( pStage->texture.texgen == TG_SCREEN ) ? "TG_SCREEN" : "TG_SCREEN2" );
		renderLog.Indent();

		float plane[4];
		plane[0] = mat[0*4+0];
		plane[1] = mat[1*4+0];
		plane[2] = mat[2*4+0];
		plane[3] = mat[3*4+0];
		renderProgManager.SetRenderParm( RENDERPARM_TEXGEN_0_S, plane );
		RENDERLOG_PRINTF( "TEXGEN_S = %4.3f, %4.3f, %4.3f, %4.3f\n",  plane[0], plane[1], plane[2], plane[3] );

		plane[0] = mat[0*4+1];
		plane[1] = mat[1*4+1];
		plane[2] = mat[2*4+1];
		plane[3] = mat[3*4+1];
		renderProgManager.SetRenderParm( RENDERPARM_TEXGEN_0_T, plane );
		RENDERLOG_PRINTF( "TEXGEN_T = %4.3f, %4.3f, %4.3f, %4.3f\n",  plane[0], plane[1], plane[2], plane[3] );

		plane[0] = mat[0*4+3];
		plane[1] = mat[1*4+3];
		plane[2] = mat[2*4+3];
		plane[3] = mat[3*4+3];
		renderProgManager.SetRenderParm( RENDERPARM_TEXGEN_0_Q, plane );	
		RENDERLOG_PRINTF( "TEXGEN_Q = %4.3f, %4.3f, %4.3f, %4.3f\n",  plane[0], plane[1], plane[2], plane[3] );

		renderLog.Outdent();

	} else if ( pStage->texture.texgen == TG_DIFFUSE_CUBE ) {

		// As far as I can tell, this is never used
		idLib::Warning( "Using Diffuse Cube! Please contact Brian!" );

	} else if ( pStage->texture.texgen == TG_GLASSWARP ) {

		// As far as I can tell, this is never used
		idLib::Warning( "Using GlassWarp! Please contact Brian!" );
	}

	renderProgManager.SetRenderParm( RENDERPARM_TEXGEN_0_ENABLED, useTexGenParm );

    return numImages;
}

/*
=====================
RB_DrawStageCustomVFP

new style stages
=====================
*/
static void RB_DrawStageCustomVFP( 
    ID3D11DeviceContext2* pContext, 
    const drawSurf_t* surf,
    const newShaderStage_t *newStage, 
    uint64 stageGLState ) {

    GPU_SCOPED_PROFILE();

    // @pjb: is this a safe constant?
    idImage* pImages[16];

	// get the expressions for conditionals / color / texcoords
	const float	*regs = surf->shaderRegisters;

	renderLog.OpenBlock( "New Shader Stage" );

    D3DDrv_SetBlendStateFromMask( pContext, stageGLState );
    D3DDrv_SetDepthStateFromMask( pContext, stageGLState );

	for ( int j = 0; j < newStage->numVertexParms; j++ ) {
		float parm[4];
		parm[0] = regs[ newStage->vertexParms[j][0] ];
		parm[1] = regs[ newStage->vertexParms[j][1] ];
		parm[2] = regs[ newStage->vertexParms[j][2] ];
		parm[3] = regs[ newStage->vertexParms[j][3] ];
		renderProgManager.SetRenderParm( (renderParm_t)( RENDERPARM_USER + j ), parm );
	}

	// set rpEnableSkinning if the shader has optional support for skinning
	if ( surf->jointCache && renderProgManager.ShaderHasOptionalSkinning( newStage->vertexProgram ) ) {
		const idVec4 skinningParm( 1.0f );
		renderProgManager.SetRenderParm( RENDERPARM_ENABLE_SKINNING, skinningParm.ToFloatPtr() );
	}

    assert( newStage->numFragmentProgramImages < _countof( pImages ) );

	// bind texture units
	for ( int j = 0; j < newStage->numFragmentProgramImages; j++ ) {
		idImage * image = newStage->fragmentProgramImages[j];
		if ( image != NULL ) {
            pImages[j] = image;
		}
	}
                
    ID3D11VertexShader* pVertexShader = renderProgManager.GetVertexShader( newStage->vertexProgram );
    ID3D11PixelShader* pPixelShader = renderProgManager.GetPixelShader( newStage->fragmentProgram );

    pContext->VSSetShader( pVertexShader, nullptr, 0 );
    pContext->PSSetShader( pPixelShader, nullptr, 0 );
    RB_BindImages( pContext, pImages, 0, newStage->numFragmentProgramImages );

	// draw it
	RB_DrawElementsWithCounters( pContext, surf );
                
	// clear rpEnableSkinning if it was set
	if ( surf->jointCache && renderProgManager.ShaderHasOptionalSkinning( newStage->vertexProgram ) ) {
		const idVec4 skinningParm( 0.0f );
		renderProgManager.SetRenderParm( RENDERPARM_ENABLE_SKINNING, skinningParm.ToFloatPtr() );
	}

	renderLog.CloseBlock();
}

/*
=====================
RB_DrawStageBuiltInFVP

Make sure you bind your images before you call this
=====================
*/
static void RB_DrawStageBuiltInVFP( 
    ID3D11DeviceContext2* pContext,
    const BUILTIN_SHADER shader,
    const drawSurf_t *surf, 
    const shaderStage_t *pStage, 
    const uint64 stageGLState ) {

    GPU_SCOPED_PROFILE();
    
    // get the expressions for conditionals / color / texcoords
	const float	*regs = surf->shaderRegisters;

	// set the color
	float color[4];
	color[0] = regs[ pStage->color.registers[0] ];
	color[1] = regs[ pStage->color.registers[1] ];
	color[2] = regs[ pStage->color.registers[2] ];
	color[3] = regs[ pStage->color.registers[3] ];

	// skip the entire stage if an add would be black
	if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE ) 
		&& color[0] <= 0 && color[1] <= 0 && color[2] <= 0 ) {
		return;
	}

	// skip the entire stage if a blend would be completely transparent
	if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA )
		&& color[3] <= 0 ) {
		return;
	}

	renderLog.OpenBlock( "Old Shader Stage" );

    renderProgManager.SetRenderParm( RENDERPARM_COLOR, color );

	stageVertexColor_t svc = pStage->vertexColor;
	if ( surf->space->isGuiSurface ) {
		// Force gui surfaces to always be SVC_MODULATE
		svc = SVC_MODULATE;
    }
    RB_SetVertexColorParms( svc );

	// set the state
    D3DDrv_SetBlendStateFromMask( pContext, stageGLState );
    D3DDrv_SetDepthStateFromMask( pContext, stageGLState );
		
    pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( shader ), nullptr, 0 );
    pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( shader ), nullptr, 0 );

	// draw it
	RB_DrawElementsWithCounters( pContext, surf );

    RB_ResetColor();

	renderLog.CloseBlock();
}

/*
=========================================================================================

DEPTH BUFFER RENDERING

=========================================================================================
*/


/*
==================
RB_FillDepthBufferGeneric
==================
*/
static void RB_FillDepthBufferGeneric( ID3D11DeviceContext2* pContext, const drawSurf_t * const * drawSurfs, int numDrawSurfs ) {

    idImage* pImages[16];

    GPU_SCOPED_PROFILE();
    
    for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t * drawSurf = drawSurfs[i];
		const idMaterial * shader = drawSurf->material;

		// translucent surfaces don't put anything in the depth buffer and don't
		// test against it, which makes them fail the mirror clip plane operation
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;
		}

		// get the expressions for conditionals / color / texcoords
		const float * regs = drawSurf->shaderRegisters;

		// if all stages of a material have been conditioned off, don't do anything
		int stage = 0;
		for ( ; stage < shader->GetNumStages(); stage++ ) {		
			const shaderStage_t * pStage = shader->GetStage( stage );
			// check the stage enable condition
			if ( regs[ pStage->conditionRegister ] != 0 ) {
				break;
			}
		}
		if ( stage == shader->GetNumStages() ) {
			continue;
		}

		// change the matrix if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			RB_SetMVP( drawSurf->space->mvp );

			backEnd.currentSpace = drawSurf->space;
		}

		uint64 surfGLState = 0;

		// set polygon offset if necessary
        ID3D11RasterizerState* rasterizerState = 
		    ( shader->GetRasterizerState() ) ?
			shader->GetRasterizerState() :
		    D3DDrv_GetRasterizerState( shader->GetCullType(), surfGLState );
        
		// subviews will just down-modulate the color buffer
		float color[4];
		if ( shader->GetSort() == SS_SUBVIEW ) {
			surfGLState |= GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS;
			color[0] = 1.0f;
			color[1] = 1.0f;
			color[2] = 1.0f;
			color[3] = 1.0f;
		} else {
			// others just draw black
			color[0] = 0.0f;
			color[1] = 0.0f;
			color[2] = 0.0f;
			color[3] = 1.0f;
		}

		renderLog.OpenBlock( shader->GetName() );

		bool drawSolid = false;
		if ( shader->Coverage() == MC_OPAQUE ) {
			drawSolid = true;
		} else if ( shader->Coverage() == MC_PERFORATED ) {
			// we may have multiple alpha tested stages
			// if the only alpha tested stages are condition register omitted,
			// draw a normal opaque surface
			bool didDraw = false;

			// perforated surfaces may have multiple alpha tested stages
			for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {		
				const shaderStage_t *pStage = shader->GetStage(stage);

				if ( !pStage->hasAlphaTest ) {
					continue;
				}

				// check the stage enable condition
				if ( regs[ pStage->conditionRegister ] == 0 ) {
					continue;
				}

				// if we at least tried to draw an alpha tested stage,
				// we won't draw the opaque surface
				didDraw = true;

				// set the alpha modulate
				color[3] = regs[ pStage->color.registers[3] ];

				// skip the entire stage if alpha would be black
				if ( color[3] <= 0.0f ) {
					continue;
				}

				uint64 stageGLState = surfGLState;

                renderProgManager.SetRenderParm( RENDERPARM_COLOR, color );

				idVec4 alphaTestValue( regs[ pStage->alphaTestRegister ] );
				renderProgManager.SetRenderParm( RENDERPARM_ALPHA_TEST, alphaTestValue.ToFloatPtr() );

                BUILTIN_SHADER shaderToUse = 
				    ( drawSurf->jointCache ) ?
                    BUILTIN_SHADER_TEXTURE_VERTEXCOLOR_SKINNED :
                    BUILTIN_SHADER_TEXTURE_VERTEXCOLOR;

				RB_SetVertexColorParms( SVC_IGNORE );

				// bind the textures
                pImages[0] = pStage->texture.image;
                int numImages = RB_PrepareStageTexturing( pStage, drawSurf, &shaderToUse, pImages );
                RB_BindImages( pContext, pImages, 0, numImages );

                // bind the shaders
                pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( shaderToUse ), nullptr, 0 );
                pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( shaderToUse ), nullptr, 0 );

				// must render with less-equal for Z-Cull to work properly
                D3DDrv_SetDepthStateFromMask( pContext, ( stageGLState & ~GLS_DEPTHFUNC_BITS ) | GLS_DEPTHFUNC_LESS );
                D3DDrv_SetBlendStateFromMask( pContext, stageGLState );

				// set privatePolygonOffset if necessary
				if ( pStage->rasterizerState ) {
					pContext->RSSetState( pStage->rasterizerState );
				} else {
                    pContext->RSSetState( rasterizerState ); 
                }

				// draw it
				RB_DrawElementsWithCounters( pContext, drawSurf );

                RB_ResetColor();
			}

			if ( !didDraw ) {
				drawSolid = true;
			}
		}

		// draw the entire surface solid
		if ( drawSolid ) {

            BUILTIN_SHADER shaderToUse;
			if ( shader->GetSort() == SS_SUBVIEW ) {
				shaderToUse = BUILTIN_SHADER_COLOR;
                renderProgManager.SetRenderParm( RENDERPARM_COLOR, color );
			} else {
				if ( drawSurf->jointCache ) {
					shaderToUse = BUILTIN_SHADER_DEPTH_SKINNED;
				} else {
					shaderToUse = BUILTIN_SHADER_DEPTH;
				}
				surfGLState |= GLS_ALPHAMASK;
			}

            D3DDrv_SetDepthStateFromMask( pContext, ( surfGLState & ~GLS_DEPTHFUNC_BITS ) | GLS_DEPTHFUNC_LESS );
            D3DDrv_SetBlendStateFromMask( pContext, surfGLState );
            pContext->RSSetState( rasterizerState ); 

            // bind the shaders
            pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( shaderToUse ), nullptr, 0 );
            pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( shaderToUse ), nullptr, 0 );

			// draw it
			RB_DrawElementsWithCounters( pContext, drawSurf );

            RB_ResetColor();
		}

		renderLog.CloseBlock();
	}

    renderProgManager.SetRenderParm( RENDERPARM_ALPHA_TEST, vec4_zero.ToFloatPtr() );
}

/*
=====================
RB_FillDepthBufferFast

Optimized fast path code.

If there are subview surfaces, they must be guarded in the depth buffer to allow
the mirror / subview to show through underneath the current view rendering.

Surfaces with perforated shaders need the full shader setup done, but should be
drawn after the opaque surfaces.

The bulk of the surfaces should be simple opaque geometry that can be drawn very rapidly.

If there are no subview surfaces, we could clear to black and use fast-Z rendering
on the 360.
=====================
*/
static void RB_FillDepthBufferFast( ID3D11DeviceContext2* pContext, drawSurf_t **drawSurfs, int numDrawSurfs ) {
	renderLog.OpenMainBlock( MRB_FILL_DEPTH_BUFFER );
	renderLog.OpenBlock( "RB_FillDepthBufferFast" );

    GPU_SCOPED_PROFILE();
    
    // force MVP change on first surface
	backEnd.currentSpace = NULL;

	int	surfNum;
	for ( surfNum = 0; surfNum < numDrawSurfs; surfNum++ ) {
		if ( drawSurfs[surfNum]->material->GetSort() != SS_SUBVIEW ) {
			break;
		}
		RB_FillDepthBufferGeneric( pContext, &drawSurfs[surfNum], 1 );
	}

	const drawSurf_t ** perforatedSurfaces = (const drawSurf_t ** )_alloca( numDrawSurfs * sizeof( drawSurf_t * ) );
	int numPerforatedSurfaces = 0;

	// draw all the opaque surfaces and build up a list of perforated surfaces that
	// we will defer drawing until all opaque surfaces are done

	// must render with less-equal for Z-Cull to work properly
    D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHFUNC_LESS );
    D3DDrv_SetRasterizerStateFromMask( pContext, CT_FRONT_SIDED, GLS_DEFAULT );
    D3DDrv_SetBlendStateFromMask( pContext, GLS_DEFAULT );

	// continue checking past the subview surfaces
	for ( ; surfNum < numDrawSurfs; surfNum++ ) {
		const drawSurf_t * surf = drawSurfs[ surfNum ];
		const idMaterial * shader = surf->material;

		// translucent surfaces don't put anything in the depth buffer
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			continue;
		}
		if ( shader->Coverage() == MC_PERFORATED ) {
			// save for later drawing
			perforatedSurfaces[ numPerforatedSurfaces ] = surf;
			numPerforatedSurfaces++;
			continue;
		}

		// set polygon offset?

		// set mvp matrix
		if ( surf->space != backEnd.currentSpace ) {
			RB_SetMVP( surf->space->mvp );
			backEnd.currentSpace = surf->space;
		}

		renderLog.OpenBlock( shader->GetName() );

        BUILTIN_SHADER shaderToUse =
		    ( surf->jointCache ) ?
			BUILTIN_SHADER_DEPTH_SKINNED :
    		BUILTIN_SHADER_DEPTH;

        // bind the shaders
        pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( shaderToUse ), nullptr, 0 );
        pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( BUILTIN_SHADER_DEPTH ), nullptr, 0 );

		// draw it solid
		RB_DrawElementsWithCounters( pContext, surf );

		renderLog.CloseBlock();
	}

	// draw all perforated surfaces with the general code path
	if ( numPerforatedSurfaces > 0 ) {
		RB_FillDepthBufferGeneric( pContext, perforatedSurfaces, numPerforatedSurfaces );
	}

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

/*
==============================================================================================

STENCIL SHADOW RENDERING

==============================================================================================
*/

/*
=====================
RB_StencilShadowPass

The stencil buffer should have been set to 128 on any surfaces that might receive shadows.
=====================
*/
static void RB_StencilShadowPass( ID3D11DeviceContext2* pContext, const drawSurf_t *drawSurfs, const viewLight_t * vLight ) {
	if ( r_skipShadows.GetBool() ) {
		return;
	}

	if ( drawSurfs == NULL ) {
		return;
	}

    GPU_SCOPED_PROFILE();
    
    RENDERLOG_PRINTF( "---------- RB_StencilShadowPass ----------\n" );

    pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_SHADOW ), nullptr, 0 );
    pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader(  BUILTIN_SHADER_SHADOW ), nullptr, 0 );

	uint64 glState = 0;

	// for visualizing the shadows
	if ( r_showShadows.GetInteger() ) {
		// set the debug shadow color
		renderProgManager.SetRenderParm( RENDERPARM_COLOR, colorMagenta.ToFloatPtr() );
		if ( r_showShadows.GetInteger() == 2 ) {
			// draw filled in
			glState = GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS;
		} else {
			// draw as lines, filling the depth buffer
			glState = GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS;
		}
	} else {
		// don't write to the color or depth buffer, just the stencil buffer
		glState = GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS;
	}

    D3DDrv_SetBlendStateFromMask( pContext, glState );

	// the actual stencil func will be set in the draw code, but we need to make sure it isn't
	// disabled here, and that the value will get reset for the interactions without looking
	// like a no-change-required
    glState |= GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE );
    D3DDrv_SetDepthStateFromMask( pContext, glState | GLS_DEPTH_STENCIL_PACKAGE_INC );

	// Two Sided Stencil reduces two draw calls to one for slightly faster shadows
    D3DDrv_SetRasterizerStateFromMask( pContext, CT_TWO_SIDED, glState | GLS_POLYGON_OFFSET_SHADOW );

	// process the chain of shadows with the current rendering state
	backEnd.currentSpace = NULL;

	for ( const drawSurf_t * drawSurf = drawSurfs; drawSurf != NULL; drawSurf = drawSurf->nextOnLight ) {
		if ( drawSurf->scissorRect.IsEmpty() ) {
			continue;	// !@# FIXME: find out why this is sometimes being hit!
						// temporarily jump over the scissor and draw so the gl error callback doesn't get hit
		}

		// make sure the shadow volume is done
		if ( drawSurf->shadowVolumeState != SHADOWVOLUME_DONE ) {
			assert( drawSurf->shadowVolumeState == SHADOWVOLUME_UNFINISHED || drawSurf->shadowVolumeState == SHADOWVOLUME_DONE );

			uint64 start = Sys_Microseconds();
			while ( drawSurf->shadowVolumeState == SHADOWVOLUME_UNFINISHED ) {
				Sys_Yield();
			}
			uint64 end = Sys_Microseconds();

			backEnd.pc.shadowMicroSec += end - start;
		}

		if ( drawSurf->numIndexes == 0 ) {
			continue;	// a job may have created an empty shadow volume
		}

		if ( !backEnd.currentScissor.Equals( drawSurf->scissorRect ) && r_useScissor.GetBool() ) {
			// change the scissor
			D3DDrv_SetScissor( pContext,
                        backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1,
						backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
						drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
						drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1 );
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		if ( drawSurf->space != backEnd.currentSpace ) {
			// change the matrix
			RB_SetMVP( drawSurf->space->mvp );

			// set the local light position to allow the vertex program to project the shadow volume end cap to infinity
			idVec4 localLight( 0.0f );
			R_GlobalPointToLocal( drawSurf->space->modelMatrix, vLight->globalLightOrigin, localLight.ToVec3() );
			renderProgManager.SetRenderParm( RENDERPARM_LOCALLIGHTORIGIN, localLight.ToFloatPtr() );

			backEnd.currentSpace = drawSurf->space;
		}

		// if ( r_showShadows.GetInteger() == 0 ) { // @pjb: todo
			if ( drawSurf->jointCache ) {
                pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_SHADOW_SKINNED ), nullptr, 0 );
			} else {
                pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_SHADOW ), nullptr, 0 );
			}
		//} else {
		//	if ( drawSurf->jointCache ) {
		//		renderProgManager.BindShader_ShadowDebugSkinned();
		//	} else {
		//		renderProgManager.BindShader_ShadowDebug();
		//	}
		//}

		// Determine whether or not the shadow volume needs to be rendered with Z-pass or
		// Z-fail. It is worthwhile to spend significant resources to reduce the number of
		// cases where shadow volumes need to be rendered with Z-fail because Z-fail
		// rendering can be significantly slower even on today's hardware. For instance,
		// on NVIDIA hardware Z-fail rendering causes the Z-Cull to be used in reverse:
		// Z-near becomes Z-far (trivial accept becomes trivial reject). Using the Z-Cull
		// in reverse is far less efficient because the Z-Cull only stores Z-near per 16x16
		// pixels while the Z-far is stored per 4x2 pixels. (The Z-near coallesce buffer
		// which has 4x4 granularity is only used when updating the depth which is not the
		// case for shadow volumes.) Note that it is also important to NOT use a Z-Cull
		// reconstruct because that would clear the Z-near of the Z-Cull which results in
		// no trivial rejection for Z-fail stencil shadow rendering.

		const bool renderZPass = ( drawSurf->renderZFail == 0 ) || r_forceZPassStencilShadows.GetBool();


		if ( renderZPass ) {
			// Z-pass
		    D3DDrv_SetDepthStateFromMask( pContext, glState | GLS_DEPTH_STENCIL_PACKAGE_Z );
		} else if ( r_useStencilShadowPreload.GetBool() ) {
			// preload + Z-pass
		    D3DDrv_SetDepthStateFromMask( pContext, glState | GLS_DEPTH_STENCIL_PACKAGE_PRELOAD_Z );
		} else {
			// Z-fail
		    D3DDrv_SetDepthStateFromMask( pContext, glState | GLS_DEPTH_STENCIL_PACKAGE_INC );
        }

		// get vertex buffer
		const vertCacheHandle_t vbHandle = drawSurf->shadowCache;
		idVertexBuffer * vertexBuffer;
		if ( vertexCache.CacheIsStatic( vbHandle ) ) {
			vertexBuffer = &vertexCache.staticData.vertexBuffer;
		} else {
			const uint64 frameNum = (int)( vbHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
			if ( frameNum != ( ( vertexCache.currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
				idLib::Warning( "RB_DrawElementsWithCounters, vertexBuffer == NULL" );
				continue;
			}
			vertexBuffer = &vertexCache.frameData[vertexCache.drawListNum].vertexBuffer;
		}
		const int vertOffset = (int)( vbHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

		// get index buffer
		const vertCacheHandle_t ibHandle = drawSurf->indexCache;
		idIndexBuffer * indexBuffer;
		if ( vertexCache.CacheIsStatic( ibHandle ) ) {
			indexBuffer = &vertexCache.staticData.indexBuffer;
		} else {
			const uint64 frameNum = (int)( ibHandle >> VERTCACHE_FRAME_SHIFT ) & VERTCACHE_FRAME_MASK;
			if ( frameNum != ( ( vertexCache.currentFrame - 1 ) & VERTCACHE_FRAME_MASK ) ) {
				idLib::Warning( "RB_DrawElementsWithCounters, indexBuffer == NULL" );
				continue;
			}
			indexBuffer = &vertexCache.frameData[vertexCache.drawListNum].indexBuffer;
		}
		const uint64 indexOffset = (int)( ibHandle >> VERTCACHE_OFFSET_SHIFT ) & VERTCACHE_OFFSET_MASK;

		RENDERLOG_PRINTF( "Binding Buffers: %p %p\n", vertexBuffer, indexBuffer );

        UINT numCBuffers = 2;
        ID3D11Buffer* constantBuffers[] = {
            renderProgManager.GetRenderParmConstantBuffer(),
            renderProgManager.GetUserParmConstantBuffer(),
            nullptr
        };

        size_t vertSize = sizeof( idShadowVert );
        ID3D11InputLayout* pLayout = idLayoutManager::GetLayout<idShadowVert>();
    
		if ( drawSurf->jointCache ) {
			// assert( renderProgManager.ShaderUsesJoints() ); @pjb: todo

			idJointBuffer jointBuffer;
			if ( !vertexCache.GetJointBuffer( drawSurf->jointCache, &jointBuffer ) ) {
				idLib::Warning( "RB_DrawElementsWithCounters, jointBuffer == NULL" );
				continue;
			}
			assert( ( jointBuffer.GetOffset() & ( glConfig.uniformBufferOffsetAlignment - 1 ) ) == 0 );

            ID3D11Buffer* pJointBuffer = jointBuffer.GetBuffer();
            constantBuffers[numCBuffers++] = pJointBuffer;

            uint offset[4] = { jointBuffer.GetOffset() / ( sizeof( float ) * 4 ), 0, 0, 0 };
            renderProgManager.SetRenderParm( RENDERPARM_JOINT_OFFSET, (float*) offset );

            pLayout = idLayoutManager::GetLayout<idShadowVertSkinned>();
            vertSize = sizeof( idShadowVertSkinned );
		}

		renderProgManager.UpdateConstantBuffers( pContext );

        pContext->VSSetConstantBuffers( 0, numCBuffers, constantBuffers );
        pContext->PSSetConstantBuffers( 0, numCBuffers, constantBuffers );

        ID3D11Buffer* pVertexBuffer = vertexBuffer->GetBuffer();
        UINT vbOffset = 0;
        UINT vbStride = (UINT) vertSize;

        pContext->IASetInputLayout( pLayout );
        pContext->IASetIndexBuffer( indexBuffer->GetBuffer(), DXGI_FORMAT_R16_UINT, 0 );
        pContext->IASetVertexBuffers( 0, 1, &pVertexBuffer, &vbStride, &vbOffset );
        pContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    
        pContext->DrawIndexed(
		    r_singleTriangle.GetBool() ? 3 : drawSurf->numIndexes,
            indexOffset / sizeof( triIndex_t ),
            vertOffset / vertSize );

		if ( !renderZPass && r_useStencilShadowPreload.GetBool() ) {
			// render again with Z-pass
		    D3DDrv_SetDepthStateFromMask( pContext, glState | GLS_DEPTH_STENCIL_PACKAGE_Z );
    
            pContext->DrawIndexed(
		        r_singleTriangle.GetBool() ? 3 : drawSurf->numIndexes,
                indexOffset / sizeof( triIndex_t ),
                vertOffset / vertSize );
		}
	}

    RB_ResetColor();
}

/*
==================
RB_StencilSelectLight

Deform the zeroOneCubeModel to exactly cover the light volume. Render the deformed cube model to the stencil buffer in
such a way that only fragments that are directly visible and contained within the volume will be written creating a 
mask to be used by the following stencil shadow and draw interaction passes.
==================
*/
static void RB_StencilSelectLight( ID3D11DeviceContext2* pContext, const viewLight_t * vLight ) {
	renderLog.OpenBlock( "Stencil Select" );

    GPU_SCOPED_PROFILE();
    
    // enable the light scissor
	if ( !backEnd.currentScissor.Equals( vLight->scissorRect ) && r_useScissor.GetBool() ) {
		D3DDrv_SetScissor( pContext,
                    backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1, 
					backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
					vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
					vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1 );
		backEnd.currentScissor = vLight->scissorRect;
	}

	// clear stencil buffer to 0 (not drawable)
    D3DDrv_SetBlendStateFromMask( pContext, 0 );
    D3DDrv_SetDepthStateFromMask( pContext, 0 );
    D3DDrv_SetRasterizerStateFromMask( pContext, CT_TWO_SIDED, 0 );
    D3DDrv_Clear( pContext, CLEAR_STENCIL, nullptr, 0, 0 );

	// two-sided stencil test
    D3DDrv_SetBlendStateFromMask( pContext, GLS_COLORMASK | GLS_ALPHAMASK );
    D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHMASK | GLS_DEPTHFUNC_LESS | GLS_DEPTH_STENCIL_PACKAGE_TWO_SIDED | GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) );

    pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_SHADOW ), nullptr, 0 );
    // pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader(  BUILTIN_SHADER_SHADOW ), nullptr, 0 );
    // No pixel shader because no color writes.
    pContext->PSSetShader( nullptr, nullptr, 0 );

	// set the matrix for deforming the 'zeroOneCubeModel' into the frustum to exactly cover the light volume
	idRenderMatrix invProjectMVPMatrix;
	idRenderMatrix::Multiply( backEnd.viewDef->worldSpace.mvp, vLight->inverseBaseLightProject, invProjectMVPMatrix );
	RB_SetMVP( invProjectMVPMatrix );

	RB_DrawElementsWithCounters( pContext, &backEnd.zeroOneCubeSurface );

	renderLog.CloseBlock();
}

/*
=========================================================================================

GENERAL INTERACTION RENDERING

=========================================================================================
*/

const int INTERACTION_TEXUNIT_BUMP			= 0;
//const int INTERACTION_TEXUNIT_DIFFUSE		= 1;
//const int INTERACTION_TEXUNIT_SPECULAR	= 2;
const int INTERACTION_TEXUNIT_FALLOFF		= 3;
//const int INTERACTION_TEXUNIT_PROJECTION	= 4;

/*
==================
RB_SetupInteractionStage
==================
*/
static void RB_SetupInteractionStage( const shaderStage_t *surfaceStage, const float *surfaceRegs, const float lightColor[4],
									idVec4 matrix[2], float color[4] ) {

	if ( surfaceStage->texture.hasMatrix ) {
		matrix[0][0] = surfaceRegs[surfaceStage->texture.matrix[0][0]];
		matrix[0][1] = surfaceRegs[surfaceStage->texture.matrix[0][1]];
		matrix[0][2] = 0.0f;
		matrix[0][3] = surfaceRegs[surfaceStage->texture.matrix[0][2]];

		matrix[1][0] = surfaceRegs[surfaceStage->texture.matrix[1][0]];
		matrix[1][1] = surfaceRegs[surfaceStage->texture.matrix[1][1]];
		matrix[1][2] = 0.0f;
		matrix[1][3] = surfaceRegs[surfaceStage->texture.matrix[1][2]];

		// we attempt to keep scrolls from generating incredibly large texture values, but
		// center rotations and center scales can still generate offsets that need to be > 1
		if ( matrix[0][3] < -40.0f || matrix[0][3] > 40.0f ) {
			matrix[0][3] -= idMath::Ftoi( matrix[0][3] );
		}
		if ( matrix[1][3] < -40.0f || matrix[1][3] > 40.0f ) {
			matrix[1][3] -= idMath::Ftoi( matrix[1][3] );
		}
	} else {
		matrix[0][0] = 1.0f;
		matrix[0][1] = 0.0f;
		matrix[0][2] = 0.0f;
		matrix[0][3] = 0.0f;

		matrix[1][0] = 0.0f;
		matrix[1][1] = 1.0f;
		matrix[1][2] = 0.0f;
		matrix[1][3] = 0.0f;
	}

	if ( color != NULL ) {
		for ( int i = 0; i < 4; i++ ) {
			// clamp here, so cards with a greater range don't look different.
			// we could perform overbrighting like we do for lights, but
			// it doesn't currently look worth it.
			color[i] = idMath::ClampFloat( 0.0f, 1.0f, surfaceRegs[surfaceStage->color.registers[i]] ) * lightColor[i];
		}
	}
}

/*
=================
RB_DrawSingleInteraction
=================
*/
static void RB_DrawSingleInteraction( 
    ID3D11DeviceContext2* pContext, 
    drawInteraction_t * din ) {

	if ( din->bumpImage == NULL ) {
		// stage wasn't actually an interaction
		return;
	}

	if ( din->diffuseImage == NULL || r_skipDiffuse.GetBool() ) {
		// this isn't a YCoCg black, but it doesn't matter, because
		// the diffuseColor will also be 0
		din->diffuseImage = globalImages->blackImage;
	}
	if ( din->specularImage == NULL || r_skipSpecular.GetBool() || din->ambientLight ) {
		din->specularImage = globalImages->blackImage;
	}
	if ( r_skipBump.GetBool() ) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	const bool diffuseIsBlack = ( din->diffuseImage == globalImages->blackImage )
									|| ( ( din->diffuseColor[0] <= 0 ) && ( din->diffuseColor[1] <= 0 ) && ( din->diffuseColor[2] <= 0 ) );
	const bool specularIsBlack = ( din->specularImage == globalImages->blackImage )
									|| ( ( din->specularColor[0] <= 0 ) && ( din->specularColor[1] <= 0 ) && ( din->specularColor[2] <= 0 ) );
	if ( diffuseIsBlack && specularIsBlack ) {
		return;
	}

    GPU_SCOPED_PROFILE();
    
    // bump matrix
	renderProgManager.SetRenderParm( RENDERPARM_BUMPMATRIX_S, din->bumpMatrix[0].ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_BUMPMATRIX_T, din->bumpMatrix[1].ToFloatPtr() );

	// diffuse matrix
	renderProgManager.SetRenderParm( RENDERPARM_DIFFUSEMATRIX_S, din->diffuseMatrix[0].ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_DIFFUSEMATRIX_T, din->diffuseMatrix[1].ToFloatPtr() );

	// specular matrix
	renderProgManager.SetRenderParm( RENDERPARM_SPECULARMATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_SPECULARMATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	RB_SetVertexColorParms( din->vertexColor );

	renderProgManager.SetRenderParm( RENDERPARM_DIFFUSEMODIFIER, din->diffuseColor.ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_SPECULARMODIFIER, din->specularColor.ToFloatPtr() );

    RB_BindImages( pContext, din->images, INTERACTION_TEXUNIT_BUMP, _countof(din->images) );
	RB_DrawElementsWithCounters( pContext, din->surf );
}

/*
=================
RB_SetupForFastPathInteractions

These are common for all fast path surfaces
=================
*/
static void RB_SetupForFastPathInteractions( const idVec4 & diffuseColor, const idVec4 & specularColor ) {
	const idVec4 sMatrix( 1, 0, 0, 0 );
	const idVec4 tMatrix( 0, 1, 0, 0 );

	// bump matrix
	renderProgManager.SetRenderParm( RENDERPARM_BUMPMATRIX_S, sMatrix.ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_BUMPMATRIX_T, tMatrix.ToFloatPtr() );

	// diffuse matrix
	renderProgManager.SetRenderParm( RENDERPARM_DIFFUSEMATRIX_S, sMatrix.ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_DIFFUSEMATRIX_T, tMatrix.ToFloatPtr() );

	// specular matrix
	renderProgManager.SetRenderParm( RENDERPARM_SPECULARMATRIX_S, sMatrix.ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_SPECULARMATRIX_T, tMatrix.ToFloatPtr() );

	RB_SetVertexColorParms( SVC_IGNORE );

	renderProgManager.SetRenderParm( RENDERPARM_DIFFUSEMODIFIER, diffuseColor.ToFloatPtr() );
	renderProgManager.SetRenderParm( RENDERPARM_SPECULARMODIFIER, specularColor.ToFloatPtr() );
}

/*
=============
RB_RenderInteractions

With added sorting and trivial path work.
=============
*/
static void RB_RenderInteractions( ID3D11DeviceContext2* pContext, const drawSurf_t *surfList, const viewLight_t * vLight, int depthFunc, bool performStencilTest ) {
	if ( surfList == NULL ) {
		return;
	}

    GPU_SCOPED_PROFILE();
    
    // change the scissor if needed, it will be constant across all the surfaces lit by the light
	if ( !backEnd.currentScissor.Equals( vLight->scissorRect ) && r_useScissor.GetBool() ) {
		D3DDrv_SetScissor(  pContext,
                            backEnd.viewDef->viewport.x1 + vLight->scissorRect.x1, 
					        backEnd.viewDef->viewport.y1 + vLight->scissorRect.y1,
					        vLight->scissorRect.x2 + 1 - vLight->scissorRect.x1,
					        vLight->scissorRect.y2 + 1 - vLight->scissorRect.y1 );
		backEnd.currentScissor = vLight->scissorRect;
	}

	// perform setup here that will be constant for all interactions
    D3DDrv_SetBlendStateFromMask( pContext, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE );
    D3DDrv_SetRasterizerStateFromMask( pContext, CT_FRONT_SIDED, 0 );

    if ( performStencilTest ) {
        D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHMASK | depthFunc | GLS_DEPTH_STENCIL_PACKAGE_REF_EQUAL | GLS_STENCIL_MAKE_REF( STENCIL_SHADOW_TEST_VALUE ) );
	} else {
        D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHMASK | depthFunc );
	}

	// some rare lights have multiple animating stages, loop over them outside the surface list
	const idMaterial * lightShader = vLight->lightShader;
	const float * lightRegs = vLight->shaderRegisters;

	drawInteraction_t inter = {};
	inter.ambientLight = lightShader->IsAmbientLight();

	//---------------------------------
	// Split out the complex surfaces from the fast-path surfaces
	// so we can do the fast path ones all in a row.
	// The surfaces should already be sorted by space because they
	// are added single-threaded, and there is only a negligable amount
	// of benefit to trying to sort by materials.
	//---------------------------------
	static const int MAX_INTERACTIONS_PER_LIGHT = 1024;
	static const int MAX_COMPLEX_INTERACTIONS_PER_LIGHT = 128;
	idStaticList< const drawSurf_t *, MAX_INTERACTIONS_PER_LIGHT > allSurfaces;
	idStaticList< const drawSurf_t *, MAX_COMPLEX_INTERACTIONS_PER_LIGHT > complexSurfaces;
	for ( const drawSurf_t * walk = surfList; walk != NULL; walk = walk->nextOnLight ) {

		// make sure the triangle culling is done
		if ( walk->shadowVolumeState != SHADOWVOLUME_DONE ) {
			assert( walk->shadowVolumeState == SHADOWVOLUME_UNFINISHED || walk->shadowVolumeState == SHADOWVOLUME_DONE );

			uint64 start = Sys_Microseconds();
			while ( walk->shadowVolumeState == SHADOWVOLUME_UNFINISHED ) {
				Sys_Yield();
			}
			uint64 end = Sys_Microseconds();

			backEnd.pc.shadowMicroSec += end - start;
		}

		const idMaterial * surfaceShader = walk->material;
		if ( surfaceShader->GetFastPathBumpImage() ) {
			allSurfaces.Append( walk );
		} else {
			complexSurfaces.Append( walk );
		}
	}
	for ( int i = 0; i < complexSurfaces.Num(); i++ ) {
		allSurfaces.Append( complexSurfaces[i] );
	}

	for ( int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++ ) {
		const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

		// ignore stages that fail the condition
		if ( !lightRegs[ lightStage->conditionRegister ] ) {
			continue;
		}

		const float lightScale = r_lightScale.GetFloat();
		const idVec4 lightColor(
			lightScale * lightRegs[ lightStage->color.registers[0] ],
			lightScale * lightRegs[ lightStage->color.registers[1] ],
			lightScale * lightRegs[ lightStage->color.registers[2] ],
			lightRegs[ lightStage->color.registers[3] ] );
		// apply the world-global overbright and the 2x factor for specular
		const idVec4 diffuseColor = lightColor;
		const idVec4 specularColor = lightColor * 2.0f;

		float lightTextureMatrix[16];
		if ( lightStage->texture.hasMatrix ) {
			RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, lightTextureMatrix );
		}

        // Bind the lighting images
        idImage* pLightImages[] = {
            vLight->falloffImage,
            lightStage->texture.image
        };

        RB_BindImages( pContext, pLightImages, INTERACTION_TEXUNIT_FALLOFF, _countof( pLightImages ) );

		// force the light textures to not use anisotropic filtering, which is wasted on them
		// all of the texture sampler parms should be constant for all interactions, only
		// the actual texture image bindings will change

		//----------------------------------
		// For all surfaces on this light list, generate an interaction for this light stage
		//----------------------------------

		// setup renderparms assuming we will be drawing trivial surfaces first
		RB_SetupForFastPathInteractions( diffuseColor, specularColor );

		// even if the space does not change between light stages, each light stage may need a different lightTextureMatrix baked in
		backEnd.currentSpace = NULL;

		for ( int sortedSurfNum = 0; sortedSurfNum < allSurfaces.Num(); sortedSurfNum++ ) {
			const drawSurf_t * const surf = allSurfaces[ sortedSurfNum ];

			// select the render prog
            BUILTIN_SHADER vshader, pshader;
			if ( lightShader->IsAmbientLight() ) {
                pshader = BUILTIN_SHADER_INTERACTION_AMBIENT; 

				if ( surf->jointCache ) {
					vshader = BUILTIN_SHADER_INTERACTION_AMBIENT_SKINNED;
				} else {
					vshader = BUILTIN_SHADER_INTERACTION_AMBIENT;
				}
			} else {
                pshader = BUILTIN_SHADER_INTERACTION; 

				if ( surf->jointCache ) {
					vshader = BUILTIN_SHADER_INTERACTION_SKINNED;
				} else {
					vshader = BUILTIN_SHADER_INTERACTION;
				}
			}

            pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( vshader ), nullptr, 0 );
            pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( pshader ), nullptr, 0 );

			const idMaterial * surfaceShader = surf->material;
			const float * surfaceRegs = surf->shaderRegisters;

			inter.surf = surf;

			// change the MVP matrix, view/light origin and light projection vectors if needed
			if ( surf->space != backEnd.currentSpace ) {
				backEnd.currentSpace = surf->space;

				// model-view-projection
				RB_SetMVP( surf->space->mvp );

				// tranform the light/view origin into model local space
				idVec4 localLightOrigin( 0.0f );
				idVec4 localViewOrigin( 1.0f );
				R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, localLightOrigin.ToVec3() );
				R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );

				// set the local light/view origin
				renderProgManager.SetRenderParm( RENDERPARM_LOCALLIGHTORIGIN, localLightOrigin.ToFloatPtr() );
				renderProgManager.SetRenderParm( RENDERPARM_LOCALVIEWORIGIN, localViewOrigin.ToFloatPtr() );

				// transform the light project into model local space
				idPlane lightProjection[4];
				for ( int i = 0; i < 4; i++ ) {
					R_GlobalPlaneToLocal( surf->space->modelMatrix, vLight->lightProject[i], lightProjection[i] );
				}

				// optionally multiply the local light projection by the light texture matrix
				if ( lightStage->texture.hasMatrix ) {
					RB_BakeTextureMatrixIntoTexgen( lightProjection, lightTextureMatrix );
				}

				// set the light projection
				renderProgManager.SetRenderParm( RENDERPARM_LIGHTPROJECTION_S, lightProjection[0].ToFloatPtr() );
				renderProgManager.SetRenderParm( RENDERPARM_LIGHTPROJECTION_T, lightProjection[1].ToFloatPtr() );
				renderProgManager.SetRenderParm( RENDERPARM_LIGHTPROJECTION_Q, lightProjection[2].ToFloatPtr() );
				renderProgManager.SetRenderParm( RENDERPARM_LIGHTFALLOFF_S, lightProjection[3].ToFloatPtr() );
			}

			// check for the fast path
			if ( surfaceShader->GetFastPathBumpImage() && !r_skipInteractionFastPath.GetBool() ) {
				renderLog.OpenBlock( surf->material->GetName() );

                // Bind the images and draw
                idImage* pMaterialImages[] = {
                    surfaceShader->GetFastPathBumpImage(),
                    surfaceShader->GetFastPathDiffuseImage(),
                    surfaceShader->GetFastPathSpecularImage()
                };
                RB_BindImages( pContext, pMaterialImages, INTERACTION_TEXUNIT_BUMP, _countof( pMaterialImages ) );

				RB_DrawElementsWithCounters( pContext, surf );

				renderLog.CloseBlock();
				continue;
			}
			
			renderLog.OpenBlock( surf->material->GetName() );

			inter.bumpImage = NULL;
			inter.specularImage = NULL;
			inter.diffuseImage = NULL;
			inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
			inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

			// go through the individual surface stages
			//
			// This is somewhat arcane because of the old support for video cards that had to render
			// interactions in multiple passes.
			//
			// We also have the very rare case of some materials that have conditional interactions
			// for the "hell writing" that can be shined on them.
			for ( int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++ ) {
				const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

				switch( surfaceStage->lighting ) {
					case SL_COVERAGE: {
						// ignore any coverage stages since they should only be used for the depth fill pass
						// for diffuse stages that use alpha test.
						break;
					}
					case SL_AMBIENT: {
						// ignore ambient stages while drawing interactions
						break;
					}
					case SL_BUMP: {
						// ignore stage that fails the condition
						if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
							break;
						}
						// draw any previous interaction
						if ( inter.bumpImage != NULL ) {
							RB_DrawSingleInteraction( pContext, &inter );
						}
						inter.bumpImage = surfaceStage->texture.image;
						inter.diffuseImage = NULL;
						inter.specularImage = NULL;
						RB_SetupInteractionStage( surfaceStage, surfaceRegs, NULL,
												inter.bumpMatrix, NULL );
						break;
					}
					case SL_DIFFUSE: {
						// ignore stage that fails the condition
						if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
							break;
						}
						// draw any previous interaction
						if ( inter.diffuseImage != NULL ) {
							RB_DrawSingleInteraction( pContext, &inter );
						}
						inter.diffuseImage = surfaceStage->texture.image;
						inter.vertexColor = surfaceStage->vertexColor;
						RB_SetupInteractionStage( surfaceStage, surfaceRegs, diffuseColor.ToFloatPtr(),
												inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
						break;
					}
					case SL_SPECULAR: {
						// ignore stage that fails the condition
						if ( !surfaceRegs[ surfaceStage->conditionRegister ] ) {
							break;
						}
						// draw any previous interaction
						if ( inter.specularImage != NULL ) {
							RB_DrawSingleInteraction( pContext, &inter );
						}
						inter.specularImage = surfaceStage->texture.image;
						inter.vertexColor = surfaceStage->vertexColor;
						RB_SetupInteractionStage( surfaceStage, surfaceRegs, specularColor.ToFloatPtr(),
												inter.specularMatrix, inter.specularColor.ToFloatPtr() );
						break;
					}
				}
			}

			// draw the final interaction
			RB_DrawSingleInteraction( pContext, &inter );

			renderLog.CloseBlock();
		}
	}
}

static void RB_DrawInteractions( ID3D11DeviceContext2* pContext ) {
	if ( r_skipInteractions.GetBool() ) {
		return;
	}

	renderLog.OpenMainBlock( MRB_DRAW_INTERACTIONS );
	renderLog.OpenBlock( "RB_DrawInteractions" );

    GPU_SCOPED_PROFILE();
    
    //
	// for each light, perform shadowing and adding
	//
	for ( const viewLight_t * vLight = backEnd.viewDef->viewLights; vLight != NULL; vLight = vLight->next ) {
		// do fogging later
		if ( vLight->lightShader->IsFogLight() ) {
			continue;
		}
		if ( vLight->lightShader->IsBlendLight() ) {
			continue;
		}

		if ( vLight->localInteractions == NULL && vLight->globalInteractions == NULL && vLight->translucentInteractions == NULL ) {
			continue;
		}

		const idMaterial * lightShader = vLight->lightShader;
		renderLog.OpenBlock( lightShader->GetName() );

		// only need to clear the stencil buffer and perform stencil testing if there are shadows
		const bool performStencilTest = ( vLight->globalShadows != NULL || vLight->localShadows != NULL );

		// mirror flips the sense of the stencil select, and I don't want to risk accidentally breaking it
		// in the normal case, so simply disable the stencil select in the mirror case
		const bool useLightStencilSelect = ( r_useLightStencilSelect.GetBool() && backEnd.viewDef->isMirror == false );

		if ( performStencilTest ) {
			if ( useLightStencilSelect ) {
				// write a stencil mask for the visible light bounds to hi-stencil
				RB_StencilSelectLight( pContext, vLight );
			} else {
				// always clear whole S-Cull tiles
				idScreenRect rect;
				rect.x1 = ( vLight->scissorRect.x1 +  0 ) & ~15;
				rect.y1 = ( vLight->scissorRect.y1 +  0 ) & ~15;
				rect.x2 = ( vLight->scissorRect.x2 + 15 ) & ~15;
				rect.y2 = ( vLight->scissorRect.y2 + 15 ) & ~15;

				if ( !backEnd.currentScissor.Equals( rect ) && r_useScissor.GetBool() ) {
		            D3DDrv_SetScissor( pContext,
					            backEnd.viewDef->viewport.x1 + rect.x1,
								backEnd.viewDef->viewport.y1 + rect.y1,
								rect.x2 + 1 - rect.x1,
								rect.y2 + 1 - rect.y1 );
					backEnd.currentScissor = rect;
				}

                D3DDrv_SetBlendStateFromMask( pContext, 0 );
                D3DDrv_SetDepthStateFromMask( pContext, 0 ); // make sure stencil mask passes for the clear
                D3DDrv_Clear( pContext, CLEAR_STENCIL, nullptr, STENCIL_SHADOW_TEST_VALUE, 0 );
			}
		}

		if ( vLight->globalShadows != NULL ) {
			renderLog.OpenBlock( "Global Light Shadows" );
			RB_StencilShadowPass( pContext, vLight->globalShadows, vLight );
			renderLog.CloseBlock();
		}

		if ( vLight->localInteractions != NULL ) {
			renderLog.OpenBlock( "Local Light Interactions" );
			RB_RenderInteractions( pContext, vLight->localInteractions, vLight, GLS_DEPTHFUNC_EQUAL, performStencilTest );
			renderLog.CloseBlock();
		}

		if ( vLight->localShadows != NULL ) {
			renderLog.OpenBlock( "Local Light Shadows" );
			RB_StencilShadowPass( pContext, vLight->localShadows, vLight );
			renderLog.CloseBlock();
		}

		if ( vLight->globalInteractions != NULL ) {
			renderLog.OpenBlock( "Global Light Interactions" );
			RB_RenderInteractions( pContext, vLight->globalInteractions, vLight, GLS_DEPTHFUNC_EQUAL, performStencilTest );
			renderLog.CloseBlock();
		}


		if ( vLight->translucentInteractions != NULL && !r_skipTranslucent.GetBool() ) {
			renderLog.OpenBlock( "Translucent Interactions" );

			// The depth buffer wasn't filled in for translucent surfaces, so they
			// can never be constrained to perforated surfaces with the depthfunc equal.

			// Translucent surfaces do not receive shadows. This is a case where a
			// shadow buffer solution would work but stencil shadows do not because
			// stencil shadows only affect surfaces that contribute to the view depth
			// buffer and translucent surfaces do not contribute to the view depth buffer.

			RB_RenderInteractions( pContext, vLight->translucentInteractions, vLight, GLS_DEPTHFUNC_LESS, false );

			renderLog.CloseBlock();
		}

		renderLog.CloseBlock();
	}

	// disable stencil shadow test
    D3DDrv_SetBlendStateFromMask( pContext, 0 );
    D3DDrv_SetDepthStateFromMask( pContext, 0 );

	renderLog.CloseBlock();
	renderLog.CloseMainBlock();
}

/*
=============================================================================================

NON-LIT SHADER PASSES

=============================================================================================
*/

/*
*/
BUILTIN_SHADER RB_SelectShaderPassShader( const drawSurf_t* surf, const shaderStage_t* pStage, const uint64 stageGLState ) {
    BUILTIN_SHADER builtInShader = MAX_BUILTIN_SHADERS;

	if ( surf->space->isGuiSurface ) {
		// use special shaders for bink cinematics
		if ( pStage->texture.cinematic ) {
			if ( ( stageGLState & GLS_OVERRIDE ) != 0 ) {
				// This is a hack... Only SWF Guis set GLS_OVERRIDE
				// Old style guis do not, and we don't want them to use the new GUI renederProg
				builtInShader = BUILTIN_SHADER_BINK_GUI;
			} else {
				builtInShader = BUILTIN_SHADER_BINK;
			}
		} else {
			if ( ( stageGLState & GLS_OVERRIDE ) != 0 ) {
				// This is a hack... Only SWF Guis set GLS_OVERRIDE
				// Old style guis do not, and we don't want them to use the new GUI renderProg
				builtInShader = BUILTIN_SHADER_GUI;
			} else {
				if ( surf->jointCache ) {
					builtInShader = BUILTIN_SHADER_TEXTURE_VERTEXCOLOR_SKINNED;
				} else {
					builtInShader = BUILTIN_SHADER_TEXTURE_VERTEXCOLOR;
				}
			}
		}
	} else if ( ( pStage->texture.texgen == TG_SCREEN ) || ( pStage->texture.texgen == TG_SCREEN2 ) ) {
		builtInShader = BUILTIN_SHADER_TEXTURE_TEXGEN_VERTEXCOLOR;
	} else if ( pStage->texture.cinematic ) {
		builtInShader = BUILTIN_SHADER_BINK;
	} else {
		if ( surf->jointCache ) {
			builtInShader = BUILTIN_SHADER_TEXTURE_VERTEXCOLOR_SKINNED;
		} else {
			builtInShader = BUILTIN_SHADER_TEXTURE_VERTEXCOLOR;
		}
	}
    return builtInShader;
}

/*
=====================
RB_DrawShaderPasses

Draw non-light dependent passes

If we are rendering Guis, the drawSurf_t::sort value is a depth offset that can
be multiplied by guiEye for polarity and screenSeparation for scale.
=====================
*/
static int RB_DrawShaderPasses( ID3D11DeviceContext2* pContext, const drawSurf_t * const * const drawSurfs, const int numDrawSurfs ) {
	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

    GPU_SCOPED_PROFILE();
    
    renderLog.OpenBlock( "RB_DrawShaderPasses" );

	backEnd.currentSpace = (const viewEntity_t *)1;	// using NULL makes /analyze think surf->space needs to be checked...

    // @pjb: Todo: given we only ever use idDrawVert we could set that state up here once instead of for each draw call

	int i = 0;
	for ( ; i < numDrawSurfs; i++ ) {
		const drawSurf_t * surf = drawSurfs[i];
		const idMaterial * shader = surf->material;

		if ( !shader->HasAmbient() ) {
			continue;
		}

		if ( shader->IsPortalSky() ) {
			continue;
		}

		// some deforms may disable themselves by setting numIndexes = 0
		if ( surf->numIndexes == 0 ) {
			continue;
		}

		if ( shader->SuppressInSubview() ) {
			continue;
		}

		if ( backEnd.viewDef->isXraySubview && surf->space->entityDef ) {
			if ( surf->space->entityDef->parms.xrayIndex != 2 ) {
				continue;
			}
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( shader->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied ) {
			break;
		}

		renderLog.OpenBlock( shader->GetName() );

		// change the matrix and other space related vars if needed
		if ( surf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = surf->space;

			const viewEntity_t *space = backEnd.currentSpace;

			RB_SetMVP( space->mvp );

			// set eye position in local space
			idVec4 localViewOrigin( 1.0f );
			R_GlobalPointToLocal( space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
			renderProgManager.SetRenderParm( RENDERPARM_LOCALVIEWORIGIN, localViewOrigin.ToFloatPtr() );

			// set model Matrix
			float modelMatrixTranspose[16];
			R_MatrixTranspose( space->modelMatrix, modelMatrixTranspose );
			renderProgManager.SetRenderParms( RENDERPARM_MODELMATRIX_X, modelMatrixTranspose, 4 );

			// Set ModelView Matrix
			float modelViewMatrixTranspose[16];
			R_MatrixTranspose( space->modelViewMatrix, modelViewMatrixTranspose );
			renderProgManager.SetRenderParms( RENDERPARM_MODELVIEWMATRIX_X, modelViewMatrixTranspose, 4 );
		}

		// change the scissor if needed
		if ( !backEnd.currentScissor.Equals( surf->scissorRect ) && r_useScissor.GetBool() ) {
			D3DDrv_SetScissor( pContext,
                               backEnd.viewDef->viewport.x1 + surf->scissorRect.x1, 
						       backEnd.viewDef->viewport.y1 + surf->scissorRect.y1,
						       surf->scissorRect.x2 + 1 - surf->scissorRect.x1,
						       surf->scissorRect.y2 + 1 - surf->scissorRect.y1 );
			backEnd.currentScissor = surf->scissorRect;
		}

		// get the expressions for conditionals / color / texcoords
		const float	*regs = surf->shaderRegisters;

		// set face culling appropriately
        ID3D11RasterizerState* rasterizerState = nullptr;
        if ( surf->space->isGuiSurface ) {
            assert( !shader->TestMaterialFlag(MF_POLYGONOFFSET) );
            // @pjb: todo: line mode
            rasterizerState = D3DDrv_GetRasterizerState( CT_TWO_SIDED, 0 );
        } else {
            rasterizerState = shader->GetRasterizerState(); 
        }

		uint64 surfGLState = surf->extraGLState;

        // if the shader doesn't have it's own rasterizer state, get one
        if ( rasterizerState == nullptr ) {
            rasterizerState = D3DDrv_GetRasterizerState( shader->GetCullType(), surfGLState );
        }

        pContext->RSSetState( rasterizerState );

		for ( int stage = 0; stage < shader->GetNumStages(); stage++ ) {		
			const shaderStage_t *pStage = shader->GetStage(stage);

			// check the enable condition
			if ( regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}

			// skip the stages involved in lighting
			if ( pStage->lighting != SL_AMBIENT ) {
				continue;
			}

			uint64 stageGLState = surfGLState;
			if ( ( surfGLState & GLS_OVERRIDE ) == 0 ) {
				stageGLState |= pStage->drawStateBits;
			}

			// skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
			if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
				continue;
			}

			// see if we are a new-style stage
			newShaderStage_t *newStage = pStage->newStage;
			if ( newStage != NULL ) 
            {
	            if ( r_skipNewAmbient.GetBool() ) {
		            continue;
	            }
                RB_DrawStageCustomVFP( pContext, surf, newStage, stageGLState );
			}
            else 
            {
                BUILTIN_SHADER shader = RB_SelectShaderPassShader( surf, pStage, stageGLState );

                // Bind the images for this shader
                // @pjb: is this a safe constant?
                idImage* pImages[16];
                pImages[0] = nullptr;
	            int numImages = RB_PrepareStageTexturing( pStage, surf, &shader, pImages );
                RB_BindImages( pContext, pImages, 0, numImages );

			    RB_DrawStageBuiltInVFP( pContext, shader, surf, pStage, stageGLState );
            }
		}

		renderLog.CloseBlock();
	}

	renderLog.CloseBlock();
	return i;
}

/*
=========================================================================================================

BACKEND COMMANDS

=========================================================================================================
*/
/*
==================
RB_DrawViewInternal
==================
*/
void RB_DrawViewInternal( ID3D11DeviceContext2* pContext, const viewDef_t * viewDef ) {
    GPU_SCOPED_PROFILE();
    
    renderLog.OpenBlock( "RB_DrawViewInternal" );

	//-------------------------------------------------
	// guis can wind up referencing purged images that need to be loaded.
	// this used to be in the gui emit code, but now that it can be running
	// in a separate thread, it must not try to load images, so do it here.
	//-------------------------------------------------
    drawSurf_t **drawSurfs = (drawSurf_t **)&viewDef->drawSurfs[0];
	const int numDrawSurfs = viewDef->numDrawSurfs;

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t * ds = viewDef->drawSurfs[ i ];
		if ( ds->material != NULL ) {
			const_cast<idMaterial *>( ds->material )->EnsureNotPurged();
		}
	}

	//-------------------------------------------------
	// RB_BeginDrawingView
	//
	// Any mirrored or portaled views have already been drawn, so prepare
	// to actually render the visible surfaces for this view
	//
	// clear the z buffer, set the projection matrix, etc
	//-------------------------------------------------

	// set the window clipping
	D3DDrv_SetViewport( 
        pContext,
        viewDef->viewport.x1,
		viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );

	// the scissor may be smaller than the viewport for subviews
	D3DDrv_SetScissor( 
        pContext,
        backEnd.viewDef->viewport.x1 + viewDef->scissor.x1,
		backEnd.viewDef->viewport.y1 + viewDef->scissor.y1,
		viewDef->scissor.x2 + 1 - viewDef->scissor.x1,
		viewDef->scissor.y2 + 1 - viewDef->scissor.y1 );
	backEnd.currentScissor = viewDef->scissor;

	// Clear the depth buffer and clear the stencil to 128 for stencil shadows as well as gui masking
	D3DDrv_Clear( pContext, CLEAR_DEPTH | CLEAR_STENCIL, nullptr, STENCIL_SHADOW_TEST_VALUE, 1 );
    
	//------------------------------------
	// sets variables that can be used by all programs
	//------------------------------------
	{
		//
		// set eye position in global space
		//
		float parm[4];
		parm[0] = backEnd.viewDef->renderView.vieworg[0];
		parm[1] = backEnd.viewDef->renderView.vieworg[1];
		parm[2] = backEnd.viewDef->renderView.vieworg[2];
		parm[3] = 1.0f;
		renderProgManager.SetRenderParm( RENDERPARM_GLOBALEYEPOS, parm ); // rpGlobalEyePos

		// sets overbright to make world brighter
		// This value is baked into the specularScale and diffuseScale values so
		// the interaction programs don't need to perform the extra multiply,
		// but any other renderprogs that want to obey the brightness value
		// can reference this.
		float overbright = r_lightScale.GetFloat() * 0.5f;
		parm[0] = overbright;
		parm[1] = overbright;
		parm[2] = overbright;
		parm[3] = overbright;
		renderProgManager.SetRenderParm( RENDERPARM_OVERBRIGHT, parm );

		// Set Projection Matrix
		float projMatrixTranspose[16];
		R_MatrixTranspose( backEnd.viewDef->projectionMatrix, projMatrixTranspose );
		renderProgManager.SetRenderParms( RENDERPARM_PROJMATRIX_X, projMatrixTranspose, 4 );
	}	

	//-------------------------------------------------
	// draw the depth pass
	// if we are just doing 2D rendering, no need to fill the depth buffer
	//-------------------------------------------------
	if ( backEnd.viewDef->viewEntitys != NULL && numDrawSurfs > 0 ) {
        RB_FillDepthBufferFast( pContext, drawSurfs, numDrawSurfs );
    }

	//-------------------------------------------------
	// draw the lit material passes
	//-------------------------------------------------
	RB_DrawInteractions( pContext );

	//-------------------------------------------------
	// now draw any non-light dependent shading passes
	//-------------------------------------------------
	int processed = 0;
	if ( !r_skipShaderPasses.GetBool() ) {
		renderLog.OpenMainBlock( MRB_DRAW_SHADER_PASSES );
		processed = RB_DrawShaderPasses( pContext, drawSurfs, numDrawSurfs );
		renderLog.CloseMainBlock();
	}

	renderLog.CloseBlock();
}

/*
==================
RB_MotionBlur

Experimental feature
==================
*/
void RB_MotionBlur( ID3D11DeviceContext2* pContext ) {
    if ( !backEnd.viewDef->viewEntitys ) {
		// 3D views only
		return;
	}
	if ( r_motionBlur.GetInteger() <= 0 ) {
		return;
	}
	if ( backEnd.viewDef->isSubview ) {
		return;
	}

    GPU_SCOPED_PROFILE();
    
    // clear the alpha buffer and draw only the hands + weapon into it so
	// we can avoid blurring them
    D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHMASK );
    D3DDrv_SetBlendStateFromMask( pContext, GLS_COLORMASK );

    // @pjb: D3D doesn't support clear with color mask, so we have to clear manually using a fullscreen quad
    pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_MOTION_BLUR ), nullptr, 0 );
    pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( BUILTIN_SHADER_COLOR ), nullptr, 0 );

    float clearCol[] = { 1, 1, 1, 0 }; // rgb is masked off but set to 255 for debugging purposes
	renderProgManager.SetRenderParm( RENDERPARM_COLOR, clearCol );
	RB_DrawElementsWithCounters( pContext, &backEnd.unitSquareSurface );
    // @pjb - end

    float renderCol[] = { 0, 0, 0, 0 };
    renderProgManager.SetRenderParm( RENDERPARM_COLOR, renderCol );

    RB_BindImages( pContext, &globalImages->blackImage, 0, 1 );

    backEnd.currentSpace = NULL;

	drawSurf_t **drawSurfs = (drawSurf_t **)&backEnd.viewDef->drawSurfs[0];
	for ( int surfNum = 0; surfNum < backEnd.viewDef->numDrawSurfs; surfNum++ ) {
		const drawSurf_t * surf = drawSurfs[ surfNum ];

		if ( !surf->space->weaponDepthHack && !surf->space->skipMotionBlur && !surf->material->HasSubview() ) {
			// Apply motion blur to this object
			continue;
		}

		const idMaterial * shader = surf->material;
		if ( shader->Coverage() == MC_TRANSLUCENT ) {
			// muzzle flash, etc
			continue;
		}

		// set mvp matrix
		if ( surf->space != backEnd.currentSpace ) {
			RB_SetMVP( surf->space->mvp );
			backEnd.currentSpace = surf->space;
		}

        pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR ), nullptr, 0 );

		// this could just be a color, but we don't have a skinned color-only prog
		if ( surf->jointCache ) {
            pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR_SKINNED ), nullptr, 0 );
		} else {
            pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR ), nullptr, 0 );
		}

		// draw it solid
		RB_DrawElementsWithCounters( pContext, surf );
	}

    // copy off the color buffer and the depth buffer for the motion blur prog
	// we use the viewport dimensions for copying the buffers in case resolution scaling is enabled.
	const idScreenRect & viewport = backEnd.viewDef->viewport;
    globalImages->currentRenderImage->CopyFramebuffer( viewport.x1, viewport.y1, viewport.GetWidth(), viewport.GetHeight() );

	// in stereo rendering, each eye needs to get a separate previous frame mvp
	int mvpIndex = ( backEnd.viewDef->renderView.viewEyeBuffer == 1 ) ? 1 : 0;

	// derive the matrix to go from current pixels to previous frame pixels
	idRenderMatrix	inverseMVP;
	idRenderMatrix::Inverse( backEnd.viewDef->worldSpace.mvp, inverseMVP );

	idRenderMatrix	motionMatrix;
	idRenderMatrix::Multiply( backEnd.prevMVP[mvpIndex], inverseMVP, motionMatrix );

	backEnd.prevMVP[mvpIndex] = backEnd.viewDef->worldSpace.mvp;

	RB_SetMVP( motionMatrix );

    D3DDrv_SetBlendStateFromMask( pContext, GLS_DEFAULT );
    D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHFUNC_ALWAYS );
    D3DDrv_SetRasterizerStateFromMask( pContext, CT_TWO_SIDED, GLS_DEFAULT );

    pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_MOTION_BLUR ), nullptr, 0 );
    pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( BUILTIN_SHADER_MOTION_BLUR ), nullptr, 0 );

	// let the fragment program know how many samples we are going to use
	idVec4 samples( (float)( 1 << r_motionBlur.GetInteger() ) );
	renderProgManager.SetRenderParm( RENDERPARM_OVERBRIGHT, samples.ToFloatPtr() );

    idImage* pImages[] = {
        globalImages->currentRenderImage,
        globalImages->currentDepthImage
    };

    RB_BindImages( pContext, pImages, 0, _countof( pImages ) );

	RB_DrawElementsWithCounters( pContext, &backEnd.unitSquareSurface );
}

/*
==================
RB_DrawView
==================
*/
void RB_DrawView( ID3D11DeviceContext2* pContext, const void *data ) {
    const drawSurfsCommand_t * cmd = (const drawSurfsCommand_t *)data;

	backEnd.viewDef = cmd->viewDef;

	// we will need to do a new copyTexSubImage of the screen
	// when a SS_POST_PROCESS material is used
	backEnd.currentRenderCopied = false;

	// if there aren't any drawsurfs, do nothing
	if ( !backEnd.viewDef->numDrawSurfs ) {
		return;
	}

	// skip render bypasses everything that has models, assuming
	// them to be 3D views, but leaves 2D rendering visible
	if ( r_skipRender.GetBool() && backEnd.viewDef->viewEntitys ) {
		return;
	}

	backEnd.pc.c_surfaces += backEnd.viewDef->numDrawSurfs;

	// render the scene
	RB_DrawViewInternal( pContext, cmd->viewDef );

	RB_MotionBlur( pContext );
}

/*
==================
RB_CopyRender

Copy part of the current framebuffer to an image
==================
*/
void RB_CopyRender( ID3D11DeviceContext2* pContext, const void *data ) {
	const copyRenderCommand_t * cmd = (const copyRenderCommand_t *)data;

	if ( r_skipCopyTexture.GetBool() ) {
		return;
	}

    GPU_SCOPED_PROFILE();
    
    RENDERLOG_PRINTF( "***************** RB_CopyRender *****************\n" );

	if ( cmd->image ) {
		cmd->image->CopyFramebuffer( cmd->x, cmd->y, cmd->imageWidth, cmd->imageHeight );
	}

	if ( cmd->clearColorAfterCopy ) {
		D3DDrv_Clear( pContext, CLEAR_COLOR, nullptr, STENCIL_SHADOW_TEST_VALUE, 0 );
	}
}

/*
==================
RB_PostProcess

==================
*/
extern idCVar rs_enable;
void RB_PostProcess( ID3D11DeviceContext2* pContext, const void * data ) {
    GPU_SCOPED_PROFILE();
    /* @pjb: todo: post processing


	// only do the post process step if resolution scaling is enabled. Prevents the unnecessary copying of the framebuffer and
	// corresponding full screen quad pass.
	if ( rs_enable.GetInteger() == 0 ) { 
		return;
	}

	// resolve the scaled rendering to a temporary texture
	postProcessCommand_t * cmd = (postProcessCommand_t *)data;
	const idScreenRect & viewport = cmd->viewDef->viewport;
	globalImages->currentRenderImage->CopyFramebuffer( viewport.x1, viewport.y1, viewport.GetWidth(), viewport.GetHeight() );

    D3DDrv_SetBlendStateFromMask( pContext, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
    D3DDrv_SetDepthStateFromMask( pContext, GLS_DEPTHMASK | GLS_DEPTHFUNC_ALWAYS );
    D3DDrv_SetRasterizerStateFromMask( pContext, CT_TWO_SIDED, 0 );

	int screenWidth = renderSystem->GetWidth();
	int screenHeight = renderSystem->GetHeight();

	// set the window clipping
	D3DDrv_SetViewport( pContext, 0, 0, screenWidth, screenHeight );
	D3DDrv_SetScissor( pContext, 0, 0, screenWidth, screenHeight );

    RB_BindImages( pContext, &globalImages->currentRenderImage, 0, 1 );

	// Draw
	RB_DrawElementsWithCounters( pContext, &backEnd.unitSquareSurface );

	renderLog.CloseBlock();

     */
}

/*
====================
RB_ExecuteBackEndCommands

This function will be called syncronously if running without
smp extensions, or asyncronously by another thread.
====================
*/
void RB_ExecuteBackEndCommands( const emptyCommand_t *cmds ) {

    GPU_SCOPED_PROFILE();

    // r_debugRenderToTexture
	int c_draw3d = 0;
	int c_draw2d = 0;
	int c_setBuffers = 0;
	int c_copyRenders = 0;

	resolutionScale.SetCurrentGPUFrameTime( commonLocal.GetRendererGPUMicroseconds() );

	renderLog.StartFrame();

	if ( cmds->commandId == RC_NOP && !cmds->next ) {
		return;
	}

    // Do we need to restart the state?
    if ( r_shadowPolygonFactor.IsModified() ||
         r_shadowPolygonOffset.IsModified() ||
         r_offsetFactor.IsModified() ||
         r_offsetUnits.IsModified() ) {
        // The user changed a cvar that affects our state blocks
        D3DDrv_RegenerateStateBlocks();

        // Clear modified flag
        r_shadowPolygonFactor.ClearModified();
        r_shadowPolygonOffset.ClearModified();
        r_offsetFactor.ClearModified();
        r_offsetUnits.ClearModified();
    }

    ID3D11DeviceContext2* pContext = D3DDrv_GetImmediateContext();

	uint64 backEndStartTime = Sys_Microseconds();

	for ( ; cmds != NULL; cmds = (const emptyCommand_t *)cmds->next ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW_3D:
		case RC_DRAW_VIEW_GUI:
			RB_DrawView( pContext, cmds );
			if ( ((const drawSurfsCommand_t *)cmds)->viewDef->viewEntitys ) {
				c_draw3d++;
			} else {
				c_draw2d++;
			}
			break;
		case RC_SET_BUFFER:
			c_setBuffers++;
			break;
		case RC_COPY_RENDER:
			RB_CopyRender( pContext, cmds );
			c_copyRenders++;
			break;
		case RC_POST_PROCESS:
			RB_PostProcess( pContext, cmds );
			break;
		default:
			common->Error( "RB_ExecuteBackEndCommands: bad commandId" );
			break;
		}
	}

	D3DDrv_Flush( pContext );

	// stop rendering on this thread
	uint64 backEndFinishTime = Sys_Microseconds();
	backEnd.pc.totalMicroSec = backEndFinishTime - backEndStartTime;

	if ( r_debugRenderToTexture.GetInteger() == 1 ) {
		common->Printf( "3d: %i, 2d: %i, SetBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_copyRenders, backEnd.pc.c_copyFrameBuffer );
		backEnd.pc.c_copyFrameBuffer = 0;
	}
	renderLog.EndFrame();
}
