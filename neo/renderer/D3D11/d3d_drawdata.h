#pragma once

struct d3dImage_t;

void InitQuadRenderData( d3dQuadRenderData_t* qrd );
void DestroyQuadRenderData( d3dQuadRenderData_t* qrd );

void InitSkyBoxRenderData( d3dSkyBoxRenderData_t* rd );
void DestroySkyBoxRenderData( d3dSkyBoxRenderData_t* rd );

void InitGenericStageRenderData( d3dGenericStageRenderData_t* rd );
void DestroyGenericStageRenderData( d3dGenericStageRenderData_t* rd );

void InitViewRenderData( d3dViewRenderData_t* vrd );
void DestroyViewRenderData( d3dViewRenderData_t* vrd );

void InitRasterStates( d3dRasterStates_t* rs );
void DestroyRasterStates( d3dRasterStates_t* rs );

void InitDepthStates( d3dDepthStates_t* ds );
void DestroyDepthStates( d3dDepthStates_t* ds );

void InitBlendStates( d3dBlendStates_t* bs );
void DestroyBlendStates( d3dBlendStates_t* bs );

void InitTessBuffers( d3dTessBuffers_t* tess );
void DestroyTessBuffers( d3dTessBuffers_t* tess );
