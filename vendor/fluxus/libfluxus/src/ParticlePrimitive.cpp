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
#include "ParticlePrimitive.h"
#include "RenderBackend.h"   // fluxus->JUCE port: draws go through the seam now
#include "State.h"

using namespace Fluxus;

ParticlePrimitive::ParticlePrimitive()
{
	AddData("p",new TypedPData<dVector>);
	AddData("c",new TypedPData<dColour>);
	AddData("s",new TypedPData<dVector>);
	AddData("r",new TypedPData<float>);

	// direct access for speed
	PDataDirty();
}

ParticlePrimitive::ParticlePrimitive(const ParticlePrimitive &other) :
Primitive(other)
{
	PDataDirty();
}

ParticlePrimitive::~ParticlePrimitive()
{
}

ParticlePrimitive* ParticlePrimitive::Clone() const
{
	return new ParticlePrimitive(*this);
}

void ParticlePrimitive::PDataDirty()
{
	m_VertData=GetDataVec<dVector>("p");
	m_ColData=GetDataVec<dColour>("c");
	m_SizeData=GetDataVec<dVector>("s");
	m_RotateData=GetDataVec<float>("r");
}
	
void ParticlePrimitive::Render()
{
	Backend()->setLighting(false);

	// fluxus->JUCE port: points go through the seam. The backend owns client-array
	// enable/disable now (it disables what a draw does not supply), so the manual
	// juggling around this draw is gone.
	if (m_State.Hints & HINT_POINTS)
	{
		if (m_State.Hints & HINT_AALIAS) glEnable(GL_POINT_SMOOTH);
		else glDisable(GL_POINT_SMOOTH);

		RVertexArrays va;
		va.pos = m_VertData->begin()->arr();
		va.col = m_ColData->begin()->arr();
		va.stride = sizeof(dVector);
		Backend()->drawArrays(RPrim::Points, va, (int)m_VertData->size(), 0, 0);
	}

	if (m_State.Hints & HINT_SOLID)
	{
		dVector cameradir=GetLocalCameraDir();
		dVector across=GetLocalCameraUp().cross(cameradir);
		across.normalise();
		dVector down=across.cross(cameradir);
		down.normalise();
		
		// fluxus->JUCE port: the sorted and unsorted branches differed ONLY in the
		// order particles are visited, so only that order is chosen here and the
		// quad building below is shared. One backend draw replaces both
		// glBegin/glVertex loops — same four corners, same winding, same texcoords.
		const unsigned int count = m_VertData->size();
		m_DrawOrder.resize(count);
		if (m_State.Hints & HINT_DEPTH_SORT)
		{
			dMatrix ModelView2;
			Backend()->getModelView(ModelView2.arr());

			list<SortItem> sorted;
			for (unsigned int n=0; n<count; n++)
			{
				dVector t=ModelView2.transform((*m_VertData)[n]);
				sorted.push_back(SortItem(n, t.z));
			}
			sorted.sort();

			unsigned int i=0;
			for (list<SortItem>::iterator s=sorted.begin(); s!=sorted.end(); ++s)
				m_DrawOrder[i++]=s->Index;
		}
		else
		{
			for (unsigned int n=0; n<count; n++) m_DrawOrder[n]=n;
		}

		m_DrawPos.resize(count*4);
		m_DrawTex.resize(count*4);
		m_DrawCol.resize(count*4);
		for (unsigned int n=0; n<count; n++)
		{
			const unsigned int p = m_DrawOrder[n];
			dVector scaledacross(across*(*m_SizeData)[p].x*0.5);
			dVector scaledown(down*(*m_SizeData)[p].y*0.5);
			const unsigned int i=n*4;
			m_DrawPos[i]   = (*m_VertData)[p]-scaledacross-scaledown;
			m_DrawPos[i+1] = (*m_VertData)[p]-scaledacross+scaledown;
			m_DrawPos[i+2] = (*m_VertData)[p]+scaledacross+scaledown;
			m_DrawPos[i+3] = (*m_VertData)[p]+scaledacross-scaledown;
			m_DrawTex[i]   = dVector(0,0,0);
			m_DrawTex[i+1] = dVector(0,1,0);
			m_DrawTex[i+2] = dVector(1,1,0);
			m_DrawTex[i+3] = dVector(1,0,0);
			for (int k=0; k<4; k++) m_DrawCol[i+k]=(*m_ColData)[p];
		}

		RVertexArrays va;
		va.pos = m_DrawPos[0].arr();
		va.tex = m_DrawTex[0].arr();
		va.col = m_DrawCol[0].arr();
		va.stride = sizeof(dVector);
		Backend()->drawArrays(RPrim::Quads, va, (int)count*4, 0, 0);
	}
	Backend()->setLighting(true);
}

dBoundingBox ParticlePrimitive::GetBoundingBox(const dMatrix &space)
{
	dBoundingBox box;
	for (vector<dVector,FLX_ALLOC(dVector) >::iterator i=m_VertData->begin(); i!=m_VertData->end(); ++i)
	{
		box.expand(space.transform(*i));
	}
	return box;
}

void ParticlePrimitive::ApplyTransform(bool ScaleRotOnly)
{
	if (!ScaleRotOnly)
	{
		for (vector<dVector,FLX_ALLOC(dVector) >::iterator i=m_VertData->begin(); i!=m_VertData->end(); ++i)
		{
			*i=GetState()->Transform.transform(*i);
		}
	}
	else
	{
		for (vector<dVector,FLX_ALLOC(dVector) >::iterator i=m_VertData->begin(); i!=m_VertData->end(); ++i)
		{
			*i=GetState()->Transform.transform_no_trans(*i);
		}
	}

	GetState()->Transform.init();
}
