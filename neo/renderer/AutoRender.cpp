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

const int AUTO_RENDER_STACK_SIZE = 256 * 1024;

void RB_BindImages( ID3D11DeviceContext2* pContext, idImage** pImages, int offset, int numImages );

idAutoRender rAutoRender;

/*
============================
idAutoRender::idAutoRender
============================
*/
idAutoRender::idAutoRender() {
	nextRotateTime = 0.0f;
	currentRotation = 0.0f;
	autoRenderIcon = AUTORENDER_DEFAULTICON;
}

/*
============================
idAutoRender::Run
============================
*/
int idAutoRender::Run() {
	while ( !IsTerminating() ) {
		RenderFrame();
	}


	return 0;
}

/*
============================
idAutoRender::StartBackgroundAutoSwaps
============================
*/
void idAutoRender::StartBackgroundAutoSwaps( autoRenderIconType_t iconType ) {


	if ( IsRunning() ) {
		EndBackgroundAutoSwaps();
	}

	autoRenderIcon = iconType;

	idLib::Printf("Starting Background AutoSwaps\n");

	const bool captureToImage = true;
	common->UpdateScreen( captureToImage );

	StartThread("BackgroundAutoSwaps", CORE_0B, THREAD_NORMAL, AUTO_RENDER_STACK_SIZE );
}

/*
============================
idAutoRender::EndBackgroundAutoSwaps
============================
*/
void idAutoRender::EndBackgroundAutoSwaps() {
	idLib::Printf("End Background AutoSwaps\n");

	StopThread();

}

/*
============================
idAutoRender::RenderFrame
============================
*/
void idAutoRender::RenderFrame() {
	// values are 0 to 1
	float loadingIconPosX = 0.5f;
	float loadingIconPosY = 0.6f;
	float loadingIconScale = 0.025f;
	float loadingIconSpeed = 0.095f;

	if ( autoRenderIcon == AUTORENDER_HELLICON ) {
		loadingIconPosX = 0.85f;
		loadingIconPosY = 0.85f;
		loadingIconScale = 0.1f;
		loadingIconSpeed = 0.095f;
	} else if ( autoRenderIcon == AUTORENDER_DIALOGICON ) {
		loadingIconPosY = 0.73f;
	}

    ID3D11DeviceContext2* pContext = D3DDrv_GetImmediateContext();

	const bool stereoRender = false;

	const int width = renderSystem->GetWidth();
	const int height = renderSystem->GetHeight();
	const int guardBand = height / 24;

	if ( stereoRender ) {
		for ( int viewNum = 0 ; viewNum < 2; viewNum++ ) {
            int y = viewNum * ( height + guardBand );
		    D3DDrv_SetViewport( pContext, 0, y, width, height );
            D3DDrv_SetScissor( pContext, 0, y, width, height );
			RenderBackground();
			RenderLoadingIcon( loadingIconPosX, loadingIconPosY, loadingIconScale, loadingIconSpeed );
		}
	} else {
		D3DDrv_SetViewport( pContext, 0, 0, width, height );
        D3DDrv_SetScissor( pContext, 0, 0, width, height );
		RenderBackground();
		RenderLoadingIcon( loadingIconPosX, loadingIconPosY, loadingIconScale, loadingIconSpeed );
	}

}

/*
============================
idAutoRender::RenderBackground
============================
*/
void idAutoRender::RenderBackground() {

    auto pIC = D3DDrv_GetImmediateContext();

    idImage* pImages[] = { 
        globalImages->currentRenderImage
    };

    RB_BindImages( pIC, pImages, 0, _countof(pImages) );

    D3DDrv_SetRasterizerStateFromMask( pIC, CT_TWO_SIDED, 0 );
    D3DDrv_SetBlendStateFromMask( pIC, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );
    D3DDrv_SetDepthStateFromMask( pIC, GLS_DEPTHFUNC_ALWAYS );

	float mvpMatrix[16] = { 0 };
	mvpMatrix[0] = 1;
	mvpMatrix[5] = 1;
	mvpMatrix[10] = 1;
	mvpMatrix[15] = 1;

	// Set Parms
	float texS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float texT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	renderProgManager.SetRenderParm( RENDERPARM_TEXTUREMATRIX_S, texS );
	renderProgManager.SetRenderParm( RENDERPARM_TEXTUREMATRIX_T, texT );

	// disable texgen
	float texGenEnabled[4] = { 0, 0, 0, 0 };
	renderProgManager.SetRenderParm( RENDERPARM_TEXGEN_0_ENABLED, texGenEnabled );

	// set matrix
	renderProgManager.SetRenderParms( RENDERPARM_MVPMATRIX_X, mvpMatrix, 4 );

    pIC->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR ), nullptr, 0 );
    pIC->PSSetShader( renderProgManager.GetBuiltInPixelShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR ), nullptr, 0 );

	RB_DrawElementsWithCounters( pIC, &backEnd.unitSquareSurface );
}

/*
============================
idAutoRender::RenderLoadingIcon
============================
*/
void idAutoRender::RenderLoadingIcon( float fracX, float fracY, float size, float speed ) {
	float s = 0.0f; 
	float c = 1.0f;

    if ( autoRenderIcon != AUTORENDER_HELLICON ) {
		if ( Sys_Milliseconds() >= nextRotateTime ) {
			nextRotateTime = Sys_Milliseconds() + 100;
			currentRotation -= 90.0f;
		}
		float angle = DEG2RAD( currentRotation );
		idMath::SinCos( angle, s, c );
	}

	const float pixelAspect = renderSystem->GetPixelAspect();
	const float screenWidth = renderSystem->GetWidth();
	const float screenHeight = renderSystem->GetHeight();

	const float minSize = Min( screenWidth, screenHeight );
	if ( minSize <= 0.0f ) {
		return;
	}

	float scaleX = size * minSize / screenWidth;
	float scaleY = size * minSize / screenHeight;

	float scale[16] = { 0 };
	scale[0] = c * scaleX / pixelAspect;
	scale[1] = -s * scaleY;
	scale[4] = s * scaleX / pixelAspect;
	scale[5] = c * scaleY;
	scale[10] = 1.0f;
	scale[15] = 1.0f;

	scale[12] = fracX;
	scale[13] = fracY;

	float ortho[16] = { 0 };
	ortho[0] = 2.0f;
	ortho[5] = -2.0f;
	ortho[10] = -2.0f;
	ortho[12] = -1.0f;
	ortho[13] = 1.0f;
	ortho[14] = -1.0f;
	ortho[15] = 1.0f;

	float finalOrtho[16];
	R_MatrixMultiply( scale, ortho, finalOrtho );

	float projMatrixTranspose[16];
	R_MatrixTranspose( finalOrtho, projMatrixTranspose );
	renderProgManager.SetRenderParms( RENDERPARM_MVPMATRIX_X, projMatrixTranspose, 4 );

	float a = 1.0f;
	if ( autoRenderIcon == AUTORENDER_HELLICON ) {
		float alpha = DEG2RAD( Sys_Milliseconds() * speed );
		a = idMath::Sin( alpha );
		a = 0.35f + ( 0.65f * idMath::Fabs( a ) );
	}

    auto pIC = D3DDrv_GetImmediateContext();

    idImage* pImage = nullptr;
	if ( autoRenderIcon == AUTORENDER_HELLICON ) {
		pImage = globalImages->hellLoadingIconImage;
	} else {
		pImage = globalImages->loadingIconImage;
	}
    RB_BindImages( pIC, &pImage, 0, 1 );

    D3DDrv_SetRasterizerStateFromMask( pIC, CT_TWO_SIDED, 0 );
    D3DDrv_SetBlendStateFromMask( pIC, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
    D3DDrv_SetDepthStateFromMask( pIC, GLS_DEPTHFUNC_ALWAYS );

	// Set Parms
	float texS[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	float texT[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
	renderProgManager.SetRenderParm( RENDERPARM_TEXTUREMATRIX_S, texS );
	renderProgManager.SetRenderParm( RENDERPARM_TEXTUREMATRIX_T, texT );

	if ( autoRenderIcon == AUTORENDER_HELLICON ) {
        const float c[] = { 1, 1, 1, a };
        renderProgManager.SetRenderParm( RENDERPARM_COLOR, c );
	}

	// disable texgen
	float texGenEnabled[4] = { 0, 0, 0, 0 };
	renderProgManager.SetRenderParm( RENDERPARM_TEXGEN_0_ENABLED, texGenEnabled );

    pIC->VSSetShader( renderProgManager.GetBuiltInVertexShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR ), nullptr, 0 );
    pIC->PSSetShader( renderProgManager.GetBuiltInPixelShader( BUILTIN_SHADER_TEXTURE_VERTEXCOLOR ), nullptr, 0 );

	RB_DrawElementsWithCounters( pIC, &backEnd.unitSquareSurface );

	if ( autoRenderIcon == AUTORENDER_HELLICON ) {
        const float c[] = { 1, 1, 1, 1 };
        renderProgManager.SetRenderParm( RENDERPARM_COLOR, c );
	}
}
