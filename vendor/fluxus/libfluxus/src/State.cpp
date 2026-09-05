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
#include "TexturePainter.h"
#include "State.h"
#include "RenderBackend.h"   // fluxus->JUCE port: route GL through the backend seam
// fluxus->JUCE minimal port: PixelPrimitive excluded (pulls in ODE physics);
// not referenced in this translation unit.
//#include "PixelPrimitive.h"

using namespace Fluxus;

State::State() :
Colour(1,1,1),
Shinyness(1.0f),
Opacity(1.0f),
Parent(1),
Hints(HINT_SOLID),
LineWidth(1),
StippledLines(false),
StippleFactor(4),
StipplePattern(0xaaaa),
PointWidth(1),
SourceBlend(GL_SRC_ALPHA),
DestinationBlend(GL_ONE_MINUS_SRC_ALPHA),
WireColour(1,1,1),
NormalColour(1,0,0),
WireOpacity(1.0f),
ColourMode(MODE_RGB),
Shader(NULL),
Cull(true)
{
	for (int c=0; c<MAX_TEXTURES; c++)
	{
		Textures[c]=0;
	}
}

State::State(const State &other)
{
	*this=other;
}

const State &State::operator=(const State &other)
{
	Colour=other.Colour;
	Specular=other.Specular;
	Emissive=other.Emissive;
	Ambient=other.Ambient;
	Shinyness=other.Shinyness;
	Opacity=other.Opacity;
	Parent=other.Parent;
	Hints=other.Hints;
	LineWidth=other.LineWidth;
	StippledLines=other.StippledLines;
	StippleFactor=other.StippleFactor;
	StipplePattern=other.StipplePattern;
	PointWidth=other.PointWidth;
	SourceBlend=other.SourceBlend;
	DestinationBlend=other.DestinationBlend;
	WireColour=other.WireColour;
	NormalColour=other.NormalColour;
	WireOpacity=other.WireOpacity;
	ColourMode=other.ColourMode;
	Transform=other.Transform;
	Shader=other.Shader;
	Cull=other.Cull;

	if (Shader!=NULL)
	{
		Shader->IncRef();
	}
	for (int n=0; n<MAX_TEXTURES; n++)
	{
		Textures[n]=other.Textures[n];
		TextureStates[n]=other.TextureStates[n];
	}

	return *this;
}

State::~State()
{
	if (Shader!=NULL && Shader->DecRef()) delete Shader;
}

void State::Apply()
{
	// fluxus->JUCE port: matrix/material/colour/blend/cull routed via IRenderBackend
	Backend()->multMatrix(Transform.arr());
	if (Opacity != 1.0f) Colour.a=Ambient.a=Emissive.a=Specular.a=Opacity;
	if (WireOpacity != 1.0f) WireColour.a=WireOpacity;
	Backend()->setColour(Colour.r,Colour.g,Colour.b,Colour.a);
	Backend()->setMaterial(Ambient.arr(),Emissive.arr(),Colour.arr(),Specular.arr(),Shinyness);
	Backend()->setLineWidth(LineWidth);
	Backend()->setPointSize(PointWidth);
	Backend()->setBlend(SourceBlend,DestinationBlend);
	Backend()->setCull(Cull);
	Backend()->setFrontFaceCW((Hints&HINT_CULL_CCW)!=0);

	if (Hints & HINT_NORMALISE)
		Backend()->setNormaliseNormals(true);

	if (Hints & HINT_NOZWRITE)
		glDepthMask(false);

	TexturePainter::Get()->SetCurrent(Textures,TextureStates);

	if (Shader != NULL)
	{
		if (Hints & HINT_POINTS)
			Backend()->setProgramPointSize(true);

		Shader->Apply();
	}
	else GLSLShader::Unapply();
}

void State::Unapply()
{
	if (Hints & HINT_NORMALISE)
		Backend()->setNormaliseNormals(false);

	if (Hints & HINT_NOZWRITE)
		glDepthMask(true);

	if (Shader != NULL)
	{
		if (Hints & HINT_POINTS)
			Backend()->setProgramPointSize(false);
	}
}

void State::Spew()
{
	Trace::Stream<<"Colour: "<<Colour<<endl
		<<"Specular: "<<Specular<<endl
		<<"Ambient: "<<Ambient<<endl
		<<"Emissive: "<<Emissive<<endl
		<<"Shinyness: "<<Shinyness<<endl
		<<"Opacity: "<<Opacity<<endl
		<<"WireOpacity: "<<WireOpacity<<endl
		<<"Texture: "<<Textures[0]<<endl
		<<"Parent: "<<Parent<<endl
		<<"Hints: "<<Hints<<endl
		<<"LineWidth: "<<LineWidth<<endl
		<<"Transform: "<<Transform<<endl;
}

