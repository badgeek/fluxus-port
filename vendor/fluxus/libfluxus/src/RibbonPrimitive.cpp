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
#include "RibbonPrimitive.h"
#include "State.h"
#include "RenderBackend.h"   // fluxus->JUCE port: draws go through the seam now

using namespace Fluxus;

RibbonPrimitive::RibbonPrimitive() :
    m_InverseNormals(false)
{
	AddData("p",new TypedPData<dVector>);
	AddData("w",new TypedPData<float>);
	AddData("c",new TypedPData<dColour>);
	PDataDirty();
}

RibbonPrimitive::RibbonPrimitive(const RibbonPrimitive &other) :
Primitive(other)
{
	PDataDirty();
}

RibbonPrimitive::~RibbonPrimitive()
{
}

RibbonPrimitive* RibbonPrimitive::Clone() const
{
	return new RibbonPrimitive(*this);
}

void RibbonPrimitive::PDataDirty()
{
	// reset pointers
	m_VertData=GetDataVec<dVector>("p");
	m_WidthData=GetDataVec<float>("w");
	m_ColData=GetDataVec<dColour>("c");
}

void RibbonPrimitive::Render()
{
	if (m_VertData->size()<2) return;

	if (m_State.Hints & HINT_UNLIT) Backend()->setLighting(false);
	if (m_State.Hints & HINT_AALIAS) glEnable(GL_LINE_SMOOTH);

	if (m_State.Hints & HINT_SPHERE_MAP)
	{
		glEnable(GL_TEXTURE_GEN_S);
		glEnable(GL_TEXTURE_GEN_T);
		glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
		glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	}

	// fluxus->JUCE port: the camera-facing strip is built into vertex arrays and
	// issued as ONE backend draw, replacing the glBegin/glVertex loop. Same
	// vertices in the same order with the same normals — only the delivery
	// changed, so this is invisible on desktop GL (test/golden.sh) while making
	// the primitive drawable by a GLES backend, which has no immediate mode.
	// The two former branches differ only in the colour array, so they are one
	// loop now.
	if (m_State.Hints & HINT_SOLID)
	{
		const bool vertcols = (m_State.Hints & HINT_VERTCOLS) != 0;
		const unsigned int count = m_VertData->size();
		m_DrawPos.resize(count*2);
		m_DrawNrm.resize(count*2);
		m_DrawTex.resize(count*2);
		if (vertcols) m_DrawCol.resize(count*2);

		for (unsigned int n=0; n<count; n++)
		{
			float tx = n/(float)count;
			dVector line;
			if (n==count-1)
			{
				line=(*m_VertData)[n]-(*m_VertData)[n-1];
				tx=1.0f;
			}
			else line=(*m_VertData)[n+1]-(*m_VertData)[n];
			dVector up=line.cross(GetLocalCameraDir());
			up.normalise();

			dVector topnorm=up;
			dVector botnorm=-up;

			if (m_InverseNormals)
			{
				topnorm=-up;
				botnorm=up;
			}

			const unsigned int i=n*2;
			m_DrawPos[i]   = (*m_VertData)[n]-(up*(*m_WidthData)[n]);
			m_DrawNrm[i]   = botnorm;
			m_DrawTex[i]   = dVector(tx,0,0);
			m_DrawPos[i+1] = (*m_VertData)[n]+(up*(*m_WidthData)[n]);
			m_DrawNrm[i+1] = topnorm;
			m_DrawTex[i+1] = dVector(tx,1,0);
			if (vertcols)
			{
				m_DrawCol[i]   = (*m_ColData)[n];
				m_DrawCol[i+1] = (*m_ColData)[n];
			}
		}

		if (!vertcols)
		{
			const float *c = m_State.Colour.arr();
			Backend()->setColour(c[0],c[1],c[2],c[3]);
		}

		RVertexArrays va;
		va.pos = m_DrawPos[0].arr();
		va.nrm = m_DrawNrm[0].arr();
		va.tex = m_DrawTex[0].arr();
		va.col = vertcols ? m_DrawCol[0].arr() : 0;
		va.stride = sizeof(dVector);
		Backend()->drawArrays(RPrim::TriStrip, va, (int)count*2, 0, 0);
	}

	if (m_State.Hints & HINT_WIRE)
	{
		Backend()->setLighting(false);

		if ((m_State.Hints & HINT_WIRE_STIPPLED) > HINT_WIRE)
		{
			glEnable(GL_LINE_STIPPLE);
			glLineStipple(m_State.StippleFactor, m_State.StipplePattern);
		}

		// fluxus->JUCE port: the wire pass is the centreline as one LineStrip
		// through the backend. It draws the pdata positions directly, so only
		// the texcoords need building.
		const bool vertcols = (m_State.Hints & HINT_VERTCOLS) != 0;
		const unsigned int count = m_VertData->size();
		m_DrawTex.resize(count);
		for (unsigned int n=0; n<count; n++) m_DrawTex[n] = dVector(n/(float)count,0,0);

		if (!vertcols)
		{
			const float *c = m_State.WireColour.arr();
			Backend()->setColour(c[0],c[1],c[2],c[3]);
		}

		RVertexArrays va;
		va.pos = (*m_VertData)[0].arr();
		va.nrm = 0;                       // lighting is off for the wire pass
		va.tex = m_DrawTex[0].arr();
		va.col = vertcols ? (*m_ColData)[0].arr() : 0;
		va.stride = sizeof(dVector);
		Backend()->drawArrays(RPrim::LineStrip, va, (int)count, 0, 0);

		if ((m_State.Hints & HINT_WIRE_STIPPLED) > HINT_WIRE)
		{
			glDisable(GL_LINE_STIPPLE);
		}

		Backend()->setLighting(true);
	}

	if (m_State.Hints & HINT_AALIAS) glDisable(GL_LINE_SMOOTH);
	if (m_State.Hints & HINT_UNLIT) Backend()->setLighting(true);
	if (m_State.Hints & HINT_SPHERE_MAP)
	{
		glDisable(GL_TEXTURE_GEN_S);
		glDisable(GL_TEXTURE_GEN_T);
	}
}

dBoundingBox RibbonPrimitive::GetBoundingBox(const dMatrix &space)
{
	dBoundingBox box;
	for (unsigned int n=0; n<m_VertData->size()-1; n++)
	{
		box.expand(space.transform((*m_VertData)[n]));
	}
	return box;
}

void RibbonPrimitive::ApplyTransform(bool ScaleRotOnly)
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

