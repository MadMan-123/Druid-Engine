
#include "../../../include/druid.h"

typedef struct
{
	float radius;
}CircleCollider;

typedef struct
{
	Vec2 scale;
}BoxCollider;

typedef struct {
	Vec3 scale;
} Box3DCollider;

typedef struct {
	Mesh* mesh;
	Transform* transform; 
} MeshColliderState;

//NEW COLLIDERS, need testing
Collider* createCubeCollider(Vec3 scale)
{
	Collider* col = (Collider*)malloc(sizeof(Collider));
	if (!col) return NULL;
    
	col->type = Cube;
	col->state = malloc(sizeof(Box3DCollider));
	if (!col->state)
    {
		free(col);
		return NULL;
	}
    
	((Box3DCollider*)col->state)->scale = scale;
	col->layer = 0;
	col->isColliding = false;
	col->response = NULL;
    
	return col;
}

Collider* createMeshCollider(Mesh* mesh, Transform* transform) 
{
	Collider* col = (Collider*)malloc(sizeof(Collider));
	if (!col) return NULL;
    
	col->type = MeshCollider;
	col->state = malloc(sizeof(MeshColliderState));
	if (!col->state)
    {
		free(col);
		return NULL;
	}
    
	MeshColliderState* state = (MeshColliderState*)col->state;
	state->mesh = mesh;
	state->transform = transform;
    
	col->layer = 0;
	col->isColliding = false;
	col->response = NULL;
    
	return col;
}
Collider* createCircleCollider(float radius)
{
	//Allocate memory for the collider
	Collider* allocatedCollider = (Collider*)malloc(sizeof(Collider));
	//Check if the allocation was successful	
	if (allocatedCollider == NULL)
	{
		fprintf(stderr, "spherer colider allocation falied\n");
		return NULL;
	}
	//Set the type of the collider
	allocatedCollider->type = Circle;
	allocatedCollider->state = (CircleCollider*)malloc(sizeof(CircleCollider));
	CircleCollider* iState = (CircleCollider*)(allocatedCollider->state);
	if (iState == NULL)
	{
		fprintf(stderr, "cannot access the internal state for the sphere collider\n");
		return NULL;
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
	if (allocatedCollider == NULL)
	{
		fprintf(stderr, "box colider allocation falied\n");
		return NULL;
	}
	//Set the type of the collider
	allocatedCollider->type = Box;
	allocatedCollider->state = (BoxCollider*)malloc(sizeof(BoxCollider));
	BoxCollider* iState = (BoxCollider*)(allocatedCollider->state);
	if (iState == NULL)
	{
		fprintf(stderr, "cannot access the internal state for the box collider\n");
		free(allocatedCollider);
		return NULL;
	}

	//Set the scale of the collider
	iState->scale = scale;
	allocatedCollider->layer = 0;
	
	return allocatedCollider;
}

float getRadius(Collider* col)
{
	CircleCollider* colState = (CircleCollider*)col->state;
	return colState->radius;
}

Vec2 getScale(Collider* col)
{
	return ((BoxCollider*)col->state)->scale;
}

bool setBoxScale(Collider* col, Vec2 scale)
{
	if(col == NULL)
		return false;

	BoxCollider* colState = (BoxCollider*)col->state;
	colState->scale = scale;
	return true;
}

bool cleanCollider(Collider* col)
{
	if(col == NULL)
		return false;
	
	if(col->state == NULL)
		return false;
	free(col->state);
	col->state = NULL;
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
