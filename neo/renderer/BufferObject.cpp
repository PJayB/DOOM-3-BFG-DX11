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

idCVar r_showBuffers( "r_showBuffers", "0", CVAR_INTEGER, "" );


//static const GLenum bufferUsage = GL_STATIC_DRAW_ARB;
static const GLenum bufferUsage = GL_DYNAMIC_DRAW_ARB;

/*
==================
IsWriteCombined
==================
*/
bool IsWriteCombined( void * base ) {
	MEMORY_BASIC_INFORMATION info;
	SIZE_T size = VirtualQueryEx( GetCurrentProcess(), base, &info, sizeof( info ) );
	if ( size == 0 ) {
		DWORD error = GetLastError();
		error = error;
		return false;
	}
	bool isWriteCombined = ( ( info.AllocationProtect & PAGE_WRITECOMBINE ) != 0 );
	return isWriteCombined;
}



/*
================================================================================================

	Buffer Objects

================================================================================================
*/

#ifdef ID_WIN_X86_SSE2_INTRIN

void CopyBuffer( byte * dst, const byte * src, int numBytes ) {
	assert_16_byte_aligned( dst );
	assert_16_byte_aligned( src );

	int i = 0;
	for ( ; i + 128 <= numBytes; i += 128 ) {
		__m128i d0 = _mm_load_si128( (__m128i *)&src[i + 0*16] );
		__m128i d1 = _mm_load_si128( (__m128i *)&src[i + 1*16] );
		__m128i d2 = _mm_load_si128( (__m128i *)&src[i + 2*16] );
		__m128i d3 = _mm_load_si128( (__m128i *)&src[i + 3*16] );
		__m128i d4 = _mm_load_si128( (__m128i *)&src[i + 4*16] );
		__m128i d5 = _mm_load_si128( (__m128i *)&src[i + 5*16] );
		__m128i d6 = _mm_load_si128( (__m128i *)&src[i + 6*16] );
		__m128i d7 = _mm_load_si128( (__m128i *)&src[i + 7*16] );
		_mm_stream_si128( (__m128i *)&dst[i + 0*16], d0 );
		_mm_stream_si128( (__m128i *)&dst[i + 1*16], d1 );
		_mm_stream_si128( (__m128i *)&dst[i + 2*16], d2 );
		_mm_stream_si128( (__m128i *)&dst[i + 3*16], d3 );
		_mm_stream_si128( (__m128i *)&dst[i + 4*16], d4 );
		_mm_stream_si128( (__m128i *)&dst[i + 5*16], d5 );
		_mm_stream_si128( (__m128i *)&dst[i + 6*16], d6 );
		_mm_stream_si128( (__m128i *)&dst[i + 7*16], d7 );
	}
	for ( ; i + 16 <= numBytes; i += 16 ) {
		__m128i d = _mm_load_si128( (__m128i *)&src[i] );
		_mm_stream_si128( (__m128i *)&dst[i], d );
	}
	for ( ; i + 4 <= numBytes; i += 4 ) {
		*(uint32 *)&dst[i] = *(const uint32 *)&src[i];
	}
	for ( ; i < numBytes; i++ ) {
		dst[i] = src[i];
	}
	_mm_sfence();
}

#else

void CopyBuffer( byte * dst, const byte * src, int numBytes ) {
	assert_16_byte_aligned( dst );
	assert_16_byte_aligned( src );
	memcpy( dst, src, numBytes );
}

#endif


ID3D11Buffer *CreateDirect3DBuffer( const void* data, UINT numBytes, UINT bindFlags )
{
	D3D11_BUFFER_DESC desc;
	ZeroMemory(&desc, sizeof(desc));

	desc.BindFlags = bindFlags;
	desc.ByteWidth = (UINT)numBytes;

    //if (data) {
	//    desc.Usage = D3D11_USAGE_IMMUTABLE;
    //    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    //} else {
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    //}

	D3D11_SUBRESOURCE_DATA srd;
	ZeroMemory(&srd, sizeof(srd));
	srd.pSysMem = data;

	ID3D11Buffer* buffer = NULL;
	D3DDrv_GetDevice()->CreateBuffer(&desc, data ? &srd : nullptr, &buffer);

    return buffer;
}

/*
================================================================================================

	idDoubleBuffer_t

================================================================================================
*/
idDoubleBuffer_t::idDoubleBuffer_t() 
    : m_mapBuffer( nullptr )
    , m_drawBuffer( nullptr )
{
}

idDoubleBuffer_t::idDoubleBuffer_t( const idDoubleBuffer_t& other ) 
    : m_mapBuffer( other.m_mapBuffer )
    , m_drawBuffer( other.m_drawBuffer )
{
    if ( m_mapBuffer ) { m_mapBuffer->AddRef(); }
    if ( m_drawBuffer ) { m_drawBuffer->AddRef(); }
}

idDoubleBuffer_t& idDoubleBuffer_t::operator = ( const idDoubleBuffer_t& other ) 
{
    m_mapBuffer = other.m_mapBuffer;
    m_drawBuffer = other.m_drawBuffer;
    if ( m_mapBuffer ) { m_mapBuffer->AddRef(); }
    if ( m_drawBuffer ) { m_drawBuffer->AddRef(); }
    return *this;
}

bool idDoubleBuffer_t::Init( const void* data, size_t size, uint bindFlags ) {
    m_mapBuffer = CreateDirect3DBuffer( data, size, bindFlags ); 
    m_drawBuffer = CreateDirect3DBuffer( data, size, bindFlags ); 
    return m_mapBuffer != nullptr && m_drawBuffer != nullptr;
}

void idDoubleBuffer_t::Destroy()
{
    SAFE_RELEASE( m_mapBuffer );
    SAFE_RELEASE( m_drawBuffer );
}

void idDoubleBuffer_t::SwapBuffers()
{
    ID3D11Buffer* ptr = m_drawBuffer;
    m_drawBuffer = m_mapBuffer;
    m_mapBuffer = ptr;
}

/*
================================================================================================

	idVertexBuffer

================================================================================================
*/

/*
========================
idVertexBuffer::idVertexBuffer
========================
*/
idVertexBuffer::idVertexBuffer() {
	size = 0;
	offsetInOtherBuffer = 0;
	SetUnmapped();
}

/*
========================
idVertexBuffer::~idVertexBuffer
========================
*/
idVertexBuffer::~idVertexBuffer() {
	FreeBufferObject();
}

/*
========================
idVertexBuffer::AllocBufferObject
========================
*/
bool idVertexBuffer::AllocBufferObject( const void * data, int allocSize ) {
	assert( !buffers.Allocated() );
	assert_16_byte_aligned( data );

	if ( allocSize <= 0 ) {
		idLib::Error( "idVertexBuffer::AllocBufferObject: allocSize = %i", allocSize );
	}

	size = allocSize;

	int numBytes = GetAllocedSize();

	if ( !buffers.Init( data, numBytes, D3D11_BIND_VERTEX_BUFFER ) ) {
		idLib::FatalError( "idVertexBuffer::AllocBufferObject: failed" );
	}

	return true;
}

/*
========================
idVertexBuffer::FreeBufferObject
========================
*/
void idVertexBuffer::FreeBufferObject() {
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "vertex buffer free %p, api %p (%i bytes)\n", this, &buffers, GetSize() );
	}

    buffers.Destroy();

	ClearWithoutFreeing();
}

/*
========================
idVertexBuffer::Reference
========================
*/
void idVertexBuffer::Reference( const idVertexBuffer & other ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up idDrawVerts
	assert( other.GetBuffer() != NULL );
	assert( other.GetSize() > 0 );

	FreeBufferObject();
	size = other.GetSize();						// this strips the MAPPED_FLAG
	offsetInOtherBuffer = other.GetOffset();
	buffers = other.buffers;
}

/*
========================
idVertexBuffer::Reference
========================
*/
void idVertexBuffer::Reference( const idVertexBuffer & other, int refOffset, int refSize ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up idDrawVerts
	assert( other.GetBuffer() != NULL );
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= other.GetSize() );

	FreeBufferObject();
	size = refSize;
	offsetInOtherBuffer = other.GetOffset() + refOffset;
	buffers = other.buffers;
}

/*
========================
idVertexBuffer::Update
========================
*/
/*
void idVertexBuffer::Update( const void * data, int updateSize ) const {
	assert( buffers.Allocated() );
	assert( IsMapped() == false );
	assert_16_byte_aligned( data );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( updateSize > size ) {
		idLib::FatalError( "idVertexBuffer::Update: size overrun, %i > %i\n", updateSize, GetSize() );
	}

	int numBytes = ( updateSize + 15 ) & ~15;

    assert( GetOffset() == 0 );
	D3DDrv_GetImmediateContext()->UpdateSubresource1( pBuffer, 0, nullptr, data, numBytes, 0, D3D11_COPY_DISCARD );
}
*/

/*
========================
idVertexBuffer::MapBuffer
========================
*/
void * idVertexBuffer::MapBuffer( bufferMapType_t mapType ) const {
	assert( buffers.Allocated() );
	assert( IsMapped() == false );
    assert( mapType != BM_READ );

    D3D11_MAPPED_SUBRESOURCE map;

    auto pIC = D3DDrv_GetImmediateContext();
    pIC->Map( buffers.GetCurrentMapBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map );

	SetMapped();

	if ( map.pData == NULL ) {
		idLib::FatalError( "idVertexBuffer::MapBuffer: failed" );
	}

    return static_cast<BYTE*>(map.pData) + GetOffset();
}

/*
========================
idVertexBuffer::UnmapBuffer
========================
*/
void idVertexBuffer::UnmapBuffer() const {
	assert( buffers.Allocated() );
	assert( IsMapped() );

    D3DDrv_GetImmediateContext()->Unmap( buffers.GetCurrentMapBuffer(), 0 );
	SetUnmapped();
}

/*
========================
idVertexBuffer::ClearWithoutFreeing
========================
*/
void idVertexBuffer::ClearWithoutFreeing() {
	size = 0;
	offsetInOtherBuffer = 0;
}

/*
================================================================================================

	idIndexBuffer

================================================================================================
*/

/*
========================
idIndexBuffer::idIndexBuffer
========================
*/
idIndexBuffer::idIndexBuffer() {
	size = 0;
	offsetInOtherBuffer = 0;
	SetUnmapped();
}

/*
========================
idIndexBuffer::~idIndexBuffer
========================
*/
idIndexBuffer::~idIndexBuffer() {
	FreeBufferObject();
}

/*
========================
idIndexBuffer::AllocBufferObject
========================
*/
bool idIndexBuffer::AllocBufferObject( const void * data, int allocSize ) {
	assert( !buffers.Allocated() );
	assert_16_byte_aligned( data );

	if ( allocSize <= 0 ) {
		idLib::Error( "idIndexBuffer::AllocBufferObject: allocSize = %i", allocSize );
	}

	size = allocSize;

	int numBytes = GetAllocedSize();

	if ( !buffers.Init( data, numBytes, D3D11_BIND_INDEX_BUFFER ) ) {
		idLib::FatalError( "idIndexBuffer::AllocBufferObject: failed" );
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "index buffer alloc %p, api %p (%i bytes)\n", this, GetBuffer(), GetSize() );
	}

	return true;
}

/*
========================
idIndexBuffer::FreeBufferObject
========================
*/
void idIndexBuffer::FreeBufferObject() {
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "index buffer free %p, api %p (%i bytes)\n", this, GetBuffer(), GetSize() );
	}

    buffers.Destroy();

	ClearWithoutFreeing();
}

/*
========================
idIndexBuffer::Reference
========================
*/
void idIndexBuffer::Reference( const idIndexBuffer & other ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up triIndex_t
	assert( other.GetBuffer() != NULL );
	assert( other.GetSize() > 0 );

	FreeBufferObject();
	size = other.GetSize();						// this strips the MAPPED_FLAG
	offsetInOtherBuffer = other.GetOffset();
	buffers = other.buffers;
}

/*
========================
idIndexBuffer::Reference
========================
*/
void idIndexBuffer::Reference( const idIndexBuffer & other, int refOffset, int refSize ) {
	assert( IsMapped() == false );
	//assert( other.IsMapped() == false );	// this happens when building idTriangles while at the same time setting up triIndex_t
	assert( other.GetBuffer() != NULL );
	assert( refOffset >= 0 );
	assert( refSize >= 0 );
	assert( refOffset + refSize <= other.GetSize() );

	FreeBufferObject();
	size = refSize;
	offsetInOtherBuffer = other.GetOffset() + refOffset;
	buffers = other.buffers;
}

/*
========================
idIndexBuffer::Update
========================
*/
/*
void idIndexBuffer::Update( const void * data, int updateSize ) const {

	assert( buffers.Allocated() );
	assert( IsMapped() == false );
	assert_16_byte_aligned( data );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( updateSize > size ) {
		idLib::FatalError( "idIndexBuffer::Update: size overrun, %i > %i\n", updateSize, GetSize() );
	}

	int numBytes = ( updateSize + 15 ) & ~15;

    assert( GetOffset() == 0 );
    D3DDrv_GetImmediateContext()->UpdateSubresource1( pBuffer, 0, NULL, data, numBytes, 0, D3D11_COPY_DISCARD );
}
*/

/*
========================
idIndexBuffer::MapBuffer
========================
*/
void * idIndexBuffer::MapBuffer( bufferMapType_t mapType ) const {

	assert( buffers.Allocated() );
	assert( IsMapped() == false );
    assert( mapType != BM_READ );

    D3D11_MAPPED_SUBRESOURCE map;

    auto pIC = D3DDrv_GetImmediateContext();
    pIC->Map( buffers.GetCurrentMapBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map );

	SetMapped();

	if ( map.pData == NULL ) {
		idLib::FatalError( "idIndexBuffer::MapBuffer: failed" );
	}

    return static_cast<BYTE*>(map.pData) + GetOffset();
}

/*
========================
idIndexBuffer::UnmapBuffer
========================
*/
void idIndexBuffer::UnmapBuffer() const {
	assert( buffers.Allocated() );
	assert( IsMapped() );

	D3DDrv_GetImmediateContext()->Unmap( buffers.GetCurrentMapBuffer(), 0 );
	SetUnmapped();
}

/*
========================
idIndexBuffer::ClearWithoutFreeing
========================
*/
void idIndexBuffer::ClearWithoutFreeing() {
	size = 0;
	offsetInOtherBuffer = 0;
}

/*
================================================================================================

	idJointBuffer

================================================================================================
*/

/*
========================
idJointBuffer::idJointBuffer
========================
*/
idJointBuffer::idJointBuffer() {
	numJoints = 0;
	offsetInOtherBuffer = 0;
	SetUnmapped();
}

/*
========================
idJointBuffer::~idJointBuffer
========================
*/
idJointBuffer::~idJointBuffer() {
	FreeBufferObject();
}

/*
========================
idJointBuffer::AllocBufferObject
========================
*/
bool idJointBuffer::AllocBufferObject( const float * joints, int numAllocJoints ) {
	assert( !buffers.Allocated() );
	assert_16_byte_aligned( joints );

	if ( numAllocJoints <= 0 ) {
		idLib::Error( "idJointBuffer::AllocBufferObject: joints = %i", numAllocJoints );
	}

	numJoints = numAllocJoints;

	const int numBytes = GetAllocedSize();

	if ( !buffers.Init( joints, numBytes, D3D11_BIND_CONSTANT_BUFFER ) ) {
		idLib::FatalError( "idJointBuffer::AllocBufferObject: failed" );
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "joint buffer alloc %p, api %p (%i joints)\n", this, GetBuffer(), GetNumJoints() );
	}

	return true;
}

/*
========================
idJointBuffer::FreeBufferObject
========================
*/
void idJointBuffer::FreeBufferObject() {
	if ( IsMapped() ) {
		UnmapBuffer();
	}

	if ( r_showBuffers.GetBool() ) {
		idLib::Printf( "joint buffer free %p, api %p (%i joints)\n", this, GetBuffer(), GetNumJoints() );
	}

	buffers.Destroy();

	ClearWithoutFreeing();
}

/*
========================
idJointBuffer::Reference
========================
*/
void idJointBuffer::Reference( const idJointBuffer & other ) {
	assert( IsMapped() == false );
	assert( other.IsMapped() == false );
	assert( other.GetBuffer() != NULL );
	assert( other.GetNumJoints() > 0 );

	FreeBufferObject();
	numJoints = other.GetNumJoints();			// this strips the MAPPED_FLAG
	offsetInOtherBuffer = other.GetOffset();
	buffers = other.buffers;
}

/*
========================
idJointBuffer::Reference
========================
*/
void idJointBuffer::Reference( const idJointBuffer & other, int jointRefOffset, int numRefJoints ) {
	assert( IsMapped() == false );
	assert( other.IsMapped() == false );
	assert( other.GetBuffer() != NULL );
	assert( jointRefOffset >= 0 );
	assert( numRefJoints >= 0 );
	assert( jointRefOffset + numRefJoints * sizeof( idJointMat ) <= other.GetNumJoints() * sizeof( idJointMat ) );
	assert_16_byte_aligned( numRefJoints * 3 * 4 * sizeof( float ) );

	FreeBufferObject();
	numJoints = numRefJoints;
	offsetInOtherBuffer = other.GetOffset() + jointRefOffset;
	buffers = other.buffers;
}

/*
========================
idJointBuffer::Update
========================
*/
/*
void idJointBuffer::Update( const float * joints, int numUpdateJoints ) const {
	assert( buffers.Allocated() );
	assert( IsMapped() == false );
	assert_16_byte_aligned( joints );
	assert( ( GetOffset() & 15 ) == 0 );

	if ( numUpdateJoints > numJoints ) {
		idLib::FatalError( "idJointBuffer::Update: size overrun, %i > %i\n", numUpdateJoints, numJoints );
	}

	const int numBytes = numUpdateJoints * 3 * 4 * sizeof( float );
    
    assert( GetOffset() == 0 );
    D3DDrv_GetImmediateContext()->UpdateSubresource1( pBuffer, 0, NULL, joints, numBytes, 0, D3D11_COPY_DISCARD );
}
*/

/*
========================
idJointBuffer::MapBuffer
========================
*/
float * idJointBuffer::MapBuffer( bufferMapType_t mapType ) const {
	assert( IsMapped() == false );
	assert( mapType == BM_WRITE );
    assert( mapType != BM_READ );

    D3D11_MAPPED_SUBRESOURCE map;

    auto pIC = D3DDrv_GetImmediateContext();
    pIC->Map( buffers.GetCurrentMapBuffer(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map );

	SetMapped();

	if ( map.pData == NULL ) {
		idLib::FatalError( "idJointBuffer::MapBuffer: failed" );
	}

    return reinterpret_cast<float*>(
        static_cast<BYTE*>(map.pData) + GetOffset() );
}

/*
========================
idJointBuffer::UnmapBuffer
========================
*/
void idJointBuffer::UnmapBuffer() const {
	assert( buffers.Allocated() );
	assert( IsMapped() );

	D3DDrv_GetImmediateContext()->Unmap( buffers.GetCurrentMapBuffer(), 0 );
	SetUnmapped();
}

/*
========================
idJointBuffer::ClearWithoutFreeing
========================
*/
void idJointBuffer::ClearWithoutFreeing() {
	numJoints = 0;
	offsetInOtherBuffer = 0;
}

/*
========================
idJointBuffer::Swap
========================
*/
void idJointBuffer::Swap( idJointBuffer & other ) {
	SwapValues( other.numJoints, numJoints );
	SwapValues( other.offsetInOtherBuffer, offsetInOtherBuffer );
	SwapValues( other.buffers, buffers );
}
