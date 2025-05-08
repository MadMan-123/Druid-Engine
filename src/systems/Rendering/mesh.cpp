#include "../../../include/druid.h"
#include <vector>
#include <ctime>

#include <cmath>
Mesh* createMesh(Vertices* vertices, u32 numVertices, u32* indices, u32 numIndices)
{
	Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));
	IndexedModel model;
	vertices->ammount = numVertices;
	for (u32 i = 0; i < numVertices; i++)
	{
		model.positions.push_back(vertices->positions[i]);
		model.texCoords.push_back(vertices->texCoords[i]);
		model.normals.push_back(vertices->normals[i]);
	}

	for (u32 i = 0; i < numIndices; i++)
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
	glBindBuffer(GL_ARRAY_BUFFER, mesh->vab[Mesh::POSITION_VERTEXBUFFER]);
	glBufferStorage(GL_ARRAY_BUFFER,
                model.positions.size() * sizeof(model.positions[0]),
                &model.positions[0],
                GL_DYNAMIC_STORAGE_BIT | GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT);
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





// Function to generate height data using compute shader
HeightMap generateHeightMap(int sizeX, int sizeZ, f32 heightScale,const char* computeShaderPath)
{
    // Create compute shader (you can move this to init if preferred)
    static u32 computeShader = createComputeProgram(computeShaderPath);
	if (computeShader == 0) 
	{
    	printf("Failed to create compute shader!\n");
	}
    // Create height buffer
    u32 heightBuffer;
    glGenBuffers(1, &heightBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, heightBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeX * sizeZ * sizeof(f32), 
                nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, heightBuffer);



 	srand(time(0));
	//get a random seed wit time
	u32 seed = rand();
    // Run compute shader
    glUseProgram(computeShader);
    glUniform1i(glGetUniformLocation(computeShader, "gridSize"), sizeX);
    glUniform1f(glGetUniformLocation(computeShader, "heightScale"), heightScale);
    glUniform1i(glGetUniformLocation(computeShader, "seed"), seed);


int workGroupsX = (sizeX + 15) / 16;  // Ceil(sizeX / 16)
int workGroupsY = (sizeZ + 15) / 16;  // Ceil(sizeZ / 16)
glDispatchCompute(workGroupsX, workGroupsY, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Read back height data
    f32* heights = new f32[sizeX * sizeZ];
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeX * sizeZ * sizeof(f32), heights);

    // Clean up
    glDeleteBuffers(1, &heightBuffer);

    return {heights, sizeX, sizeZ};
}

u32* createTerrainIndices(u32 cellsX, u32 cellsZ, u32* outIndexCount)
{
    u32 verticesX = cellsX + 1;
    u32 totalIndices = cellsX * cellsZ * 6;  // 2 triangles per cell, 3 indices per triangle

    // Allocate indices array
    u32* indices = new u32[totalIndices];
    
    // Track index count for caller
    *outIndexCount = totalIndices;

    // Generate indices
    u32 indexIdx = 0;
    for (u32 z = 0; z < cellsZ; ++z) 
    {
        for (u32 x = 0; x < cellsX; ++x) 
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

void calculateTerrainNormals(Vertices* vertices, int width, int height)
 {
    // Reset all normals to zero
    for (int i = 0; i < width * height; i++) {
        vertices->normals[i] = {0, 0, 0};
    }

    // Calculate normals for each triangle, add to shared vertices
    for (int z = 0; z < height - 1; z++) {
        for (int x = 0; x < width - 1; x++) {
            // Get indices for the quad corners
            int topLeft = z * width + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * width + x;
            int bottomRight = bottomLeft + 1;

            // Get positions
            Vec3& v1 = vertices->positions[topLeft];
            Vec3& v2 = vertices->positions[topRight];
            Vec3& v3 = vertices->positions[bottomLeft];
            Vec3& v4 = vertices->positions[bottomRight];

            // Calculate triangle normals using cross product
            // First triangle (v1, v3, v2)
            Vec3 edge1 = {v3.x - v1.x, v3.y - v1.y, v3.z - v1.z};
            Vec3 edge2 = {v2.x - v1.x, v2.y - v1.y, v2.z - v1.z};
            Vec3 normal1 = {
                edge1.y * edge2.z - edge1.z * edge2.y,
                edge1.z * edge2.x - edge1.x * edge2.z,
                edge1.x * edge2.y - edge1.y * edge2.x
            };

            // Add normal to shared vertices
            vertices->normals[topLeft].x += normal1.x;
            vertices->normals[topLeft].y += normal1.y;
            vertices->normals[topLeft].z += normal1.z;
            vertices->normals[topRight].x += normal1.x;
            vertices->normals[topRight].y += normal1.y;
            vertices->normals[topRight].z += normal1.z;
            vertices->normals[bottomLeft].x += normal1.x;
            vertices->normals[bottomLeft].y += normal1.y;
            vertices->normals[bottomLeft].z += normal1.z;

            // Second triangle (v2, v3, v4)
            edge1 = {v3.x - v2.x, v3.y - v2.y, v3.z - v2.z};
            edge2 = {v4.x - v2.x, v4.y - v2.y, v4.z - v2.z};
            Vec3 normal2 = {
                edge1.y * edge2.z - edge1.z * edge2.y,
                edge1.z * edge2.x - edge1.x * edge2.z,
                edge1.x * edge2.y - edge1.y * edge2.x
            };

            // Add normal to shared vertices
            vertices->normals[topRight].x += normal2.x;
            vertices->normals[topRight].y += normal2.y;
            vertices->normals[topRight].z += normal2.z;
            vertices->normals[bottomLeft].x += normal2.x;
            vertices->normals[bottomLeft].y += normal2.y;
            vertices->normals[bottomLeft].z += normal2.z;
            vertices->normals[bottomRight].x += normal2.x;
            vertices->normals[bottomRight].y += normal2.y;
            vertices->normals[bottomRight].z += normal2.z;
        }
    }

    // Normalize all normals
    for (int i = 0; i < width * height; i++) {
        float length = sqrtf(
            vertices->normals[i].x * vertices->normals[i].x +
            vertices->normals[i].y * vertices->normals[i].y +
            vertices->normals[i].z * vertices->normals[i].z
        );
        if (length > 0.0001f) {
            vertices->normals[i].x /= length;
            vertices->normals[i].y /= length;
            vertices->normals[i].z /= length;
        } else {
            vertices->normals[i] = {0, 1, 0}; // Default to up if degenerate
        }
    }
}

Mesh* createTerrainMeshWithHeight(u32 cellsX, u32 cellsZ, f32 cellSize, f32 heightScale, const char* computeShaderPath) 
{
    // First generate height data
    HeightMap heightData = generateHeightMap(cellsX + 1, cellsZ + 1, heightScale,computeShaderPath);

    // Generate vertices with height
    Vertices* terrainVertices = new Vertices();
    terrainVertices->ammount = heightData.width * heightData.height;
    terrainVertices->positions = new Vec3[terrainVertices->ammount];
    terrainVertices->texCoords = new Vec2[terrainVertices->ammount];
    terrainVertices->normals = new Vec3[terrainVertices->ammount];

    // Generate vertex data with heights
    for (u32 z = 0; z < heightData.height; ++z) {
        for (u32 x = 0; x < heightData.width; ++x) {
            u32 idx = z * heightData.width + x;
            f32 height = heightData.heights[idx];

            terrainVertices->positions[idx] = {
                x * cellSize,      // X position
                height,            // Height from compute shader
                z * cellSize       // Z position
            };
			// Normalize to 0.0-1.0 range
			f32 textureScale = 0.1f;  // 1/10 = 0.1
			terrainVertices->texCoords[idx] = {
    			x * cellSize * textureScale,
    			z * cellSize * textureScale
			};
        }
    }

	calculateTerrainNormals(terrainVertices, heightData.width, heightData.height);
    // Generate indices (same as before)
    u32 indexCount;
    u32* terrainIndices = createTerrainIndices(cellsX, cellsZ, &indexCount);

    // Create mesh
    Mesh* terrainMesh = createMesh(terrainVertices, terrainVertices->ammount, terrainIndices, indexCount);

    // Clean up
    delete[] terrainVertices->positions;
    delete[] terrainVertices->texCoords;
    delete[] terrainVertices->normals;
    delete terrainVertices;
    delete[] terrainIndices;
    delete[] heightData.heights;

    return terrainMesh;
}
