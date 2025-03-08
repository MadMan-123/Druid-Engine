
#pragma once
#include "../../Core/maths.h"
#include <stdint.h>



typedef enum
{
	Circle,
	Box
} ColliderType;

typedef struct {
	ColliderType type;
	bool isColliding;
	int layer;
	void* state;
	void (*response)(uint32_t self, uint32_t other);
} Collider;


DAPI Collider* createCircleCollider(float radius);
DAPI Collider* createBoxCollider(Vec2 scale);
DAPI bool cleanCollider(Collider* col);
DAPI bool isCircleColliding(Vec2 posA, float radA, Vec2 posB, float radB);
DAPI bool isBoxColliding(Vec2 posA, Vec2 scaleA, Vec2 posB, Vec2 scaleB);
DAPI float getRadius(Collider* col);
DAPI Vec2 getScale(Collider* col);
DAPI bool setBoxScale(Collider* col, Vec2 scale);

