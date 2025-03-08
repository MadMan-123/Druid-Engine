#pragma once
#include "../defines.h"

typedef struct {
	int x, y;
}Vec2i;

typedef struct {
	float x, y;
}Vec2;

DAPI Vec2 v2Add(Vec2 a, Vec2 b);
DAPI Vec2 v2Sub(Vec2 a, Vec2 b);
DAPI Vec2 v2Mul(Vec2 a, float b);
DAPI Vec2 v2Mul(Vec2 a, Vec2 b);
DAPI float v2Mag(Vec2 a);
DAPI float v2Dis(Vec2 a, Vec2 b);
DAPI Vec2i v2Tov2i(Vec2 a);
DAPI Vec2 v2iTov2(Vec2i a);
DAPI Vec2 v2Div(Vec2 a, float b);
