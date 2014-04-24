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
=========================================================================================================

BACKEND COMMANDS

=========================================================================================================
*/

/*
==================
RB_DrawViewInternal
==================
*/
void RB_DrawViewInternal( const viewDef_t * viewDef, const int stereoEye ) {
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
	D3DDrv_SetViewport( viewDef->viewport.x1,
		viewDef->viewport.y1,
		viewDef->viewport.x2 + 1 - viewDef->viewport.x1,
		viewDef->viewport.y2 + 1 - viewDef->viewport.y1 );

	// the scissor may be smaller than the viewport for subviews
	D3DDrv_SetScissor( backEnd.viewDef->viewport.x1 + viewDef->scissor.x1,
				backEnd.viewDef->viewport.y1 + viewDef->scissor.y1,
				viewDef->scissor.x2 + 1 - viewDef->scissor.x1,
				viewDef->scissor.y2 + 1 - viewDef->scissor.y1 );
	backEnd.currentScissor = viewDef->scissor;

	backEnd.glState.faceCulling = -1;		// force face culling to set next time

	// ensures that depth writes are enabled for the depth clear
    D3DDrv_SetDefaultState();

	// Clear the depth buffer and clear the stencil to 128 for stencil shadows as well as gui masking
	D3DDrv_Clear( CLEAR_DEPTH | CLEAR_STENCIL, nullptr, STENCIL_SHADOW_TEST_VALUE, 1 );
    
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

    //
    // @pjb: todo: Draw things
    //




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

StereoEye will always be 0 in mono modes, or -1 / 1 in stereo modes.
If the view is a GUI view that is repeated for both eyes, the viewDef.stereoEye value
is 0, so the stereoEye parameter is not always the same as that.
==================
*/
void RB_DrawView( const void *data, const int stereoEye ) {
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
	RB_DrawViewInternal( cmd->viewDef, stereoEye );

	RB_MotionBlur();

	// restore the context for 2D drawing if we were stubbing it out
	if ( r_skipRenderContext.GetBool() && backEnd.viewDef->viewEntitys ) {
		// @pjb: todo: Reset all state
	}
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
		D3DDrv_Clear( CLEAR_COLOR, nullptr, STENCIL_SHADOW_TEST_VALUE, 0 );
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
			RB_DrawView( cmds, 0 );
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

	D3DDrv_Flush();

	// stop rendering on this thread
	uint64 backEndFinishTime = Sys_Microseconds();
	backEnd.pc.totalMicroSec = backEndFinishTime - backEndStartTime;

	if ( r_debugRenderToTexture.GetInteger() == 1 ) {
		common->Printf( "3d: %i, 2d: %i, SetBuf: %i, CpyRenders: %i, CpyFrameBuf: %i\n", c_draw3d, c_draw2d, c_setBuffers, c_copyRenders, backEnd.pc.c_copyFrameBuffer );
		backEnd.pc.c_copyFrameBuffer = 0;
	}
	renderLog.EndFrame();
}
