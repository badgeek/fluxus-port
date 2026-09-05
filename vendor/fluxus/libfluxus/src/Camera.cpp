// Copyright (C) 2005 Dave Griffiths
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "Renderer.h"
#include "Camera.h"

//#define DEBUG_CAMERA

#include "RenderBackend.h"   // fluxus->JUCE port: projection + camera via the seam
using namespace Fluxus;

Camera::Camera() :
m_Initialised(false),
m_Ortho(false),
m_CustomProjection(false),
m_CameraAttached(0),
m_CameraLag(0),
m_Left(-1),
m_Right(1),
m_Bottom(-0.75),
m_Top(0.75),
m_Front(1),
m_Back(10000),
m_OrthZoom(1.0f),
m_ViewX(0),
m_ViewY(0),
m_ViewWidth(1),
m_ViewHeight(1),
m_FirstAttach(true)
{
	// default camera position
	m_Transform.translate(0,0,-10);
}

Camera::~Camera()
{
}

// fluxus->JUCE port: the projection is BUILT here now instead of being asked of
// GL. glFrustum/glOrtho do not exist on GLES, and they were the only reason this
// code needed a second matrix stack. These are the matrices those two calls
// produce, in the same column-major layout GL reads (which is what dMatrix::arr()
// already hands it), so the result on desktop is identical.
static void frustumMatrix(float *m, float l, float r, float b, float t, float n, float f)
{
	for (int i=0; i<16; i++) m[i]=0;
	m[0]  = 2.0f*n/(r-l);
	m[5]  = 2.0f*n/(t-b);
	m[8]  = (r+l)/(r-l);
	m[9]  = (t+b)/(t-b);
	m[10] = -(f+n)/(f-n);
	m[11] = -1.0f;
	m[14] = -2.0f*f*n/(f-n);
}

static void orthoMatrix(float *m, float l, float r, float b, float t, float n, float f)
{
	for (int i=0; i<16; i++) m[i]=0;
	m[0]  =  2.0f/(r-l);
	m[5]  =  2.0f/(t-b);
	m[10] = -2.0f/(f-n);
	m[12] = -(r+l)/(r-l);
	m[13] = -(t+b)/(t-b);
	m[14] = -(f+n)/(f-n);
	m[15] =  1.0f;
}

void Camera::DoProjection()
{
	if (m_CustomProjection)
	{
		Backend()->setProjectionMatrix(m_CustomProjectionMatrix.arr());
	}
	else
	{
		float m[16];
		if (m_Ortho)
			orthoMatrix(m, m_Left*m_OrthZoom, m_Right*m_OrthZoom,
			               m_Bottom*m_OrthZoom, m_Top*m_OrthZoom, m_Front, m_Back);
		else
			frustumMatrix(m, m_Left, m_Right, m_Bottom, m_Top, m_Front, m_Back);
		// multiply, not load: glFrustum/glOrtho multiplied, and pick mode depends on it
		Backend()->multProjectionMatrix(m);
	}
	#ifdef DEBUG_CAMERA
	cerr<<"camera:"<<this<<" frustum:"<<m_Left<<" "<<m_Right<<" "<<m_Bottom<<" "<<m_Top<<" "<<m_Front<<" "<<m_Back<<endl;
	#endif
}

void Camera::DoCamera(Renderer * renderer)
{
	Backend()->multMatrix(m_Transform.arr());

	#ifdef DEBUG_CAMERA
	cerr<<"camera:"<<this<<" transform:"<<m_Transform<<endl;
	#endif

	if (m_CameraAttached)
	{
        dMatrix worldmat = renderer->GetGlobalTransform(m_CameraAttached).inverse();
		if (!m_FirstAttach && m_CameraLag!=0)
		{
			//m_LockedMatrix.RigidBlend(worldmat,m_CameraLag);
			m_LockedMatrix.blend(worldmat,m_CameraLag);
		}
		else
		{
			m_LockedMatrix=worldmat;
		}
		m_FirstAttach=false;
		Backend()->multMatrix(m_LockedMatrix.arr());
	}
}

void Camera::LockCamera(int p)
{
     m_CameraAttached=p;
	 m_FirstAttach=true;
}

dMatrix Camera::GetProjection()
{
	dMatrix Projection;
	Backend()->getProjection(Projection.arr());
	return Projection;
}

void Camera::SetProjection(const dMatrix &m)
{
	m_CustomProjectionMatrix = m;
	m_CustomProjection = true;
	m_Initialised = false;
}

bool Camera::NeedsInit()
{
	if (m_Initialised)
	{
		return false;
	}
	else
	{
		m_Initialised=true;
		return true;
	}
}

