#include "Light.h"
#include "State.h"
// fluxus->JUCE port: light parameters go through IRenderBackend rather than
// straight to glLight*, which GLES does not have. Same calls in the same order
// at the same time, so desktop output is identical by construction.
#include "RenderBackend.h"

using namespace Fluxus;

Light::Light() :
m_Index(0),
m_Ambient(0.2,0.2,0.2),
m_Diffuse(1,1,1),
m_Specular(1,1,1),
m_Position(0,0,0),
m_Direction(0,0,0),
m_Type(POINT),
m_CameraLock(false)
{
}

Light::~Light()
{
	Backend()->setLightEnabled(m_Index, false);
}

void Light::SetIndex(int s)
{
	m_Index=s;
	Backend()->setLightEnabled(m_Index, true);
}

void Light::SetAmbient(dColour s)
{
	Backend()->setLightColour(m_Index, RLightColour::Ambient, s.arr());
}

void Light::SetDiffuse(dColour s)
{
	Backend()->setLightColour(m_Index, RLightColour::Diffuse, s.arr());
}

void Light::SetSpecular(dColour s)
{
	Backend()->setLightColour(m_Index, RLightColour::Specular, s.arr());
}

void Light::SetSpotAngle(float s)
{
	if (m_Type==SPOT) Backend()->setLightFloat(m_Index, RLightFloat::SpotCutoff, s);
}

void Light::SetSpotExponent(float s)
{
	if (m_Type==SPOT) Backend()->setLightFloat(m_Index, RLightFloat::SpotExponent, s);
}

void Light::SetPosition(dVector s)
{
	m_Position=s;
}

void Light::SetAttenuation(int type, float s)
{
	switch (type)
	{
		case 0: Backend()->setLightFloat(m_Index, RLightFloat::ConstantAttenuation, s); break;
		case 1: Backend()->setLightFloat(m_Index, RLightFloat::LinearAttenuation, s); break;
		case 2: Backend()->setLightFloat(m_Index, RLightFloat::QuadraticAttenuation, s); break;
	}
}

void Light::SetDirection(dVector s)
{
	m_Direction=s;
}


void Light::Render()
{
	// The position is applied through the modelview, as the glTranslatef did:
	// the light then sits at the origin of that translated space.
	Backend()->pushMatrix();
	float t[16] = { 1,0,0,0,  0,1,0,0,  0,0,1,0,
	                m_Position.x, m_Position.y, m_Position.z, 1 };
	Backend()->multMatrix(t);

	if (m_Type==DIRECTIONAL)
	{
		float pos[4] = { m_Direction.x,m_Direction.y,m_Direction.z,0 };
		Backend()->setLightPosition(m_Index, pos);
	}
	else
	{
		if (m_Type==SPOT)
		{
			float pos[4] = { m_Direction.x,m_Direction.y,m_Direction.z,1 };
			Backend()->setLightSpotDirection(m_Index, pos);
		}

		float pos[4] = { 0,0,0,1 };
		Backend()->setLightPosition(m_Index, pos);
	}

	Backend()->popMatrix();
}

