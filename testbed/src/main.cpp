#include <iostream>
#include <druid.h>
#include <cmath>
#include <math.h>
Application* game;
Camera camera;
bool rightMouseWasPressed;
f32 speed = 400000.0f;
f32 rotateSpeed = 180.0f;
Vec3 light = {0,2,0};

const u32 tileSize = 100;



//TODO: SoA
Mesh* terrain;
Transform terrainTransform;

u32 texture;
u32 terrainShader;




void init()
{	

	terrain = createTerrainMeshWithHeight(
		64, 
		64, 
		tileSize, 
		50.0f,
		"..\\res\\terrain.comp"
	);
		
	terrainTransform= {
		.pos = {-1000,-400,-1000},
		.rot = quatIdentity(),
		.scale = { 1, 1, 1 }
	};
	

	terrainShader = createGraphicsProgram("..\\res\\terrain.vert","..\\res\\terrain.frag");
	
	texture = initTexture("..\\res\\128x128\\Grass\\Grass_04-128x128.png");
	initCamera(
		&camera,
		{0,0,-30},
		70.0f,
		game->display->screenWidth/game->display->screenHeight,
		0.01f,
		5000
		);

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

	bindTexture(texture, 0);
	glUseProgram(terrainShader);
	updateShaderMVP(terrainShader, terrainTransform,camera);
	draw(terrain);
	
		
	
		
}

void destroy()
{
	freeMesh(terrain);
	freeShader(terrainShader);
	freeTexture(texture);


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
