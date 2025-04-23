#pragma once
#include "../defines.h"

typedef struct {
	u32 x, y;
}Vec2i;

typedef struct{
	u32 x,y,z;
}Vec3i;

typedef struct {
	f32 x, y;
}Vec2;

typedef struct{
	f32 x,y,z;
}Vec3;



//2D vector methods
DAPI Vec2 v2Add(Vec2 a, Vec2 b);
DAPI Vec2 v2Sub(Vec2 a, Vec2 b);
DAPI Vec2 v2Mul(Vec2 a, float b);
DAPI Vec2 v2Mul(Vec2 a, Vec2 b);
DAPI float v2Mag(Vec2 a);
DAPI float v2Dis(Vec2 a, Vec2 b);
DAPI Vec2i v2Tov2i(Vec2 a);
DAPI Vec2 v2iTov2(Vec2i a);
DAPI Vec2 v2Div(Vec2 a, float b);


//3D vector methods


DAPI Vec3 v3Add(Vec3 a, Vec3 b);
DAPI Vec3 v3Sub(Vec3 a, Vec3 b);
DAPI Vec3 v3Mul(Vec3 a, float b);
DAPI Vec3 v3Mul(Vec3 a, Vec3 b);
DAPI float v3Mag(Vec3 a);
DAPI float v3Dis(Vec3 a, Vec3 b);
DAPI Vec3i v3Tov3i(Vec3 a);
DAPI Vec3 v3iTov3(Vec3i a);
DAPI Vec3 v3Div(Vec3 a, float b);


