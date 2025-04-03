#include "mesh.h"
#include <vector>


Mesh* createMesh(Vertices* vertices, unsigned int numVertices, unsigned int* indices, unsigned int numIndices)
{
	Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));
	IndexedModel model;
	vertices->ammount = numVertices;
	for (unsigned int i = 0; i < numVertices; i++)
	{
		model.positions.push_back(vertices->positions[i]);
		model.texCoords.push_back(vertices->texCoords[i]);
		model.normals.push_back(vertices->normals[i]);
	}

	for (unsigned int i = 0; i < numIndices; i++)
		model.indices.push_back(indices[i]);

	initModel(mesh,model);


	return mesh;
}


void initModel(Mesh* mesh,const IndexedModel &model)
{
	
	mesh->drawCount = model.indices.size();

	//generate our VAO to store the state of our vertex buffer
	glGenVertexArrays(1, &mesh->vao);
	//bind our vao (this will allow us to render from our buffers)
	glBindVertexArray(mesh->vao); 
	//generate our buffers to store our vertex data on the GPU
	glGenBuffers(Mesh::NUM_BUFFERS, mesh->vab); 

	//tell opengl what type of data the buffer is (GL_ARRAY_BUFFER), and pass the data
	glBindBuffer(GL_ARRAY_BUFFER, mesh->vab[0]); 

	//move the data to the GPU - type of data, size of data, starting address (pointer) of data, where do we store the data on the GPU (determined by type)
	glBufferData(GL_ARRAY_BUFFER, model.positions.size() * sizeof(model.positions[0]), &model.positions[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	
	//tell opengl what type of data the buffer is (GL_ARRAY_BUFFER), and pass the data
	glBindBuffer(GL_ARRAY_BUFFER, mesh->vab[Mesh::TEXCOORD_VB]); 
	glBufferData(GL_ARRAY_BUFFER, model.texCoords.size() * sizeof(model.texCoords[0]), &model.texCoords[0], GL_STATIC_DRAW);
	//move the data to the GPU - type of data, size of data, starting address (pointer) of data, where do we store the data on the GPU
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, mesh->vab[Mesh::NORMAL_VB]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(model.normals[0]) * model.normals.size(), &model.normals[0], GL_STATIC_DRAW);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, 0);

	
	//tell opengl what type of data the buffer is (GL_ARRAY_BUFFER), and pass the data
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->vab[Mesh::INDEX_VB]);

	//move the data to the GPU - type of data, size of data, starting address (pointer) of data, where do we store the data on the GPU
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(model.indices[0]), &model.indices[0], GL_STATIC_DRAW);

	
	glBindVertexArray(0); // unbind our VAO
}
void freeMesh(Mesh *mesh)
{
	free(mesh);
}

void destroyMesh(Mesh* mesh)
{
	
	mesh->drawCount = NULL;
	glDeleteVertexArrays(1, &mesh->vao);
	glDeleteBuffers(Mesh::NUM_BUFFERS, mesh->vab);
	free(mesh);
}

Mesh* loadModel(const std::string& filename)
{
	Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));

	//assert that the mesh was created
	assert(mesh != NULL && "Mesh could not be created");
	
	IndexedModel model = OBJModel(filename).ToIndexedModel();
	initModel(mesh, model);

	return mesh;
}



void draw(Mesh* mesh)
{
	glBindVertexArray(mesh->vao);
	
	glDrawElements(GL_TRIANGLES, mesh->drawCount, GL_UNSIGNED_INT, 0);
	//glDrawArrays(GL_TRIANGLES, 0, drawCount);
	
	glBindVertexArray(0);
}



// Generate terrain vertices with pre-allocated arrays
Vertices* createTerrainVertices(unsigned int cellsX, unsigned int cellsZ, float cellSize) {
    // Calculate total number of vertices
    unsigned int verticesX = cellsX + 1;
    unsigned int verticesZ = cellsZ + 1;
    unsigned int totalVertices = verticesX * verticesZ;

    // Allocate Vertices structure with raw arrays
    Vertices* vertices = new Vertices();
    vertices->ammount = totalVertices;
    
    // Allocate raw arrays for positions, texcoords, and normals
    vertices->positions = new glm::vec3[totalVertices];
    vertices->texCoords = new glm::vec2[totalVertices];
    vertices->normals = new glm::vec3[totalVertices];

    // Generate vertex data
    for (unsigned int z = 0; z < verticesZ; ++z) {
        for (unsigned int x = 0; x < verticesX; ++x) {
            unsigned int idx = z * verticesX + x;

            // Position calculation
            vertices->positions[idx] = glm::vec3(
                x * cellSize,      // X position
                0.0f,              // Initial flat height
                z * cellSize        // Z position
            );

            // Texture coordinate mapping
            vertices->texCoords[idx] = glm::vec2(
                static_cast<float>(x) / cellsX,  // U coordinate
                static_cast<float>(z) / cellsZ   // V coordinate
            );

            // Normal vector (upward for flat terrain)
            vertices->normals[idx] = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    return vertices;
}

// Generate indices for terrain
unsigned int* createTerrainIndices(unsigned int cellsX, unsigned int cellsZ, unsigned int* outIndexCount) {
    unsigned int verticesX = cellsX + 1;
    unsigned int totalIndices = cellsX * cellsZ * 6;  // 2 triangles per cell, 3 indices per triangle

    // Allocate indices array
    unsigned int* indices = new unsigned int[totalIndices];
    
    // Track index count for caller
    *outIndexCount = totalIndices;

    // Generate indices
    unsigned int indexIdx = 0;
    for (unsigned int z = 0; z < cellsZ; ++z) 
    {
        for (unsigned int x = 0; x < cellsX; ++x) 
	{
            // First triangle of the cell
            indices[indexIdx++] = z * verticesX + x;
            indices[indexIdx++] = (z + 1) * verticesX + x;
            indices[indexIdx++] = z * verticesX + x + 1;

            // Second triangle of the cell
            indices[indexIdx++] = z * verticesX + x + 1;
            indices[indexIdx++] = (z + 1) * verticesX + x;
            indices[indexIdx++] = (z + 1) * verticesX + x + 1;
        }
    }

    return indices;
}

// Create terrain mesh using the existing createMesh function
 Mesh* createTerrainMesh(unsigned int cellsX, unsigned int cellsZ, float cellSize) {
    // Generate vertices
    Vertices* terrainVertices = createTerrainVertices(cellsX, cellsZ, cellSize);

    // Generate indices
    unsigned int indexCount;
    unsigned int* terrainIndices = createTerrainIndices(cellsX, cellsZ, &indexCount);

    // Create mesh using the provided createMesh function
    Mesh* terrainMesh = createMesh(terrainVertices, terrainVertices->ammount, terrainIndices, indexCount);

    // Clean up temporary arrays (createMesh should have made copies)
    delete[] terrainVertices->positions;
    delete[] terrainVertices->texCoords;
    delete[] terrainVertices->normals;
    delete terrainVertices;
    delete[] terrainIndices;

    return terrainMesh;
}

