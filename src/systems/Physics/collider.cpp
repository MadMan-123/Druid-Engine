
#include "collider.h"
#include <cstdlib>
#include <cstdio>

typedef struct
{
	float radius;
}CircleCollider;

typedef struct
{
	Vec2 scale;
}BoxCollider;


Collider* createCircleCollider(float radius)
{
	//Allocate memory for the collider
	Collider* allocatedCollider = (Collider*)malloc(sizeof(Collider));
	//Check if the allocation was successful	
	if (allocatedCollider == nullptr)
	{
		fprintf(stderr, "spherer colider allocation falied\n");
		return nullptr;
	}
	//Set the type of the collider
	allocatedCollider->type = Circle;
	allocatedCollider->state = (CircleCollider*)malloc(sizeof(CircleCollider));
	auto iState = (CircleCollider*)(allocatedCollider->state);
	if (iState == nullptr)
	{
		fprintf(stderr, "cannot access the internal state for the sphere collider\n");
		return nullptr;
	}
	//Set the radius of the collider
	iState->radius = radius;
	allocatedCollider->layer = 0;
	
	return allocatedCollider;

}

Collider* createBoxCollider(Vec2 scale)
{
	//Allocate memory for the collider
	Collider* allocatedCollider = (Collider*)malloc(sizeof(Collider));
	if (allocatedCollider == nullptr)
	{
		fprintf(stderr, "box colider allocation falied\n");
		return nullptr;
	}
	//Set the type of the collider
	allocatedCollider->type = Box;
	allocatedCollider->state = (BoxCollider*)malloc(sizeof(BoxCollider));
	auto iState = (BoxCollider*)(allocatedCollider->state);
	if (iState == nullptr)
	{
		fprintf(stderr, "cannot access the internal state for the box collider\n");
		free(allocatedCollider);
		return nullptr;
	}

	//Set the scale of the collider
	iState->scale = scale;
	allocatedCollider->layer = 0;
	
	return allocatedCollider;
}

float getRadius(Collider* col)
{
	auto colState = (CircleCollider*)col->state;
	return colState->radius;
}

Vec2 getScale(Collider* col)
{
	if(col->type != ColliderType::Box)
		return {0,0};
	return ((BoxCollider*)col->state)->scale;
}

bool setBoxScale(Collider* col, Vec2 scale)
{
	if(col == nullptr)
		return false;

	auto colState = (BoxCollider*)col->state;
	colState->scale = scale;
	return true;
}

bool cleanCollider(Collider* col)
{
	if(col == nullptr)
		return false;
	
	if(col->state == nullptr)
		return false;
	free(col->state);
	col->state = nullptr;
	free(col);
	return true;
}

bool isCircleColliding(Vec2 posA, float radA, Vec2 posB, float radB)
{
	return v2Mag(v2Sub(posB, posA)) <= (radA + radB);
}

//AABB collision detection
bool isBoxColliding(Vec2 posA, Vec2 scaleA, Vec2 posB, Vec2 scaleB)
{
	return (posA.x + scaleA.x >= posB.x && posB.x + scaleB.x >= posA.x) && 
		(posA.y + scaleA.y >= posB.y && posB.y + scaleB.y >= posA.y);
}
