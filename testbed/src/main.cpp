#include <iostream>
#include <druid.h>
#include <cmath>
#include <math.h>
Application* game;
Camera camera;
bool rightMouseWasPressed;
float speed = 200000.0f;
float rotateSpeed = 180.0f;

Transform currentTransform, terrainTransform;
Vec3 light = {0,2,0};
const u32 gridSize = 3;

int timePos;


//TODO: SoA
Mesh* mesh, *terrainGrid[gridSize][gridSize];
Transform terrainTransforms[gridSize][gridSize];

const int tileSize = 55;



u32 texture, bumpTexture;
u32 shader, terrainShader;

u32 computeShader;

float counter = 0.0f;


void printMat4(const char* name, Mat4 mat) {
	printf("%s:\n", name);
	for (int i = 0; i < 4; i++) {
		printf("  ");
		for (int j = 0; j < 4; j++) {
			printf("%f ", mat.m[i][j]);
		}
		printf("\n");
	}
	printf("\n");
}

// Check if matrix contains NaN or Inf values
bool mat4HasNaN(Mat4 mat) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (isnan(mat.m[i][j]) || isinf(mat.m[i][j])) {
				printf("NaN/Inf found at m[%d][%d] = %f\n", i, j, mat.m[i][j]);
				return true;
			}
		}
	}
	return false;
}

void init()
{	



	int offset = 495;
	for (int z = 0; z < gridSize; ++z) {
		for (int x = 0; x < gridSize; ++x) {
			terrainGrid[z][x] = createTerrainMesh(10, 10, tileSize);
			terrainTransforms[z][x] = {
				.pos = { (x - 1) * (tileSize + offset), -10.0f, (z - 1) * (tileSize + offset)},
				.rot = { 0, 0, 0 },
				.scale = { 1, 1, 1 }
			};
		}
	}	


	currentTransform.pos = {0, 0, 0};
	currentTransform.rot = {0, 0, 0};
	currentTransform.scale = {1, 1,1};
	//mesh1.init(vertices, sizeof(vertices) / sizeof(vertices[0]), indices, sizeof(indices) / sizeof(indices[0])); //size calcuated by number of bytes of an array / no bytes of one element
	mesh = loadModel("..\\res\\monkey3.obj");
	
	shader = createGraphicsProgram("..\\res\\bump.vert","..\\res\\bump.frag"); //new shader
	
	terrainShader = createGraphicsProgram("..\\res\\terrain.vert","..\\res\\terrain.frag");
	
	timePos = glGetUniformLocation(terrainShader, "uTime");

	texture = initTexture("..\\res\\128x128\\Grass\\Grass_02-128x128.png");
	initCamera(
		&camera,
		{0,0,-30},
		70.0f,
		game->display->screenWidth/game->display->screenHeight,
		0.01f,
		2500
		);
	counter = 0.0f;

	//turn vsync off
	SDL_GL_SetSwapInterval(0);

	computeShader = createComputeProgram("..\\res\\terrain.comp");

	
	u32 totalTerrainSize = tileSize * tileSize *  (gridSize * gridSize);
/*
	for (int z = 0; z < gridSize; ++z) 
	{
		for (int x = 0; x < gridSize; ++x) 
		{

			// Create the terrain buffer
			u32 terrainBuffer;
			glGenBuffers(1, &terrainBuffer);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, terrainBuffer);
			glBufferData(GL_SHADER_STORAGE_BUFFER, totalTerrainSize * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, terrainBuffer);


	
			glUseProgram(computeShader);



			glUniform2f(glGetUniformLocation(computeShader, "terrainSize"), tileSize, tileSize);


			//glUniform2f(glGetUniformLocation(computeShader, "offset"), x * tileSize, z * tileSize);
			glUniform1f(glGetUniformLocation(computeShader, "heightScale"), 100.0f);
			glUniform1i(glGetUniformLocation(computeShader, "seed"), rand()); // Random seed

			// Workgroups = ceil(tileSize / 16.0)
			int wg = (tileSize + 15) / 16;
			glDispatchCompute(wg, wg, 1);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		}
	}
*/

}

void moveCamera(float dt)
{
       	if (isInputDown(KEY_W))
       		moveForward(&camera, (speed )* dt);
       	//move left and right
       	if (isInputDown(KEY_A))
       		moveRight(&camera, (-speed )* dt);
       	if (isInputDown(KEY_D))
       		moveRight(&camera, (speed )* dt);
       	if (isInputDown(KEY_S))
       		moveForward(&camera, (-speed )* dt);
}

// In camera struct:
f32 yaw = 0;
f32 currentPitch = 0;

void rotateCamera(float dt) 
{
	if (isMouseDown(SDL_BUTTON_RIGHT)) 
	{
		f32 x, y;
		getMouseDelta(&x, &y);

		yaw += -x * (rotateSpeed * 200.0f) * dt;
		currentPitch += -y * (rotateSpeed * 100.0f) * dt;


		//89 in radians
		f32 goal = radians(89.0f);
		// Clamp pitch to avoid gimbal lock
		currentPitch = clamp(currentPitch,-goal, goal);


		// Rebuild quaternion from scratch (prevents drift)
		Vec4 yawQuat   = quatFromAxisAngle(v3Up, yaw);
		Vec4 pitchQuat = quatFromAxisAngle(v3Right, currentPitch);
		camera.orientation = quatNormalize(quatMul(yawQuat, pitchQuat));
	}
}




void update(float dt)
{
	moveCamera(dt);
      	//pitch and rotate based on mouse movement
      	rotateCamera(dt);			

	//print fps
	//printf("FPS: %f\n", game->fps);
	
}



void render(float dt)
{

	
	clearDisplay(0.01f, 0.01f, 0.01f, 1);
	//draw the mesh

	//glUniform3f(lightPos,light.x,light.y,light.z);	
	
	bindTexture(texture, 0);
	glUseProgram(terrainShader);
	glUniform1f(timePos, SDL_GetTicks() / 1000);
	for (int z = 0; z < gridSize; ++z) {
		for (int x = 0; x < gridSize; ++x) {
			updateShaderMVP(terrainShader, terrainTransforms[z][x], camera);
			//pass the index of the current terrain	
			glUniform1i(glGetUniformLocation(terrainShader, "panelIndex"), x + z * gridSize);
			draw(terrainGrid[z][x]);
		}
	}	
	
		
	
		
}

void destroy()
{
	freeMesh(mesh);
	freeShader(shader);
	freeShader(terrainShader);
	freeShader(computeShader);
	freeTexture(texture);
	freeTexture(bumpTexture);
	for (int z = 0; z < gridSize; ++z) 
	{
		for (int x = 0; x < gridSize; ++x)
		{
			//free the terrain mesh		
			freeMesh(terrainGrid[z][x]);
		}
	}	
}


int main(int argc, char** argv) //argument used to call SDL main
{
	//create the application
	game = createApplication(init, update, render, destroy);
	
	//assert that the application was created
	assert(game != NULL && "Application could not be created");
	
	//run the application
	run(game);

	destroyApplication(game);
	return 0;
}
