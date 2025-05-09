#include "../../../include/druid.h"
#include <vector>
#include <ctime>

#include <cmath>
Mesh* createMesh(Vertices* vertices, u32 numVertices, u32* indices, u32 numIndices)
{
	Mesh* mesh = (Mesh*)malloc(sizeof(Mesh));
	IndexedModel model;
	if(vertices != NULL)
    { 
		
		vertices->ammount = numVertices;
		for (u32 i = 0; i < numVertices; i++)
		{
			model.positions.push_back(vertices->positions[i]);
			model.texCoords.push_back(vertices->texCoords[i]);
			model.normals.push_back(vertices->normals[i]);
		}
	}
	if(indices != NULL)
	{
		for (u32 i = 0; i < numIndices; i++)
			model.indices.push_back(indices[i]);
	}
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
    f32* heights =  (f32*)malloc(sizeof(f32)*(sizeX * sizeZ));
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

Mesh* createTerrainMeshWithHeight(u32 cellsX, u32 cellsZ, f32 cellSize, f32 heightScale, const char* computeShaderPath, HeightMap* output) 
{
    // First generate height data
    HeightMap heightData = generateHeightMap(cellsX + 1, cellsZ + 1, heightScale,computeShaderPath);
		
	//copuy all the data from the heightmap to the output
	if(output != NULL)	
    {
		//setup heights array
		output->heights = (f32*)malloc(sizeof(f32) * heightData.width * heightData.height);
		//copy the height data to the output
		memcpy(output->heights, heightData.heights, sizeof(f32) * heightData.width * heightData.height);
        output->width = heightData.width;
        output->height = heightData.height;
    }
    

    // Generate vertices with height
    Vertices* terrainVertices =  (Vertices*)malloc(sizeof(Vertices));
    terrainVertices->ammount = heightData.width * heightData.height;
    terrainVertices->positions = (Vec3*)malloc(terrainVertices->ammount * sizeof(Vec3));
    terrainVertices->texCoords = (Vec2*)malloc(terrainVertices->ammount * sizeof(Vec2));
    terrainVertices->normals = (Vec3*)malloc(terrainVertices->ammount * sizeof(Vec3));


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
	free(heightData.heights);
	free(terrainVertices->positions);
	free(terrainVertices->texCoords);
	free(terrainVertices->normals);
	free(terrainVertices);


    return terrainMesh;
}


Vertices* createTerrainVertices(unsigned int cellsX, unsigned int cellsZ, float cellSize) {
    // Calculate total number of vertices
    unsigned int verticesX = cellsX + 1;
    unsigned int verticesZ = cellsZ + 1;
    unsigned int totalVertices = verticesX * verticesZ;

    Vertices* vertices = new Vertices();
    vertices->ammount = totalVertices;
    
    vertices->positions = new Vec3[totalVertices];
    vertices->texCoords = new Vec2[totalVertices];
    vertices->normals = new Vec3[totalVertices];

    // Generate vertex data
    for (unsigned int z = 0; z < verticesZ; ++z) {
        for (unsigned int x = 0; x < verticesX; ++x) {
            unsigned int idx = z * verticesX + x;

            // Position calculation
            vertices->positions[idx] = {
                x * cellSize,      // X position
                0.0f,              // Initial flat height
                z * cellSize        // Z position
            };

            // Texture coordinate mapping
            vertices->texCoords[idx] = {
                static_cast<float>(x) / cellsX,  // U coordinate
                static_cast<float>(z) / cellsZ   // V coordinate
            };

            // Normal vector (upward for flat terrain)
            vertices->normals[idx] = v3Up;
        }
    }

    return vertices;
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

Mesh* createBoxMesh() 
{
    // Define vertices for a unit cube centered at origin
    // Each face needs its own vertices since texture coordinates are different
    Vertices* boxVertices = (Vertices*)malloc(sizeof(Vertices));
    boxVertices->ammount = 24; // 4 vertices per face * 6 faces
    
    boxVertices->positions = (Vec3*)malloc(boxVertices->ammount * sizeof(Vec3));
    boxVertices->texCoords = (Vec2*)malloc(boxVertices->ammount * sizeof(Vec2)); 
    boxVertices->normals = (Vec3*)malloc(boxVertices->ammount * sizeof(Vec3));  
    
    // Fill in vertex data for each face
    // FRONT FACE (Z+)
    boxVertices->positions[0] = {-1.0f,  1.0f,  1.0f};
    boxVertices->positions[1] = {-1.0f, -1.0f,  1.0f};
    boxVertices->positions[2] = { 1.0f, -1.0f,  1.0f};
    boxVertices->positions[3] = { 1.0f,  1.0f,  1.0f};
    
    // BACK FACE (Z-)
    boxVertices->positions[4] = { 1.0f,  1.0f, -1.0f};
    boxVertices->positions[5] = { 1.0f, -1.0f, -1.0f};
    boxVertices->positions[6] = {-1.0f, -1.0f, -1.0f};
    boxVertices->positions[7] = {-1.0f,  1.0f, -1.0f};
    
    // LEFT FACE (X-)
    boxVertices->positions[8] = {-1.0f,  1.0f, -1.0f};
    boxVertices->positions[9] = {-1.0f, -1.0f, -1.0f};
    boxVertices->positions[10] = {-1.0f, -1.0f,  1.0f};
    boxVertices->positions[11] = {-1.0f,  1.0f,  1.0f};
    
    // RIGHT FACE (X+)
    boxVertices->positions[12] = { 1.0f,  1.0f,  1.0f};
    boxVertices->positions[13] = { 1.0f, -1.0f,  1.0f};
    boxVertices->positions[14] = { 1.0f, -1.0f, -1.0f};
    boxVertices->positions[15] = { 1.0f,  1.0f, -1.0f};
    
    // TOP FACE (Y+)
    boxVertices->positions[16] = {-1.0f,  1.0f, -1.0f};
    boxVertices->positions[17] = {-1.0f,  1.0f,  1.0f};
    boxVertices->positions[18] = { 1.0f,  1.0f,  1.0f};
    boxVertices->positions[19] = { 1.0f,  1.0f, -1.0f};
    
    // BOTTOM FACE (Y-)
    boxVertices->positions[20] = {-1.0f, -1.0f,  1.0f};
    boxVertices->positions[21] = {-1.0f, -1.0f, -1.0f};
    boxVertices->positions[22] = { 1.0f, -1.0f, -1.0f};
    boxVertices->positions[23] = { 1.0f, -1.0f,  1.0f};
    
    // Fill unused texcoords and normals with zeros
    for (u32 i = 0; i < boxVertices->ammount; i++) {
        boxVertices->texCoords[i] = {0.0f, 0.0f};
        boxVertices->normals[i] = {0.0f, 0.0f, 0.0f};
    }
    
    // Define indices for the cube
    u32 indices[] = {
        // Front face
        0, 1, 2, 0, 2, 3,
        // Back face
        4, 5, 6, 4, 6, 7,
        // Left face
        8, 9, 10, 8, 10, 11,
        // Right face
        12, 13, 14, 12, 14, 15,
        // Top face
        16, 17, 18, 16, 18, 19,
        // Bottom face
        20, 21, 22, 20, 22, 23
    };
    
    // Create the mesh
    Mesh* boxMesh = createMesh(boxVertices, boxVertices->ammount, indices, 36);
    
    // Clean up
	free(boxVertices->positions);
	free(boxVertices->texCoords);
	free(boxVertices->normals);
	free(boxVertices);
    
    return boxMesh;
}
Mesh* createSkyboxMesh() 
{
    // Define vertices for a skybox 
    Vertices* skyboxVertices = (Vertices*)malloc(sizeof(Vertices));
    if (!skyboxVertices) return NULL;
    
    skyboxVertices->ammount = 36;
    
    // Allocate and initialize positions
    skyboxVertices->positions = (Vec3*)malloc(36 * sizeof(Vec3));
    if (!skyboxVertices->positions) {
        free(skyboxVertices);
        return NULL;
    }
    
    // Allocate empty (but valid) arrays for texCoords and normals
    skyboxVertices->texCoords = (Vec2*)malloc(36 * sizeof(Vec2));
    skyboxVertices->normals = (Vec3*)malloc(36 * sizeof(Vec3));
    if (!skyboxVertices->texCoords || !skyboxVertices->normals) {
        free(skyboxVertices->positions);
        free(skyboxVertices->texCoords);
        free(skyboxVertices->normals);
        free(skyboxVertices);
        return NULL;
    }
    
    // Initialize with default values
    memset(skyboxVertices->texCoords, 0, 36 * sizeof(Vec2));
    memset(skyboxVertices->normals, 0, 36 * sizeof(Vec3));
    
    // Fill vertex positions 
    // Back face (Z-)
    skyboxVertices->positions[0] = {-1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[1] = {-1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[2] = { 1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[3] = { 1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[4] = { 1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[5] = {-1.0f,  1.0f, -1.0f};

    // Left face (X-)
    skyboxVertices->positions[6] = {-1.0f, -1.0f,  1.0f};
    skyboxVertices->positions[7] = {-1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[8] = {-1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[9] = {-1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[10] = {-1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[11] = {-1.0f, -1.0f,  1.0f};

    // Right face (X+)
    skyboxVertices->positions[12] = { 1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[13] = { 1.0f, -1.0f,  1.0f};
    skyboxVertices->positions[14] = { 1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[15] = { 1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[16] = { 1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[17] = { 1.0f, -1.0f, -1.0f};

    // Front face (Z+)
    skyboxVertices->positions[18] = {-1.0f, -1.0f,  1.0f};
    skyboxVertices->positions[19] = {-1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[20] = { 1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[21] = { 1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[22] = { 1.0f, -1.0f,  1.0f};
    skyboxVertices->positions[23] = {-1.0f, -1.0f,  1.0f};

    // Top face (Y+)
    skyboxVertices->positions[24] = {-1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[25] = { 1.0f,  1.0f, -1.0f};
    skyboxVertices->positions[26] = { 1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[27] = { 1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[28] = {-1.0f,  1.0f,  1.0f};
    skyboxVertices->positions[29] = {-1.0f,  1.0f, -1.0f};

    // Bottom face (Y-)
    skyboxVertices->positions[30] = {-1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[31] = {-1.0f, -1.0f,  1.0f};
    skyboxVertices->positions[32] = { 1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[33] = { 1.0f, -1.0f, -1.0f};
    skyboxVertices->positions[34] = {-1.0f, -1.0f,  1.0f};
    skyboxVertices->positions[35] = { 1.0f, -1.0f,  1.0f};

    // Create the mesh 
    Mesh* skyboxMesh = createMesh(skyboxVertices, 36, NULL, 0);
    
    // Clean up
    free(skyboxVertices->positions);
    free(skyboxVertices->texCoords);
    free(skyboxVertices->normals);
    free(skyboxVertices);
    
    return skyboxMesh;
    
}