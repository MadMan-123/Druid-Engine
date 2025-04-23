
#include <cmath>
#include <stdio.h>

#include "maths.h"


Vec2 v2Add(Vec2 a, Vec2 b)
{
    return { a.x + b.x, a.y + b.y };
}


Vec2 v2Sub(Vec2 a, Vec2 b)
{
    return { a.x - b.x, a.y - b.y };
}


Vec2 v2Scale(Vec2 a, float b)
{
    return {a.x * b, a.y * b };
}


Vec2 v2Mul(Vec2 a, Vec2 b)
{
    return { a.x * b.x, a.y * b.y };
}
float v2Mag(Vec2 a)
{
    return sqrt(a.x * a.x + a.y * a.y);
}

float v2Dis(Vec2 a, Vec2 b)
{
    return  (v2Mag(v2Sub(b, a)));
}

Vec2i v2Tov2i(Vec2 a)
{
    return {(int)a.x,(int)a.y};
}

Vec2 v2iTov2(Vec2i a)
{
    return {(float)a.x, (float)a.y};
}



Vec2 v2Div(Vec2 a, float b)
{
	return {(float)(a.x / b),(float)(a.y / b)};
}


Vec3 v3Add(Vec3 a, Vec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 v3Sub(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 v3Scale(Vec3 a, float b)
{

	return {a.x / b, a.y / b, a.z / b};
}

Vec3 v3Mul(Vec3 a, Vec3 b)
{	
	return {a.x * b.x, a.y * b.y, a.z * b.z};
}

float v3Mag(Vec3 a)
{
	return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

float v3Dis(Vec3 a, Vec3 b)
{
	return v3Mag(v3Sub(b,a));
}

Vec3i v3Tov3i(Vec3 a)
{
	return {(u32)a.x, (u32)a.y,(u32)a.z};
}

Vec3 v3iTov3(Vec3i a)
{
	return {(f32)a.x, (f32)a.y,(f32)a.z};
}

Vec3 v3Div(Vec3 a, float b)
{
	return {a.x/b,a.y/b,a.z/b};
}

void matAdd(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] + b[x][y];
		}
	}

}
void matSub(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] - b[x][y];
		}
	}

}


void matDiv(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] / b[x][y];
		}
	}

}
void matMul(f32** a,f32** b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] * b[x][y];
		}
	}

}

void matScale(f32** a,f32 b, Vec2i aSize)
{
	for(u32 x = 0; x < aSize.x; x++)
	{
		for(u32 y = 0; y < aSize.y;y++)
		{
			a[x][y] = a[x][y] * b;
		}
	}

}



