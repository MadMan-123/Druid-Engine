#include <iostream>
#include <druid.h>
#include <cmath>
#include <math.h>
#include <memory>

Application* game;
Camera camera;
bool rightMouseWasPressed;
f32 speed = 300000.0f;
f32 rotateSpeed = 180.0f;
Vec3 light = {-1000,2,-1000};

const u32 tileSize = 100;

/*
static u32 lightingUBO;
//im testing lighting so ive put it in here so far but this might get added to the engine later on
#pragma pack(push, 1)
typedef struct {    
	Vec4 lightPos;
	Vec4 lightColor;
	Vec4 viewPos;
	float intensity;
	Vec3 ambientColor;
	float padding; // to make struct size a multiple of 16 bytes
}LightingUBO;
#pragma pack(pop)

void updateLightingUBO(const Vec3& lightPos, const Vec3& lightColor, const Vec3& viewPos) 
{
	//vec4 values
	Vec4 qlightPos = {lightPos.x, lightPos.y, lightPos.z, 1.0f};	
	Vec4 qlightColor = {lightColor.x, lightColor.y, lightColor.z, 1.0f};
	Vec4 qviewPos = {viewPos.x, viewPos.y, viewPos.z, 1.0f};

	static LightingUBO ubo = {
		qlightPos,
		qlightColor,
		qviewPos,
		.intensity = 1.0f,
		.ambientColor = {0.2f, 0.2f, 0.2f}
	};
    
	glBindBuffer(GL_UNIFORM_BUFFER, lightingUBO);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(LightingUBO), &ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
void initLightingUBO(u32 shader) 
{
	glGenBuffers(1, &lightingUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, lightingUBO);
	glBufferData(GL_UNIFORM_BUFFER, 64, NULL, GL_DYNAMIC_DRAW); // Explicit size
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, lightingUBO);
    
	// Debug check
	GLint blockSize;
	glGetActiveUniformBlockiv(shader, 
		glGetUniformBlockIndex(shader, "Lighting"),
		GL_UNIFORM_BLOCK_DATA_SIZE, 
		&blockSize);
	std::cout << "UBO Block Size: " << blockSize << " bytes" << std::endl;
}

bool isHeightmapColliding(Vec3 position, HeightMap* heightmap, f32 cellSize, f32 radius, const Transform* terrainTransform) 
{

	Vec3 localPos =  position;
    
	// Convert local position to heightmap coordinates
	f32 x = localPos.x / cellSize;
	f32 z = localPos.z / cellSize;
    
	int x0 = (int)floorf(x);
	int x1 = x0 + 1;
	int z0 = (int)floorf(z);
	int z1 = z0 + 1;

	// Check bounds
	if (x0 < 0 || x1 >= heightmap->width || z0 < 0 || z1 >= heightmap->height) 
	{
		return false;
	}
    
	// Get the 4 height samples around the position
	f32 h00 = heightmap->heights[z0 * heightmap->width + x0];
	f32 h10 = heightmap->heights[z0 * heightmap->width + x1];
	f32 h01 = heightmap->heights[z1 * heightmap->width + x0];
	f32 h11 = heightmap->heights[z1 * heightmap->width + x1];
    
	// Bilinear interpolation
	f32 hx0 = lerp(h00, h10, x - x0);
	f32 hx1 = lerp(h01, h11, x - x0);
	f32 localHeight = lerp(hx0, hx1, z - z0);
    
	// Transform height back to world space
	Vec3 heightWorldPos = mat4TransformPoint(getModel(terrainTransform), 
								  (Vec3){localPos.x, localHeight, localPos.z});
    
	// Calculate actual world-space height difference
	f32 worldHeight = heightWorldPos.y;
	f32 distance = position.y - worldHeight;
    
	printf("Distance to height: %f\n", distance);
     
	return position.y - radius <= worldHeight;
}

bool isCubeColliding(Vec3 posA, Vec3 scaleA, Vec3 posB, Vec3 scaleB) 
{
	return (posA.x - scaleA.x <= posB.x + scaleB.x && posA.x + scaleA.x >= posB.x - scaleB.x) &&
		   (posA.y - scaleA.y <= posB.y + scaleB.y && posA.y + scaleA.y >= posB.y - scaleB.y) &&
		   (posA.z - scaleA.z <= posB.z + scaleB.z && posA.z + scaleA.z >= posB.z - scaleB.z);
}
THIS IS JUST A DEAD END FOR NOW, KEEPING THE CODE FOR LATER
*/
//TODO: SoA
Mesh* terrain,* water;
Transform terrainTransform,waterTransform;


u32 grassTexture, waterTexture, stoneTexture, snowTexture;
u32 terrainShader, waterShader;

u32 timeLocation;

u32 grassTextureLoc, stoneTextureLoc, snowTextureLoc;

u32 viewLoc, projLoc;

u32 cubeMapTexture;
Mesh* cubeMapMesh;
u32 skyboxShader;
Transform cubeTransform;

HeightMap terrainMap = {0};
Mesh* knightMesh;

//cool models
Mesh* monkey;
u32 regularShader, warbleShader, psxShader;
u32 monkeyTexture;

//building
Mesh* building;
Transform buildingTransform = 
{
	.pos = {5000,0,5000},
	.rot = quatIdentity(),
	.scale = { 15, 15, 15 }
};


Transform monkeyTransform = 
{
	.pos = {0,1000,0},
	.rot = quatIdentity(),
	.scale = { 20, 20, 20 }
};

Transform orbitTransform = 
{
	.pos = {0,2500,0},
	.rot = quatIdentity(),
	.scale = {100, 100, 100 }
};

Transform warbleTransform = {
	.pos = {5000,1500,-6000},
	.rot = quatIdentity(),
	.scale = { 2500, 2500, 2500 }
};

void init()
{	

	//ooh look a fancy c++ feature,
	std::vector<std::string> faces = 
	{
		"..\\res\\Textures\\Skybox\\right.jpg",
		"..\\res\\Textures\\Skybox\\left.jpg",
		"..\\res\\Textures\\Skybox\\top.jpg",
		"..\\res\\Textures\\Skybox\\bottom.jpg",
		"..\\res\\Textures\\Skybox\\front.jpg",
		"..\\res\\Textures\\Skybox\\back.jpg"
	};

	//This is me writing in C++, look how cool i am
	std::unique_ptr<HeightMap> heightMap = std::make_unique<HeightMap>();
	//all praise the garbage collecter and down with performance
	//god forbid a man just wants to manage his own memory
	
	
	//setup cube map
	cubeMapTexture = createCubeMapTexture(faces);
	printf("CubeMap Texture ID: %u\n", cubeMapTexture);
	
	cubeMapMesh = createSkyboxMesh();
	printf("Skybox Mesh ID: %u\n", cubeMapMesh->vao);
	skyboxShader = createGraphicsProgram("..\\res\\Skybox.vert","..\\res\\Skybox.frag");
	printf("Skybox Shader ID: %u\n", skyboxShader);

		
	viewLoc = glGetUniformLocation(skyboxShader, "view");
	projLoc = glGetUniformLocation(skyboxShader, "projection");	
	cubeTransform = {
		.pos = {0,0,0},
		.rot = quatIdentity(),
		.scale = { 1, 1, 1 }
	};
	
	terrain = createTerrainMeshWithHeight(
		64, 
		64, 
		tileSize, 
		50.0f,
		"..\\res\\terrain.comp",
		&terrainMap
	);
		
	water = createTerrainMesh(
		64, 
		64, 
		tileSize  
	);

	terrainTransform= {
		.pos = {-1000,-500,-1000},
		.rot = quatIdentity(),
		.scale = { 0.9f, 1, 0.9f }
	};
	
	waterTransform= {
		.pos = {-10000,-50,-10000},
		.rot = quatIdentity(),
		.scale = { 4, 1, 4 }
	};

	terrainShader = createGraphicsProgram("..\\res\\terrain.vert","..\\res\\terrain.frag");



	grassTextureLoc = glGetUniformLocation(terrainShader, "grassTexture");	
	stoneTextureLoc = glGetUniformLocation(terrainShader, "stoneTexture");
	snowTextureLoc = glGetUniformLocation(terrainShader, "snowTexture");
	
	//sending multiple textures to the shader
	glUniform1i(grassTextureLoc, 0);	
	glUniform1i(stoneTextureLoc, 1);
	glUniform1i(snowTextureLoc, 2);

	waterShader = createGraphicsProgram("..\\res\\water.vert","..\\res\\water.frag");
	//get a random grass texture, random int and then format it to a string	: "..\\res\\Textures\\Grass({insert index}).png"
	u32 randomGrass = rand() % 2;
	u32 randomStone = rand() % 2;
	//format the string
	printf("Random Grass Texture: %u\n", randomGrass);
	char grassTexturePath[50];
	sprintf(grassTexturePath, "..\\res\\Textures\\Grass(%u).png", randomGrass);
	
	grassTexture = initTexture(grassTexturePath);
	
	char stoneTexturePath[50];
	sprintf(stoneTexturePath, "..\\res\\Textures\\Stone(%u).png", randomStone);
	stoneTexture = initTexture(stoneTexturePath);
	
	snowTexture = initTexture("..\\res\\Textures\\Snow.png");	
	
	
	waterTexture = initTexture("..\\res\\Textures\\water.png");




	//create monkey mesh
	monkey = loadModel("..\\res\\Models\\monkey3.obj");
	monkeyTexture = initTexture("..\\res\\Textures\\water.png");	
	regularShader = createGraphicsProgram("..\\res\\shader.vert","..\\res\\shader.frag");


	//get knight mesh
	knightMesh = loadModel("..\\res\\Models\\knight.obj");	
	
	warbleShader = createGraphicsProgram("..\\res\\warble.vert","..\\res\\warble.frag");
	
	//load the building mesh
	building = loadModel("..\\res\\Models\\Buildings.obj");

	initCamera(
		&camera,
		{1000,950,1000},
		70.0f,
		game->display->screenWidth/game->display->screenHeight,
		0.1f,
		20000	
		);

	timeLocation = glGetUniformLocation(waterShader, "uTime");




	
}

void moveCamera(f32 dt)
{
       	if (isInputDown(KEY_W))
       		moveForward(&camera, (speed  * 100)* dt);
       	//move left and right
       	if (isInputDown(KEY_A))
       		moveRight(&camera, (-speed * 100)* dt);
       	if (isInputDown(KEY_D))
       		moveRight(&camera, (speed * 100)* dt);
       	if (isInputDown(KEY_S))
       		moveForward(&camera, (-speed * 100)* dt);
}

// In camera struct:
f32 yaw = 0;
f32 currentPitch = 0;

void rotateCamera(f32 dt) 
{
	if (isMouseDown(SDL_BUTTON_RIGHT)) 
	{
		// Get the mouse delta
		f32 x, y;
		getMouseDelta(&x, &y);


		//apply the mouse delta to the camera
		yaw += -x * (rotateSpeed * 200.0f) * dt;
		currentPitch += -y * (rotateSpeed * 100.0f) * dt;


		//89 in radians
		f32 goal = radians(89.0f);
		// Clamp pitch to avoid gimbal lock
		currentPitch = clamp(currentPitch,-goal, goal);

		// Create yaw quaternion based on the world-up vector
		Vec4 yawQuat = quatFromAxisAngle(v3Up, yaw);
		Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
		camera.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
	}
}



f32 angle = 0.1f;
f32 angle2 = 0.1f;

void update(f32 dt)
{
	moveCamera(dt);
    rotateCamera(dt);			

	if(game->fps < 1000000000)
	{
		//format the fps to a string
		char fpsToString[50];
		sprintf(fpsToString, "FPS: %.0f", game->fps);
		//	display the fps in the window title
		SDL_SetWindowTitle(game->display->sdlWindow, fpsToString);
	}

	//slowly rotate the monkey
	//turn around the y axis	
	monkeyTransform.rot = quatFromAxisAngle(v3Up, angle);
	angle += 1000 * dt;
	angle2 += 1000000 * dt;
	Vec2 offset = {1000,1000};

	//get the distance from the camera to the orbit transform
	f32 distance = v3Dis(camera.pos, orbitTransform.pos);
	f32 speed = (1000000 * distance) * dt;
	//rotate the orbit transform around the terrain with cos and sin	
	orbitTransform.pos.x =  speed * cosf(radians(angle2)) + offset.x;
	orbitTransform.pos.z =  speed * sinf(radians(angle2)) + offset.y;
	
	//make the orbit transform look at the camera
	Vec3 lookAt = v3Sub(camera.pos, orbitTransform.pos);
	Vec4 lookAtQuat = quatFromAxisAngle(v3Up, atan2f(lookAt.z, lookAt.x));
	
	orbitTransform.rot = quatMul(lookAtQuat, quatFromAxisAngle({0.0f,-1.0f,0.0f}, radians(90.0f)));
}



void render(f32 dt) 
{


	// Clear and setup
	clearDisplay(0.1f, 0.1f, 0.1f, 0.0f);
    

	glDepthFunc(GL_LEQUAL);  

	// Skybox
	glDepthMask(GL_FALSE);

	glUseProgram(skyboxShader);

	
	Mat4 skyboxView = getView(&camera, true);
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &skyboxView.m[0][0]);
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, &camera.projection.m[0][0]);
	


	glBindVertexArray(cubeMapMesh->vao);
	glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
	
	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);    

	//Terrain with lighting
	glUseProgram(terrainShader);
	// Light properties
	glUniform3f(glGetUniformLocation(terrainShader, "lightPos"), 
			   1000.0f, 2000.0f, 1000.0f);
	glUniform3f(glGetUniformLocation(terrainShader, "lightColor"), 
			   1.0f, 1.0f, 1.0f); // Pure white
	glUniform1f(glGetUniformLocation(terrainShader, "lightIntensity"), 
			   1.0f);
	glUniform3f(glGetUniformLocation(terrainShader, "ambientColor"), 
			   0.2f, 0.2f, 0.2f);
	// Texture bindings
	bindTexture(grassTexture, 0, GL_TEXTURE_2D);
	bindTexture(stoneTexture, 1, GL_TEXTURE_2D);
	bindTexture(snowTexture, 2, GL_TEXTURE_2D);
	// Uniforms
	glUniform1i(grassTextureLoc, 0);
	glUniform1i(stoneTextureLoc, 1); 
	glUniform1i(snowTextureLoc, 2);
	// Transform
	updateShaderMVP(terrainShader, terrainTransform, camera);
	draw(terrain);

	//Water with lighting and time effect
	glUseProgram(waterShader);


	glUniform3f(glGetUniformLocation(waterShader, "lightPos"), 
			   1000.0f, 2000.0f, 1000.0f);
	glUniform3f(glGetUniformLocation(waterShader, "lightColor"), 
			   1.0f, 1.0f, 1.0f);
	glUniform1f(glGetUniformLocation(waterShader, "lightIntensity"), 
			   1.0f);
	glUniform3f(glGetUniformLocation(waterShader, "ambientColor"), 
			   0.2f, 0.2f, 0.2f);

	bindTexture(waterTexture, 0, GL_TEXTURE_2D);

	glUniform1f(timeLocation, (float)SDL_GetTicks() / 1000);
	updateShaderMVP(waterShader, waterTransform, camera);



	draw(water);

	//render monkey
	glUseProgram(regularShader);
	bindTexture(monkeyTexture, 0, GL_TEXTURE_2D);
	updateShaderMVP(regularShader, monkeyTransform, camera);
	draw(monkey);
	
	//draw another monkey orbiting around the terrain
	bindTexture(stoneTexture, 0, GL_TEXTURE_2D);
	updateShaderMVP(regularShader, orbitTransform, camera);
	draw(monkey);

	//draw buildings
	bindTexture(stoneTexture, 0, GL_TEXTURE_2D);
	updateShaderMVP(regularShader, buildingTransform, camera);
	draw(building);

	//draw the warble knight
	glUseProgram(warbleShader);
	//update shader uTime
	glUniform1f(glGetUniformLocation(warbleShader, "time"), (f32)SDL_GetTicks() / 1000);
	updateShaderMVP(warbleShader, warbleTransform, camera);
	bindTexture(stoneTexture, 0, GL_TEXTURE_2D);
	draw(knightMesh);
	
	
}

void destroy()
{
	freeMesh(terrain);
	freeShader(terrainShader);
	freeShader(waterShader);
	freeTexture(grassTexture);
	freeTexture(stoneTexture);
	freeTexture(snowTexture);
	freeTexture(waterTexture);
	freeTexture(cubeMapTexture);
	freeTexture(monkeyTexture);
	freeMesh(monkey);
	freeShader(skyboxShader);
	freeMesh(cubeMapMesh);
	freeShader(regularShader);
	freeShader(warbleShader);	
	freeMesh(knightMesh);
	freeMesh(building);
}


int main(int argc, char** argv) //argument used to call SDL main
{
	//create the application
	game = createApplication(init, update, render, destroy);
	
	//assert that the application was created
	assert(game != NULL && "Application could not be created");
	
	game->width = 1920;	
	game->height = 1080;
	//run the application
	run(game);

	destroyApplication(game);
	return 0;
}
