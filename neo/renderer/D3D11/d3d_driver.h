#ifndef __D3D_DRIVER_H__
#define __D3D_DRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

void D3DDrv_DriverInit( void );

//----------------------------------------------------------------------------
// Driver entry points
//----------------------------------------------------------------------------

void D3DDrv_Shutdown( void );
void D3DDrv_UnbindResources( void );
size_t D3DDrv_LastError( void );
void D3DDrv_ReadPixels( int x, int y, int width, int height, imageFormat_t requestedFmt, void* dest );
void D3DDrv_ReadDepth( int x, int y, int width, int height, float* dest );
void D3DDrv_ReadStencil( int x, int y, int width, int height, byte* dest );
void D3DDrv_CreateImage( const image_t* image, const byte *pic, qboolean isLightmap );
void D3DDrv_DeleteImage( const image_t* image );
void D3DDrv_UpdateCinematic( const image_t* image, const byte* pic, int cols, int rows, qboolean dirty );
void D3DDrv_DrawImage( const image_t* image, const float* coords, const float* texcoords, const float* color );
imageFormat_t D3DDrv_GetImageFormat( const image_t* image );
void D3DDrv_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] );
int D3DDrv_SumOfUsedImages( void );
void D3DDrv_GfxInfo( void );
void D3DDrv_Clear( unsigned long bits, const float* clearCol, unsigned long stencil, float depth );
void D3DDrv_SetProjection( const float* projMatrix );
void D3DDrv_GetProjection( float* projMatrix );
void D3DDrv_SetModelView( const float* modelViewMatrix );
void D3DDrv_GetModelView( float* modelViewMatrix );
void D3DDrv_SetViewport( int left, int top, int width, int height );
void D3DDrv_Flush( void );
void D3DDrv_SetState( unsigned long stateMask );
void D3DDrv_ResetState2D( void );
void D3DDrv_ResetState3D( void );
void D3DDrv_SetPortalRendering( qboolean enabled, const float* flipMatrix, const float* plane );
void D3DDrv_SetDepthRange( float minRange, float maxRange );
void D3DDrv_SetDrawBuffer( int buffer );
void D3DDrv_EndFrame( void );
void D3DDrv_MakeCurrent( qboolean current );
void D3DDrv_ShadowSilhouette( const float* edges, int edgeCount );
void D3DDrv_ShadowFinish( void );
void D3DDrv_DrawSkyBox( const skyboxDrawInfo_t* skybox, const float* eye_origin, const float* colorTint );
void D3DDrv_DrawBeam( const image_t* image, const float* color, const vec3_t startPoints[], const vec3_t endPoints[], int segs );
void D3DDrv_DrawStageGeneric( const shaderCommands_t *input );
void D3DDrv_DrawStageVertexLitTexture( const shaderCommands_t *input );
void D3DDrv_DrawStageLightmappedMultitexture( const shaderCommands_t *input );
void D3DDrv_DebugDrawAxis( void );
void D3DDrv_DebugDrawTris( const shaderCommands_t *input );
void D3DDrv_DebugDrawNormals( const shaderCommands_t *input );
void D3DDrv_DebugSetOverdrawMeasureEnabled( qboolean enabled );
void D3DDrv_DebugSetTextureMode( const char* mode );
void D3DDrv_DebugDrawPolygon( int color, int numPoints, const float* points );

#ifdef __cplusplus
} // extern "C"
#endif

#endif
