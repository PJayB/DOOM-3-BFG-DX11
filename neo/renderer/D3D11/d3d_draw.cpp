// D3D headers
#include "d3d_common.h"
#include "d3d_driver.h"
#include "d3d_state.h"
#include "d3d_image.h"
#include "d3d_shaders.h"

void UpdateDirtyViewVS()
{
    // Upload the constants
    d3dViewVSConstantBuffer_t* cb = QD3D::MapDynamicBuffer<d3dViewVSConstantBuffer_t>( g_pImmediateContext, g_DrawState.viewRenderData.vsConstantBuffer );
    memcpy( cb, &g_RunState.vsConstants, sizeof(d3dViewVSConstantBuffer_t) );
    g_pImmediateContext->Unmap( g_DrawState.viewRenderData.vsConstantBuffer, 0 );
    g_RunState.vsDirtyConstants = qfalse;
}

void UpdateDirtyViewPS()
{
    // Upload the constants
    d3dViewPSConstantBuffer_t* cb = QD3D::MapDynamicBuffer<d3dViewPSConstantBuffer_t>( g_pImmediateContext, g_DrawState.viewRenderData.psConstantBuffer );
    memcpy( cb, &g_RunState.psConstants, sizeof(d3dViewPSConstantBuffer_t) );
    g_pImmediateContext->Unmap( g_DrawState.viewRenderData.psConstantBuffer, 0 );
    g_RunState.psDirtyConstants = qfalse;
}

// @pjb: forceinline because I don't want to put the 'if' inside UpdateDirtyTransform
__forceinline void UpdateViewState()
{
    // If we have dirty constants, update the constant buffer
    if ( g_RunState.vsDirtyConstants )
        UpdateDirtyViewVS();
}

__forceinline void UpdateMaterialState()
{
    if ( g_RunState.psDirtyConstants )
        UpdateDirtyViewPS();
}

static void SetTessVertexBuffer( const d3dTessBuffers_t* tess )
{
    UINT stride = sizeof(vec4_t);
    UINT offset = tess->xyz.currentOffset;
    g_pImmediateContext->IASetVertexBuffers( 0, 1, &tess->xyz.buffer, &stride, &offset );
}

static void SetTessVertexBuffersST( const d3dTessBuffers_t* tess, const d3dTessStageBuffers_t* stage )
{
    ID3D11Buffer* vbufs[2] = {
        stage->texCoords[0].buffer,
        stage->colors.buffer,
    };

    UINT strides[2] = {
        sizeof(vec2_t),
        sizeof(color4ub_t)
    };

    UINT offsets[2] = {
        stage->texCoords[0].currentOffset,
        stage->colors.currentOffset
    };

    g_pImmediateContext->IASetVertexBuffers( 1, 2, vbufs, strides, offsets );
}

static void SetTessVertexBuffersMT( const d3dTessBuffers_t* tess, const d3dTessStageBuffers_t* stage )
{
    ID3D11Buffer* vbufs[3] = {
        stage->texCoords[0].buffer,
        stage->texCoords[1].buffer,
        stage->colors.buffer,
    };

    UINT strides[3] = {
        sizeof(vec2_t),
        sizeof(vec2_t),
        sizeof(color4ub_t)
    };

    UINT offsets[3] = {
        stage->texCoords[0].currentOffset, 
        stage->texCoords[1].currentOffset, 
        stage->colors.currentOffset
    };

    g_pImmediateContext->IASetVertexBuffers( 1, 3, vbufs, strides, offsets );
}

static void UpdateTessBuffer( d3dCircularBuffer_t* circBuf, const void* cpuBuf, UINT size )
{
    if ( size == 0 )
        return;

    ID3D11Buffer* gpuBuf = circBuf->buffer;

	D3D11_MAPPED_SUBRESOURCE map;

    // If the buffer is full, discard the old contents.
    // Else, start from the current offset.
    if ( circBuf->nextOffset + size > circBuf->size )
    {
        g_pImmediateContext->Map( gpuBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        circBuf->currentOffset = 0;
        circBuf->nextOffset = size;
    } 
    else
    {
	    g_pImmediateContext->Map( gpuBuf, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &map);
        circBuf->currentOffset = circBuf->nextOffset;
        circBuf->nextOffset += size;
    }

    memcpy( (BYTE*) map.pData + circBuf->currentOffset, cpuBuf, size );
    g_pImmediateContext->Unmap( gpuBuf, 0 );
}

static void UpdateTessBuffers( const shaderCommands_t* input, bool needDlights, bool needFog )
{
    // Lock down the buffers
    UpdateTessBuffer( &g_DrawState.tessBufs.indexes, input->indexes, sizeof(glIndex_t) * input->numIndexes );
    UpdateTessBuffer( &g_DrawState.tessBufs.xyz, input->xyz, sizeof(vec4_t) * input->numVertexes );

	for ( int stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
        const shaderStage_t* xstage = input->xstages[stage];
		const stageVars_t *cpuStage = &input->svars[stage];

		if ( !xstage || !cpuStage ) {
			break;
		}

        d3dTessStageBuffers_t* gpuStage = &g_DrawState.tessBufs.stages[stage];
        UpdateTessBuffer( &gpuStage->colors, cpuStage->colors, sizeof(color4ub_t) * input->numVertexes );
        UpdateTessBuffer( &gpuStage->texCoords[0], cpuStage->texcoords[0], sizeof(vec2_t) * input->numVertexes );

        // Only update if we need these coords
        if ( xstage->bundle[1].image[0] != 0 )
		{
            UpdateTessBuffer( &gpuStage->texCoords[1], cpuStage->texcoords[1], sizeof(vec2_t) * input->numVertexes );
        }
    }

    if ( needDlights )
    {
        for ( int l = 0; l < input->dlightCount; ++l )
        {
            const dlightProjectionInfo_t* cpuLight = &input->dlightInfo[l];
            d3dTessLightProjBuffers_t* gpuLight = &g_DrawState.tessBufs.dlights[l];

		    if ( !cpuLight->numIndexes ) {
			    continue;
		    }

            UpdateTessBuffer( &gpuLight->indexes, cpuLight->hitIndexes, sizeof(glIndex_t) * cpuLight->numIndexes );
            UpdateTessBuffer( &gpuLight->colors, cpuLight->colorArray, sizeof(byte) * 4 * input->numVertexes );
            UpdateTessBuffer( &gpuLight->texCoords, cpuLight->texCoordsArray, sizeof(float) * 2 * input->numVertexes );
        }
    }

    if ( needFog )
    {
        UpdateTessBuffer( &g_DrawState.tessBufs.fog.colors, input->fogVars.colors, sizeof(color4ub_t) * input->numVertexes );
        UpdateTessBuffer( &g_DrawState.tessBufs.fog.texCoords, input->fogVars.texcoords, sizeof(vec2_t) * input->numVertexes );
    }
}

static const d3dImage_t* GetAnimatedImage( textureBundle_t *bundle, float shaderTime ) {
	int		index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return GetImageRenderInfo( tr.scratchImage[bundle->videoMapHandle] );
	}

	if ( bundle->numImageAnimations <= 1 ) {
		return GetImageRenderInfo( bundle->image[0] );
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = myftol( shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}
	index %= bundle->numImageAnimations;

    return GetImageRenderInfo( bundle->image[index] );
}







void DrawQuad( 
    const d3dQuadRenderData_t* qrd,
    const d3dImage_t* image,
    const float* coords,
    const float* texcoords,
    const float* color )
{
    UpdateViewState();
    UpdateMaterialState();

    //
    // Update the constant buffer
    //
    d3dQuadRenderConstantBuffer_t* cb = QD3D::MapDynamicBuffer<d3dQuadRenderConstantBuffer_t>( 
        g_pImmediateContext, 
        qrd->constantBuffer );

    if ( color ) {
        memcpy( cb->color, color, sizeof(float) * 3 );
        cb->color[3] = 1;
    } else {
        cb->color[0] = 1; cb->color[1] = 1; cb->color[2] = 1; cb->color[3] = 1; 
    }

    g_pImmediateContext->Unmap( qrd->constantBuffer, 0 );

    //
    // Update the vertex buffer
    //
    d3dQuadRenderVertex_t* vb = QD3D::MapDynamicBuffer<d3dQuadRenderVertex_t>( 
        g_pImmediateContext, 
        qrd->vertexBuffer );

    vb[0].texcoords[0] = texcoords[0]; vb[0].texcoords[1] = texcoords[1];
    vb[1].texcoords[0] = texcoords[2]; vb[1].texcoords[1] = texcoords[1];
    vb[2].texcoords[0] = texcoords[2]; vb[2].texcoords[1] = texcoords[3];
    vb[3].texcoords[0] = texcoords[0]; vb[3].texcoords[1] = texcoords[3];

    vb[0].coords[0] = coords[0]; vb[0].coords[1] = coords[1];
    vb[1].coords[0] = coords[2]; vb[1].coords[1] = coords[1];
    vb[2].coords[0] = coords[2]; vb[2].coords[1] = coords[3];
    vb[3].coords[0] = coords[0]; vb[3].coords[1] = coords[3];

    g_pImmediateContext->Unmap( qrd->vertexBuffer, 0 );

    //
    // Draw
    //
    UINT stride = sizeof(float) * 4;
    UINT offset = 0;
    ID3D11Buffer* psBuffers[] = {
        g_DrawState.viewRenderData.psConstantBuffer,
        g_DrawState.quadRenderData.constantBuffer
    };

    g_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    g_pImmediateContext->IASetVertexBuffers( 0, 1, &qrd->vertexBuffer, &stride, &offset );
    g_pImmediateContext->IASetInputLayout( qrd->inputLayout );
    g_pImmediateContext->IASetIndexBuffer( qrd->indexBuffer, DXGI_FORMAT_R16_UINT, 0 );
    g_pImmediateContext->VSSetShader( qrd->vertexShader, nullptr, 0 );
    g_pImmediateContext->VSSetConstantBuffers( 0, 1, &g_DrawState.viewRenderData.vsConstantBuffer );
    g_pImmediateContext->PSSetShader( qrd->pixelShader, nullptr, 0 );
    g_pImmediateContext->PSSetSamplers( 0, 1, &image->pSampler );
    g_pImmediateContext->PSSetShaderResources( 0, 1, &image->pSRV );
    g_pImmediateContext->PSSetConstantBuffers( 0, 2, psBuffers );
    g_pImmediateContext->DrawIndexed( 6, 0, 0 );
}

static void DrawSkyBox( 
    const d3dSkyBoxRenderData_t* sbrd,
    const skyboxDrawInfo_t* skybox, 
    const float* eye_origin, 
    const float* colorTint )
{
    D3DDrv_SetState(0);

    CommitRasterizerState( CT_TWO_SIDED, qfalse, qfalse );
    
    UpdateViewState();
    UpdateMaterialState();

    //
    // Update the VS constant buffer
    //
    d3dSkyBoxVSConstantBuffer_t* vscb = QD3D::MapDynamicBuffer<d3dSkyBoxVSConstantBuffer_t>( 
        g_pImmediateContext, 
        sbrd->vsConstantBuffer );
    memcpy( vscb->eyePos, eye_origin, sizeof(float) * 3 );
    g_pImmediateContext->Unmap( sbrd->vsConstantBuffer, 0 );

    //
    // Update the PS constant buffer
    //
    d3dSkyBoxPSConstantBuffer_t* pscb = QD3D::MapDynamicBuffer<d3dSkyBoxPSConstantBuffer_t>( 
        g_pImmediateContext, 
        sbrd->psConstantBuffer );

    if ( colorTint ) {
        memcpy( pscb->color, colorTint, sizeof(float) * 3 );
        pscb->color[3] = 1;
    } else {
        pscb->color[0] = 1; pscb->color[1] = 1; pscb->color[2] = 1; pscb->color[3] = 1; 
    }

    g_pImmediateContext->Unmap( sbrd->psConstantBuffer, 0 );

    //
    // Draw
    //
    UINT stride = sizeof(float) * 5;
    UINT offset = 0;
    ID3D11Buffer* vsBuffers[] = {
        g_DrawState.viewRenderData.vsConstantBuffer,
        g_DrawState.skyBoxRenderData.vsConstantBuffer
    };
    ID3D11Buffer* psBuffers[] = {
        g_DrawState.skyBoxRenderData.psConstantBuffer
    };

    g_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    g_pImmediateContext->IASetVertexBuffers( 0, 1, &sbrd->vertexBuffer, &stride, &offset );
    g_pImmediateContext->IASetInputLayout( sbrd->inputLayout );
    g_pImmediateContext->VSSetShader( sbrd->vertexShader, nullptr, 0 );
    g_pImmediateContext->VSSetConstantBuffers( 0, _countof( vsBuffers ), vsBuffers );
    g_pImmediateContext->PSSetShader( sbrd->pixelShader, nullptr, 0 );
    g_pImmediateContext->PSSetConstantBuffers( 0, _countof( psBuffers ), psBuffers );

    for ( int i = 0; i < 6; ++i )
    {
        const skyboxSideDrawInfo_t* side = &skybox->sides[i];
        
        if ( !side->image )
            continue;

        const d3dImage_t* image = GetImageRenderInfo( side->image );

        g_pImmediateContext->PSSetShaderResources( 0, 1, &image->pSRV );
        g_pImmediateContext->PSSetSamplers( 0, 1, &image->pSampler );
        g_pImmediateContext->Draw( 6, i * 6 );
    }
}

void D3DDrv_ShadowSilhouette( const float* edges, int edgeCount )
{

}

void D3DDrv_ShadowFinish( void )
{

}

void D3DDrv_DrawSkyBox( const skyboxDrawInfo_t* skybox, const float* eye_origin, const float* colorTint )
{
    DrawSkyBox( &g_DrawState.skyBoxRenderData, skybox, eye_origin, colorTint );
}

void D3DDrv_DrawBeam( const image_t* image, const float* color, const vec3_t startPoints[], const vec3_t endPoints[], int segs )
{
    // @pjb: after a grep of the BSP files there is no reference to RT_BEAM anywhere. Skipping.
}

static void TessDrawTextured( const shaderCommands_t* input, int stage )
{
    const d3dTessBuffers_t* buffers = &g_DrawState.tessBufs;
    const d3dGenericStageRenderData_t* resources = &g_DrawState.genericStage;
    shaderStage_t	*pStage = input->xstages[stage];

    const d3dImage_t* tex = nullptr;
	if ( pStage->bundle[0].vertexLightmap && ( (r_vertexLight->integer && !r_uiFullScreen->integer) || vdConfig.hardwareType == GLHW_PERMEDIA2 ) && r_lightmap->integer ) {
        tex = GetImageRenderInfo( tr.whiteImage );
    } else {
        tex = GetAnimatedImage( &pStage->bundle[0], input->shaderTime );
    }

    ASSERT( tex );

    g_pImmediateContext->IASetInputLayout( resources->inputLayoutST );
    g_pImmediateContext->VSSetShader( resources->vertexShaderST, nullptr, 0 );
    g_pImmediateContext->PSSetShader( resources->pixelShaderST, nullptr, 0 );
    g_pImmediateContext->PSSetSamplers( 0, 1, &tex->pSampler );
    g_pImmediateContext->PSSetConstantBuffers( 0, 1, &g_DrawState.viewRenderData.psConstantBuffer );
    g_pImmediateContext->PSSetShaderResources( 0, 1, &tex->pSRV );

    SetTessVertexBuffersST( buffers, &buffers->stages[stage] );

    g_pImmediateContext->DrawIndexed( input->numIndexes, 0, 0 );
}

static void TessDrawMultitextured( const shaderCommands_t* input, int stage )
{
    const d3dTessBuffers_t* buffers = &g_DrawState.tessBufs;
    const d3dGenericStageRenderData_t* resources = &g_DrawState.genericStage;

    shaderStage_t	*pStage = input->xstages[stage];

    const d3dImage_t* tex0 = GetAnimatedImage( &pStage->bundle[0], input->shaderTime );
    const d3dImage_t* tex1 = GetAnimatedImage( &pStage->bundle[1], input->shaderTime );

    ASSERT( tex0 );
    ASSERT( tex1 );

    ID3D11ShaderResourceView* psResources[2] = { tex0->pSRV, tex1->pSRV };
    ID3D11SamplerState* psSamplers[2] = { tex0->pSampler, tex1->pSampler };

    g_pImmediateContext->IASetInputLayout( resources->inputLayoutMT );
    g_pImmediateContext->VSSetShader( resources->vertexShaderMT, nullptr, 0 );
    g_pImmediateContext->PSSetShader( resources->pixelShaderMT, nullptr, 0 );
    g_pImmediateContext->PSSetSamplers( 0, 2, psSamplers );
    g_pImmediateContext->PSSetConstantBuffers( 0, 1, &g_DrawState.viewRenderData.psConstantBuffer );
    g_pImmediateContext->PSSetShaderResources( 0, 2, psResources );
    
    SetTessVertexBuffersMT( buffers, &buffers->stages[stage] );

    g_pImmediateContext->DrawIndexed( input->numIndexes, 0, 0 );
}

static void TessProjectDynamicLights( const shaderCommands_t *input )
{
    const d3dTessBuffers_t* buffers = &g_DrawState.tessBufs;
    const d3dImage_t* tex = GetImageRenderInfo( tr.dlightImage );

    const d3dGenericStageRenderData_t* resources = &g_DrawState.genericStage;
    g_pImmediateContext->IASetInputLayout( resources->inputLayoutST );
    g_pImmediateContext->VSSetShader( resources->vertexShaderST, nullptr, 0 );
    g_pImmediateContext->PSSetShader( resources->pixelShaderST, nullptr, 0 );
    g_pImmediateContext->PSSetSamplers( 0, 1, &tex->pSampler );
    g_pImmediateContext->PSSetConstantBuffers( 0, 1, &g_DrawState.viewRenderData.psConstantBuffer );
    g_pImmediateContext->PSSetShaderResources( 0, 1, &tex->pSRV );

    ID3D11Buffer* vbufs[2] = { 
        NULL,
        NULL
    };

    UINT strides[2] = {
        sizeof(vec2_t),
        sizeof(color4ub_t)
    };

    UINT offsets[2] = {
        0, 0
    };
    
    for ( int l = 0 ; l < input->dlightCount ; l++ )
    {
        const dlightProjectionInfo_t* dlInfo = &input->dlightInfo[l];

		if ( !dlInfo->numIndexes ) {
			continue;
		}

        const d3dTessLightProjBuffers_t* projBuf = &buffers->dlights[l];

        offsets[0] = projBuf->texCoords.currentOffset;
        offsets[1] = projBuf->colors.currentOffset;
        vbufs[0] = projBuf->texCoords.buffer;
        vbufs[1] = projBuf->colors.buffer;

        g_pImmediateContext->IASetIndexBuffer( 
            projBuf->indexes.buffer, 
            DXGI_FORMAT_R16_UINT, 
            projBuf->indexes.currentOffset );

        g_pImmediateContext->IASetVertexBuffers( 1, 2, vbufs, strides, offsets );

        // Select the blend mode
		if ( dlInfo->additive ) {
			D3DDrv_SetState( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
		else {
			D3DDrv_SetState( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}

        UpdateMaterialState();

        // Draw the dynamic light
        g_pImmediateContext->DrawIndexed( dlInfo->numIndexes, 0, 0 );
    }
}

static void TessDrawFog( const shaderCommands_t* input )
{
	if ( input->shader->fogPass == FP_EQUAL ) {
		D3DDrv_SetState( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		D3DDrv_SetState( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

    UpdateMaterialState();

    const d3dGenericStageRenderData_t* resources = &g_DrawState.genericStage;
    const d3dTessBuffers_t* buffers = &g_DrawState.tessBufs;
    const d3dImage_t* tex = GetImageRenderInfo( tr.fogImage );

    ID3D11Buffer* vbufs[2] = { 
        buffers->fog.texCoords.buffer,
        buffers->fog.colors.buffer
    };

    UINT strides[2] = {
        sizeof(vec2_t),
        sizeof(color4ub_t)
    };

    UINT offsets[2] = {
        buffers->fog.texCoords.currentOffset,
        buffers->fog.colors.currentOffset
    };
    
    g_pImmediateContext->IASetInputLayout( resources->inputLayoutST );
    g_pImmediateContext->IASetIndexBuffer( buffers->indexes.buffer, DXGI_FORMAT_R16_UINT, buffers->indexes.currentOffset );
    g_pImmediateContext->IASetVertexBuffers( 1, 2, vbufs, strides, offsets );
    g_pImmediateContext->VSSetShader( resources->vertexShaderST, nullptr, 0 );
    g_pImmediateContext->PSSetShader( resources->pixelShaderST, nullptr, 0 );
    g_pImmediateContext->PSSetSamplers( 0, 1, &tex->pSampler );
    g_pImmediateContext->PSSetConstantBuffers( 0, 1, &g_DrawState.viewRenderData.psConstantBuffer );
    g_pImmediateContext->PSSetShaderResources( 0, 1, &tex->pSRV );
    g_pImmediateContext->DrawIndexed( input->numIndexes, 0, 0 );
}

static void IterateStagesGeneric( const shaderCommands_t *input )
{
    for ( int stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = input->xstages[stage];

		if ( !pStage )
		{
			break;
		}

        D3DDrv_SetState( pStage->stateBits );

        UpdateMaterialState();

 		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != 0 )
		{
            TessDrawMultitextured( input, stage );
        }
        else
        {
            TessDrawTextured( input, stage );
        }

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) )
		{
			break;
		}
    }
}

void D3DDrv_DrawStageGeneric( const shaderCommands_t *input )
{
    UpdateViewState();

    // Determine what buffers we need to update for this draw call
    bool needDLights = input->dlightBits && input->shader->sort <= SS_OPAQUE
		&& !(input->shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) );
    bool needFog = input->fogNum && input->shader->fogPass;

    UpdateTessBuffers( input, needDLights, needFog );

    // todo: wireframe mode?
    CommitRasterizerState( input->shader->cullType, input->shader->polygonOffset, qfalse );

    const d3dCircularBuffer_t* indexes = &g_DrawState.tessBufs.indexes;
    
    g_pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    g_pImmediateContext->IASetIndexBuffer( indexes->buffer, DXGI_FORMAT_R16_UINT, indexes->currentOffset );
    g_pImmediateContext->VSSetConstantBuffers( 0, 1, &g_DrawState.viewRenderData.vsConstantBuffer );
    SetTessVertexBuffer( &g_DrawState.tessBufs );

    IterateStagesGeneric( input );

    // dynamic lighting
	if ( needDLights ) {
        TessProjectDynamicLights( input );
    }

    // fog
	if ( needFog ) {
		TessDrawFog( input );
	}    
}

void D3DDrv_DrawStageVertexLitTexture( const shaderCommands_t *input )
{
    // Optimizing this is a low priority
    D3DDrv_DrawStageGeneric( input );
}

void D3DDrv_DrawStageLightmappedMultitexture( const shaderCommands_t *input )
{
    // Optimizing this is a low priority
    D3DDrv_DrawStageGeneric( input );
}

void D3DDrv_DebugDrawAxis( void )
{

}

void D3DDrv_DebugDrawTris( const shaderCommands_t *input )
{

}

void D3DDrv_DebugDrawNormals( const shaderCommands_t *input )
{

}

void D3DDrv_DebugDrawPolygon( int color, int numPoints, const float* points )
{

}
