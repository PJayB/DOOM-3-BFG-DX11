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

idCVar r_motionBlur( "r_motionBlur", "0", CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE, "1 - 5, log2 of the number of motion blur samples" );
idCVar r_skipShaderPasses( "r_skipShaderPasses", "0", CVAR_RENDERER | CVAR_BOOL, "" );

backEndState_t	backEnd;

/*
================
RB_BindImages
================
*/
void RB_BindImages( ID3D11DeviceContext1* pContext, idImage** pImages, int numImages )
{
    ID3D11ShaderResourceView* pSRVs[16];
    ID3D11SamplerState* pSamplers[16];

    assert( numImages < _countof( pSRVs ) );

    for ( int i = 0; i < numImages; ++i )
    {
        pSRVs[i] = pImages[i]->GetSRV();
        pSamplers[i] = pImages[i]->GetSampler();
    }

    pContext->PSSetShaderResources( 0, numImages, pSRVs );
    pContext->PSSetSamplers( 0, numImages, pSamplers );
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
void RB_DrawElementsWithCounters( ID3D11DeviceContext1* pContext, const drawSurf_t *surf ) {
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
    pContext->IASetPrimitiveTopology( D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    
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
================
RB_PrepareStageTexturing
================
*/
static int RB_PrepareStageTexturing( 
    const shaderStage_t *pStage, 
    const drawSurf_t *surf, 
    idRenderProgManager::BUILTIN_SHADER *shaderToUse,
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
			    *shaderToUse = idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR;
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
				*shaderToUse = idRenderProgManager::BUILTIN_BUMPY_ENVIRONMENT_SKINNED;
			} else {
				*shaderToUse = idRenderProgManager::BUILTIN_BUMPY_ENVIRONMENT;
			}
		} else {
			RENDERLOG_PRINTF( "TexGen: TG_REFLECT_CUBE: Environment\n" );
			if ( surf->jointCache ) {
                *shaderToUse = idRenderProgManager::BUILTIN_ENVIRONMENT_SKINNED;
			} else {
                *shaderToUse = idRenderProgManager::BUILTIN_ENVIRONMENT;
			}
		}

	} else if ( pStage->texture.texgen == TG_SKYBOX_CUBE ) {

        *shaderToUse = idRenderProgManager::BUILTIN_SKYBOX;

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
        *shaderToUse = idRenderProgManager::BUILTIN_WOBBLESKY;

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
=========================================================================================

DEPTH BUFFER RENDERING

=========================================================================================
*/


/*
==================
RB_FillDepthBufferGeneric
==================
*/
static void RB_FillDepthBufferGeneric( ID3D11DeviceContext1* pContext, const drawSurf_t * const * drawSurfs, int numDrawSurfs ) {

    idImage* pImages[16];

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

                idRenderProgManager::BUILTIN_SHADER shaderToUse = 
				    ( drawSurf->jointCache ) ?
                    idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED :
                    idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR;

				RB_SetVertexColorParms( SVC_IGNORE );

				// bind the textures
                pImages[0] = pStage->texture.image;
                int numImages = RB_PrepareStageTexturing( pStage, drawSurf, &shaderToUse, pImages );
                RB_BindImages( pContext, pImages, numImages );

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
			}

			if ( !didDraw ) {
				drawSolid = true;
			}
		}

		// draw the entire surface solid
		if ( drawSolid ) {

            idRenderProgManager::BUILTIN_SHADER shaderToUse;
			if ( shader->GetSort() == SS_SUBVIEW ) {
				shaderToUse = idRenderProgManager::BUILTIN_COLOR;
                renderProgManager.SetRenderParm( RENDERPARM_COLOR, color );
			} else {
				if ( drawSurf->jointCache ) {
					shaderToUse = idRenderProgManager::BUILTIN_DEPTH_SKINNED;
				} else {
					shaderToUse = idRenderProgManager::BUILTIN_DEPTH;
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
static void RB_FillDepthBufferFast( ID3D11DeviceContext1* pContext, drawSurf_t **drawSurfs, int numDrawSurfs ) {
	renderLog.OpenMainBlock( MRB_FILL_DEPTH_BUFFER );
	renderLog.OpenBlock( "RB_FillDepthBufferFast" );

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

        idRenderProgManager::BUILTIN_SHADER shaderToUse =
		    ( surf->jointCache ) ?
			idRenderProgManager::BUILTIN_DEPTH_SKINNED :
    		idRenderProgManager::BUILTIN_DEPTH;

        // bind the shaders
        pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( shaderToUse ), nullptr, 0 );
        pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( shaderToUse ), nullptr, 0 );

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
=============================================================================================

LIT SHADER PASSES

=============================================================================================
*/
static void RB_DrawShaderPassStage_New( 
    ID3D11DeviceContext1* pContext, 
    const drawSurf_t* surf,
    const newShaderStage_t *newStage, 
    uint64 stageGLState );

static void RB_DrawShaderPassStage_Old( 
    ID3D11DeviceContext1* pContext, 
    const drawSurf_t *surf, 
    const shaderStage_t *pStage, 
    uint64 stageGLState );

static void RB_DrawMaterialPasses( ID3D11DeviceContext1* pContext, const drawSurf_t * const * drawSurfs, int numDrawSurfs ) {

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t * drawSurf = drawSurfs[i];
		const idMaterial * shader = drawSurf->material;

		// some deforms may disable themselves by setting numIndexes = 0
		if ( drawSurf->numIndexes == 0 ) {
			continue;
		}

		// we need to draw the post process shaders after we have drawn the fog lights
		if ( shader->GetSort() >= SS_POST_PROCESS && !backEnd.currentRenderCopied ) {
			break;
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

		// change the matrix and other space related vars if needed
		if ( drawSurf->space != backEnd.currentSpace ) {
			backEnd.currentSpace = drawSurf->space;

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
		if ( !backEnd.currentScissor.Equals( drawSurf->scissorRect ) && r_useScissor.GetBool() ) {
			D3DDrv_SetScissor( pContext,
                               backEnd.viewDef->viewport.x1 + drawSurf->scissorRect.x1, 
						       backEnd.viewDef->viewport.y1 + drawSurf->scissorRect.y1,
						       drawSurf->scissorRect.x2 + 1 - drawSurf->scissorRect.x1,
						       drawSurf->scissorRect.y2 + 1 - drawSurf->scissorRect.y1 );
			backEnd.currentScissor = drawSurf->scissorRect;
		}

		uint64 surfGLState = 0;

		renderLog.OpenBlock( shader->GetName() );

		// perforated surfaces may have multiple alpha tested stages
		for ( stage = 0; stage < shader->GetNumStages(); stage++ ) {		
			const shaderStage_t *pStage = shader->GetStage(stage);

			// check the stage enable condition
			if ( regs[ pStage->conditionRegister ] == 0 ) {
				continue;
			}

			// skip the stages not involved in lighting
			if ( pStage->lighting == SL_AMBIENT ) {
				continue;
			}

			uint64 stageGLState = surfGLState;

			if ( ( surfGLState & GLS_OVERRIDE ) == 0 ) {
				stageGLState |= pStage->drawStateBits;
			}

			if ( pStage->hasAlphaTest ) {
			    // skip the entire stage if alpha would be black
                if ( pStage->color.registers[3] <= 0.0f ) {
				    continue;
                }
			    // skip if the stage is ( GL_ZERO, GL_ONE ), which is used for some alpha masks
			    if ( ( stageGLState & ( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS ) ) == ( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE ) ) {
				    continue;
			    }
			}
            
			// see if we are a new-style stage
			newShaderStage_t *newStage = pStage->newStage;
			if ( newStage != NULL ) 
            {
                RB_DrawShaderPassStage_New( pContext, drawSurf, newStage, stageGLState );
			}
            else 
            {
			    RB_DrawShaderPassStage_Old( pContext, drawSurf, pStage, stageGLState );
            }
		}

		renderLog.CloseBlock();
	}

    renderProgManager.SetRenderParm( RENDERPARM_ALPHA_TEST, vec4_zero.ToFloatPtr() );
}


/*
=============================================================================================

NON-LIT SHADER PASSES

=============================================================================================
*/

/*
=====================
RB_DrawShaderPassStage_New

new style stages
=====================
*/
static void RB_DrawShaderPassStage_New( 
    ID3D11DeviceContext1* pContext, 
    const drawSurf_t* surf,
    const newShaderStage_t *newStage, 
    uint64 stageGLState ) {

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
    RB_BindImages( pContext, pImages, newStage->numFragmentProgramImages );

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
RB_DrawShaderPassStage_Old
=====================
*/
static void RB_DrawShaderPassStage_Old( ID3D11DeviceContext1* pContext, const drawSurf_t *surf, const shaderStage_t *pStage, uint64 stageGLState ) {

    // @pjb: is this a safe constant?
    idImage* pImages[16];

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

	stageVertexColor_t svc = pStage->vertexColor;

	renderLog.OpenBlock( "Old Shader Stage" );

    renderProgManager.SetRenderParm( RENDERPARM_COLOR, color );

    idRenderProgManager::BUILTIN_SHADER builtInShader = idRenderProgManager::MAX_BUILTINS;

	if ( surf->space->isGuiSurface ) {
		// Force gui surfaces to always be SVC_MODULATE
		svc = SVC_MODULATE;

		// use special shaders for bink cinematics
		if ( pStage->texture.cinematic ) {
			if ( ( stageGLState & GLS_OVERRIDE ) != 0 ) {
				// This is a hack... Only SWF Guis set GLS_OVERRIDE
				// Old style guis do not, and we don't want them to use the new GUI renederProg
				builtInShader = idRenderProgManager::BUILTIN_BINK_GUI;
			} else {
				builtInShader = idRenderProgManager::BUILTIN_BINK;
			}
		} else {
			if ( ( stageGLState & GLS_OVERRIDE ) != 0 ) {
				// This is a hack... Only SWF Guis set GLS_OVERRIDE
				// Old style guis do not, and we don't want them to use the new GUI renderProg
				builtInShader = idRenderProgManager::BUILTIN_GUI;
			} else {
				if ( surf->jointCache ) {
					builtInShader = idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED;
				} else {
					builtInShader = idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR;
				}
			}
		}
	} else if ( ( pStage->texture.texgen == TG_SCREEN ) || ( pStage->texture.texgen == TG_SCREEN2 ) ) {
		builtInShader = idRenderProgManager::BUILTIN_TEXTURE_TEXGEN_VERTEXCOLOR;
	} else if ( pStage->texture.cinematic ) {
		builtInShader = idRenderProgManager::BUILTIN_BINK;
	} else {
		if ( surf->jointCache ) {
			builtInShader = idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED;
		} else {
			builtInShader = idRenderProgManager::BUILTIN_TEXTURE_VERTEXCOLOR;
		}
	}

    RB_SetVertexColorParms( svc );

	// set the state
    D3DDrv_SetBlendStateFromMask( pContext, stageGLState );
    D3DDrv_SetDepthStateFromMask( pContext, stageGLState );
		
    pImages[0] = nullptr;
	int numImages = RB_PrepareStageTexturing( pStage, surf, &builtInShader, pImages );
    RB_BindImages( pContext, pImages, numImages );
		
    pContext->VSSetShader( renderProgManager.GetBuiltInVertexShader( builtInShader ), nullptr, 0 );
    pContext->PSSetShader( renderProgManager.GetBuiltInPixelShader( builtInShader ), nullptr, 0 );

	// draw it
	RB_DrawElementsWithCounters( pContext, surf );

	renderLog.CloseBlock();
}

/*
=====================
RB_DrawShaderPasses

Draw non-light dependent passes

If we are rendering Guis, the drawSurf_t::sort value is a depth offset that can
be multiplied by guiEye for polarity and screenSeparation for scale.
=====================
*/
static int RB_DrawShaderPasses( ID3D11DeviceContext1* pContext, const drawSurf_t * const * const drawSurfs, const int numDrawSurfs ) {
	// only obey skipAmbient if we are rendering a view
	if ( backEnd.viewDef->viewEntitys && r_skipAmbient.GetBool() ) {
		return numDrawSurfs;
	}

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
                RB_DrawShaderPassStage_New( pContext, surf, newStage, stageGLState );
			}
            else 
            {
			    RB_DrawShaderPassStage_Old( pContext, surf, pStage, stageGLState );
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
void RB_DrawViewInternal( const viewDef_t * viewDef ) {
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

    ID3D11DeviceContext1* pContext = D3DDrv_GetImmediateContext();

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
	if ( backEnd.viewDef->viewEntitys != NULL && numDrawSurfs > 0 ) {
        RB_DrawMaterialPasses( pContext, drawSurfs, numDrawSurfs );
    }

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
void RB_MotionBlur() {
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

	// @pjb: todo
}

/*
==================
RB_DrawView
==================
*/
void RB_DrawView( const void *data ) {
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
	RB_DrawViewInternal( cmd->viewDef );

	RB_MotionBlur();
}

/*
==================
RB_CopyRender

Copy part of the current framebuffer to an image
==================
*/
void RB_CopyRender( const void *data ) {
	const copyRenderCommand_t * cmd = (const copyRenderCommand_t *)data;

	if ( r_skipCopyTexture.GetBool() ) {
		return;
	}

	RENDERLOG_PRINTF( "***************** RB_CopyRender *****************\n" );

	if ( cmd->image ) {
		cmd->image->CopyFramebuffer( cmd->x, cmd->y, cmd->imageWidth, cmd->imageHeight );
	}

	if ( cmd->clearColorAfterCopy ) {
		D3DDrv_Clear( D3DDrv_GetImmediateContext(), CLEAR_COLOR, nullptr, STENCIL_SHADOW_TEST_VALUE, 0 );
	}
}

/*
==================
RB_PostProcess

==================
*/
extern idCVar rs_enable;
void RB_PostProcess( const void * data ) {

	// only do the post process step if resolution scaling is enabled. Prevents the unnecessary copying of the framebuffer and
	// corresponding full screen quad pass.
	if ( rs_enable.GetInteger() == 0 ) { 
		return;
	}

	// @pjb: todo
}

/*
====================
RB_ExecuteBackEndCommands

This function will be called syncronously if running without
smp extensions, or asyncronously by another thread.
====================
*/
void RB_ExecuteBackEndCommands( const emptyCommand_t *cmds ) {
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

	uint64 backEndStartTime = Sys_Microseconds();

	for ( ; cmds != NULL; cmds = (const emptyCommand_t *)cmds->next ) {
		switch ( cmds->commandId ) {
		case RC_NOP:
			break;
		case RC_DRAW_VIEW_3D:
		case RC_DRAW_VIEW_GUI:
			RB_DrawView( cmds );
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
			RB_CopyRender( cmds );
			c_copyRenders++;
			break;
		case RC_POST_PROCESS:
			RB_PostProcess( cmds );
			break;
		default:
			common->Error( "RB_ExecuteBackEndCommands: bad commandId" );
			break;
		}
	}

	D3DDrv_Flush( D3DDrv_GetImmediateContext() );

	// stop rendering on this thread
	uint64 backEndFinishTime = Sys_Microseconds();
	backEnd.pc.totalMicroSec = backEndFinishTime - backEndStartTime;

	if ( r_debugRenderToTexture.GetInteger() == 1 ) {
		common->Printf( "3d: %i, 2d: %i, SetBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_copyRenders, backEnd.pc.c_copyFrameBuffer );
		backEnd.pc.c_copyFrameBuffer = 0;
	}
	renderLog.EndFrame();
}
