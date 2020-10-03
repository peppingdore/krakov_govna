#pragma once

#include "b_lib/Basic.h"

#include "b_lib/Reflection.h"
#include "b_lib/Math.h"


using Entity_Id = u32;

struct Entity
{
	Vector3    position;
	Quaternion rotation;
	Vector3    scale;

	Entity_Id  id;
};

REFLECT(Entity)
	MEMBER(position);
	MEMBER(rotation);
	MEMBER(scale);

	MEMBER(id);
REFLECT_END();







