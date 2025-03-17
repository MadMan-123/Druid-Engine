#pragma once
#include <glm\glm.hpp>
#include <GL\glew.h>
#include <string>
#include "../../core/obj_loader.h"
#include <cassert>
#include "../../defines.h"
typedef struct 
{
	size_t ammount;
	glm::vec3* positions;
	glm::vec2* texCoords;
	glm::vec3* normals;
}Vertices;

Vertices createVertices(const glm::vec3& pos, const glm::vec2& texCoord);

typedef struct 
{
	enum
	{
		POSITION_VERTEXBUFFER,
		TEXCOORD_VB,
		NORMAL_VB,
		INDEX_VB,
		TEXID_VB,
		NUM_BUFFERS
	};

	//vertex array object
	GLuint vao;
	//array of buffers
	GLuint vab[NUM_BUFFERS];
	unsigned int drawCount; //how much of the vertexArrayObject do we want to draw
}Mesh;

DAPI void draw(Mesh* mesh);
DAPI Mesh* createMesh (Vertices* vertices, unsigned int numVertices, unsigned int* indices, unsigned int numIndices);
DAPI Mesh* loadModel(const std::string& filename);
DAPI void initModel(Mesh* mesh,const IndexedModel &model);
DAPI void freeMesh(Mesh* mesh);


