#include "d3d_common.h"
#include "d3d_state.h"
#include "d3d_image.h"
#include <float.h>

static d3dImage_t s_d3dImages[MAX_DRAWIMAGES];

void CreateImageCustom( 
    const image_t* image, 
    const int width, 
    const int height, 
    const int mipLevels,
    const byte *pic, 
    qboolean isLightmap )
{
    d3dImage_t* d3dImage = &s_d3dImages[image->index];

    Com_Memset( d3dImage, 0, sizeof( d3dImage_t ) );

    // Choose the internal format
    // @pjb: because of the way q3 loads textures, they're all RGBA.
    d3dImage->internalFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // If it's a scratch image, make it dynamic
    if ( Q_strncmp( image->imgName, "*scratch", sizeof(image->imgName) ) == 0 )
        d3dImage->dynamic = qtrue;

    // Re-allocate space for the image and light scale it
    // @pjb: I wish I didn't have to do this, but there we are.
    size_t imageSizeBytes = width * height * sizeof( UINT );
    void* lightscaledCopy = ri.Hunk_AllocateTempMemory( (int) imageSizeBytes );
    memcpy( lightscaledCopy, pic, imageSizeBytes );
    R_LightScaleTexture( (unsigned int*) lightscaledCopy, width, height, (qboolean)(mipLevels == 1) );

    // Now generate the mip levels
    int mipWidth = width;
    int mipHeight = height;
    D3D11_SUBRESOURCE_DATA* subres = (D3D11_SUBRESOURCE_DATA*) ri.Hunk_AllocateTempMemory( (int) sizeof( D3D11_SUBRESOURCE_DATA ) * mipLevels );
    for ( int mip = 0; mip < mipLevels; ++mip )
    {
        if ( mip != 0 ) {
            // We downsample the lightscaledCopy each time
            R_MipMap( (byte*) lightscaledCopy, mipWidth, mipHeight );
            mipWidth = max( 1, mipWidth >> 1 );
            mipHeight = max( 1, mipHeight >> 1 );
        }

        int mipSize = mipWidth * mipHeight * sizeof(UINT);

        // Set up the resource sizes and memory
    	subres[mip].pSysMem = ri.Hunk_AllocateTempMemory( mipSize );
        subres[mip].SysMemPitch = mipWidth * sizeof( UINT );
        subres[mip].SysMemSlicePitch = 0;

        // Copy the mip data over
        memcpy( (void*) subres[mip].pSysMem, lightscaledCopy, mipSize );
    }

    // Create the texture
	D3D11_TEXTURE2D_DESC dsd;
	ZeroMemory(&dsd, sizeof(dsd));
	dsd.Width = width;
	dsd.Height = height;
	dsd.MipLevels = mipLevels;
	dsd.ArraySize = 1;
	dsd.Format = d3dImage->internalFormat;
	dsd.SampleDesc.Count = 1;
	dsd.SampleDesc.Quality = 0;
	dsd.BindFlags = D3D11_BIND_SHADER_RESOURCE;		

    // For cinematics we'll need to upload new texture data to this texture
    if ( d3dImage->dynamic ) {
        dsd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
   	    dsd.Usage = D3D11_USAGE_DYNAMIC;
    } else {
        dsd.Usage = D3D11_USAGE_IMMUTABLE;
    }

	g_pDevice->CreateTexture2D( &dsd, subres, &d3dImage->pTexture );

    for ( int mip = mipLevels - 1; mip >= 0; --mip )
        ri.Hunk_FreeTempMemory( (void*) subres[mip].pSysMem );
    
    ri.Hunk_FreeTempMemory( subres );
    ri.Hunk_FreeTempMemory( lightscaledCopy );

    // Create a shader resource view
    d3dImage->pSRV = QD3D::CreateTexture2DShaderResourceView( 
        g_pDevice,
        d3dImage->pTexture,
        d3dImage->internalFormat );

    // Choose the wrap/clamp mode
    D3D11_SAMPLER_DESC samplerDesc;
    Com_Memset( &samplerDesc, 0, sizeof( samplerDesc ) );

#ifdef _ARM_
    samplerDesc.MaxLOD = FLT_MAX;
#else
    samplerDesc.MaxLOD = mipLevels - 1;
#endif

    switch ( image->wrapClampMode )
    {
    case WRAPMODE_CLAMP:
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        break;
    case WRAPMODE_REPEAT:
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        break;
    }
    
    if ( image->mipmap )
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    else
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;

    
    // @pjb: todo: get a sampler rather than creating a new one each time
    g_pDevice->CreateSamplerState(
        &samplerDesc, 
        &d3dImage->pSampler );

    d3dImage->width = width;
    d3dImage->height = height;
}

D3D_PUBLIC void D3DDrv_CreateImage( const image_t* image, const byte *pic, qboolean isLightmap )
{
    // Count the number of miplevels for this image
    int mipLevels = 1;
    if ( image->mipmap )
    {
        int mipWidth = image->width;
        int mipHeight = image->height;
        mipLevels = 0;
        while ( mipWidth > 0 || mipHeight > 0 )
        {
            mipWidth >>= 1;
            mipHeight >>= 1;
            mipLevels++;
        }

        // Don't allow zero mips
        mipLevels = max( 1, mipLevels );
    }

    CreateImageCustom( 
        image,
        image->width,
        image->height,
        mipLevels,
        pic,
        isLightmap );
}

D3D_PUBLIC void D3DDrv_DeleteImage( const image_t* image )
{
    d3dImage_t* d3dImage = &s_d3dImages[image->index];

    SAFE_RELEASE( d3dImage->pTexture );
    SAFE_RELEASE( d3dImage->pSRV );
    SAFE_RELEASE( d3dImage->pSampler );

    Com_Memset( d3dImage, 0, sizeof( d3dImage_t ) );
}

D3D_PUBLIC void D3DDrv_UpdateCinematic( const image_t* image, const byte* pic, int cols, int rows, qboolean dirty )
{
    if ( cols != image->width || rows != image->height ) {
        D3DDrv_DeleteImage( image );

        // Regenerate the texture
        CreateImageCustom( 
            image,
            cols,
            rows,
            1,
            pic, 
            qfalse );
    } else {
        if (dirty) {

            d3dImage_t* d3dImage = &s_d3dImages[image->index];

            // Check it's dynamic
            if ( !d3dImage->dynamic )
                ri.Error( ERR_FATAL, "Trying to upload cinematic texture to a non-dynamic image.\n" );

            // Update the texture
            D3D11_MAPPED_SUBRESOURCE map;
            V( g_pImmediateContext->Map( d3dImage->pTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map ) );
            memcpy( map.pData, pic, cols * rows * sizeof(UINT) );
            g_pImmediateContext->Unmap( d3dImage->pTexture, 0 );

            // @pjb: todo: generate mips?
        }
    }
}

D3D_PUBLIC imageFormat_t D3DDrv_GetImageFormat( const image_t* image )
{
    // @pjb: hack: all images are RGBA8 for now
    return IMAGEFORMAT_RGBA;
}

D3D_PUBLIC int D3DDrv_SumOfUsedImages( void )
{
	int	total;
	int i;

	total = 0;
	for ( i = 0; i < tr.numImages; i++ ) {
        const d3dImage_t* d3dImage = &s_d3dImages[tr.images[i]->index];
		if ( d3dImage->frameUsed == tr.frameCount ) {
			total += d3dImage->width * d3dImage->height;
		}
	}

	return total;
}

const d3dImage_t* GetImageRenderInfo( const image_t* image )
{
    const d3dImage_t* d3dImage = &s_d3dImages[image->index];

    if ( !d3dImage->pTexture ) {
        ri.Error( ERR_FATAL, "Illegal image index.\n" );
    }

    return d3dImage;
}


void InitImages()
{
    Com_Memset( s_d3dImages, 0, sizeof( s_d3dImages ) );
}

void DestroyImages()
{

}
