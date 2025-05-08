#include <iostream>
#include <druid.h>
#include <cmath>
#include <math.h>
Application* game;
Camera camera;
bool rightMouseWasPressed;
f32 speed = 200000.0f;
f32 rotateSpeed = 180.0f;

Transform currentTransform, terrainTransform;
Vec3 light = {0,2,0};

int timePos;
const u32 tileSize = 55;

const u32 gridSize = 3;


//TODO: SoA
Mesh* mesh, *terrainGrid[gridSize][gridSize];
Transform terrainTransforms[gridSize][gridSize];



u32 texture;
u32 terrainShader;


f32 counter = 0.0f;


void init()
{	



	int offset = 495;
	for (int z = 0; z < gridSize; ++z) {
		for (int x = 0; x < gridSize; ++x) {
			terrainGrid[z][x] = createTerrainMeshWithHeight(
				10, 
				10, 
				tileSize, 
				50.0f,
				"..\\res\\terrain.comp");
			terrainTransforms[z][x] = {
				.pos = { (x - 1) * (tileSize + offset), -100.0f, (z - 1) * (tileSize + offset)},
				.rot = quatIdentity(),
				.scale = { 1, 1, 1 }
			};
		}
	}	


	currentTransform.pos = {0, 0, 0};
	currentTransform.rot = quatIdentity();
	currentTransform.scale = {1, 1,1};
	//mesh1.init(vertices, sizeof(vertices) / sizeof(vertices[0]), indices, sizeof(indices) / sizeof(indices[0])); //size calcuated by number of bytes of an array / no bytes of one element
	mesh = loadModel("..\\res\\monkey3.obj");
	
	
	terrainShader = createGraphicsProgram("..\\res\\terrain.vert","..\\res\\terrain.frag");
	
	timePos = glGetUniformLocation(terrainShader, "uTime");

	texture = initTexture("..\\res\\128x128\\Grass\\Grass_04-128x128.png");
	initCamera(
		&camera,
		{0,0,-30},
		70.0f,
		game->display->screenWidth/game->display->screenHeight,
		0.01f,
		2500
		);
	counter = 0.0f;



}

void moveCamera(f32 dt)
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

void rotateCamera(f32 dt) 
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




void update(f32 dt)
{
	moveCamera(dt);
      	//pitch and rotate based on mouse movement
      	rotateCamera(dt);			

	//print fps
	//printf("FPS: %f\n", game->fps);
	
}



void render(f32 dt)
{

	
	clearDisplay(0.01f, 0.01f, 0.01f, 1);
	//draw the mesh

	//glUniform3f(lightPos,light.x,light.y,light.z);	
	
	bindTexture(texture, 0);
	glUseProgram(terrainShader);
	for (int z = 0; z < gridSize; ++z) {
		for (int x = 0; x < gridSize; ++x) {
			updateShaderMVP(terrainShader, terrainTransforms[z][x], camera);
			//pass the index of the current terrain	
			draw(terrainGrid[z][x]);
		}
	}	
	
		
	
		
}

void destroy()
{
	freeMesh(mesh);
	freeShader(terrainShader);
	freeTexture(texture);
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
