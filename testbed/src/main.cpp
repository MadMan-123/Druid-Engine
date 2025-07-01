
#include <druid.h>
#include <cmath>
//meta data

Application* game;
Camera camera;
bool rightMouseWasPressed;
f32 speed = 300000.0f;
f32 rotateSpeed = 180.0f;
Vec3 light = {-1000,2,-1000};

const u32 tileSize = 100;


Mesh* terrain,* water;
Transform terrainTransform,waterTransform;


//Texture handles
u32 grassTexture, waterTexture, stoneTexture, snowTexture;
u32 cubeMapTexture;

//shader handles 
u32 terrainShader, waterShader;
u32 skyboxShader;
u32 regularShader, warbleShader;

//uniform location p
u32 timeLocation;
u32 grassTextureLoc, stoneTextureLoc, snowTextureLoc;
u32 viewLoc, projLoc;

//Meshes
Mesh* cubeMapMesh;
Mesh* knight;
Transform cubeTransform;

HeightMap terrainMap = {0};
Vec3* positions;
Vec4* rotations;
Vec3* scales;
		


#define MAX 10
DEFINE_ARCHETYPE(WorldObject,
		//position
		FIELD(Vec3, pos),
		FIELD(Vec4, rot),
		FIELD(Vec3, scale)
		);


EntityArena* WorldObjects;
void init()
{
	WorldObjects = createEntityArena(&WorldObject,MAX);	
	printEntityArena(WorldObjects);
	
	positions = (Vec3*)WorldObjects->fields[0];
	rotations = (Vec4*)WorldObjects->fields[1];
	scales = (Vec3*)WorldObjects->fields[2];
		
	for(u32 i = 0; i < MAX; i++)
	{
	
		positions[i] = {100,i * 10,100};
		rotations[i] = quatIdentity();
		scales[i] = {10,10,10};
		
	}

	
	
	knight = loadModel("../res/Models/Knight.obj");
	//ooh look a fancy c++ feature,
	const char* faces[6] = 
	{
		"../res/Textures/Skybox/right.jpg",
		"../res/Textures/Skybox/left.jpg",
		"../res/Textures/Skybox/top.jpg",
		"../res/Textures/Skybox/bottom.jpg",
		"../res/Textures/Skybox/front.jpg",
		"../res/Textures/Skybox/back.jpg"
	};

	
	
	//setup cube map
	cubeMapTexture = createCubeMapTexture(faces,6);
	
	cubeMapMesh = createSkyboxMesh();
	
	skyboxShader = createGraphicsProgram("../res/Skybox.vert","../res/Skybox.frag");

	//get uniform locations 
	viewLoc = glGetUniformLocation(skyboxShader, "view");
	projLoc = glGetUniformLocation(skyboxShader, "projection");	
	
    	//setup transforms 
    	//we could of had these set globaly but it dosent really matter
    	cubeTransform = {
		{0,0,0},
		quatIdentity(),
		{ 1, 1, 1 }
	};
	
    	//creates a terrain plane and applies a compute shader
	terrain = createTerrainMeshWithHeight(
		64, 
		64, 
		tileSize, 
		50.0f,
		"../res/terrain.comp",
		&terrainMap
	);
		
	water = createTerrainMesh(
		64, 
		64, 
		tileSize  
	);

	terrainTransform = {
		{-1000,-500,-1000},
		quatIdentity(),
		{ 0.9f, 1, 0.9f }
	};
	
	waterTransform= {
		{-10000,-50,-10000},
		quatIdentity(),
		{ 4, 1, 4 }
	};

    	//create shaders for graphics
	terrainShader = createGraphicsProgram("../res/terrain.vert","../res/terrain.frag");


    //get uniform locations
	grassTextureLoc = glGetUniformLocation(terrainShader, "grassTexture");	
	stoneTextureLoc = glGetUniformLocation(terrainShader, "stoneTexture");
	snowTextureLoc = glGetUniformLocation(terrainShader, "snowTexture");
	
	//sending multiple textures to the shader
	glUniform1i(grassTextureLoc, 0);	
	glUniform1i(stoneTextureLoc, 1);
	glUniform1i(snowTextureLoc, 2);


	waterShader = createGraphicsProgram("../res/water.vert","../res/water.frag");
	
    //get a random grass texture, random int and then format it to a string	: "..\\res\\Textures\\Grass({insert index}).png"
	u32 randomGrass = rand() % 2;
	u32 randomStone = rand() % 2;
	//format the string
	printf("Random Grass Texture: %u\n", randomGrass);
	char grassTexturePath[50];
	sprintf_s(grassTexturePath, "../res/Textures/Grass(%u).png", randomGrass);
	
	grassTexture = initTexture(grassTexturePath);
	
	char stoneTexturePath[50];
	sprintf_s(stoneTexturePath, "../res/Textures/Stone(%u).png", randomStone);
	stoneTexture = initTexture(stoneTexturePath);
	snowTexture = initTexture("../res/Textures/Snow.png");	
	waterTexture = initTexture("../res/Textures/water.png");

	//create monkey mesh
	regularShader = createGraphicsProgram("../res/shader.vert","../res/shader.frag");

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
		sprintf_s(fpsToString, "FPS: %.0f", game->fps);
		//	display the fps in the window title
		SDL_SetWindowTitle(game->display->sdlWindow, fpsToString);
	}

	for(u32 i = 0; i < MAX; i++)
	{
		positions[i] = v3Add(positions[i],(Vec3){0,0,sin(i)});
	}
}



void render(f32 dt) 
{

	// Clear and setup
	clearDisplay(0.1f, 0.1f, 0.1f, 0.0f);
    
    //Draw the skybox
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
	//texture bindings
	bindTexture(grassTexture, 0, GL_TEXTURE_2D);
	bindTexture(stoneTexture, 1, GL_TEXTURE_2D);
	bindTexture(snowTexture, 2, GL_TEXTURE_2D);
	//uniforms
	glUniform1i(grassTextureLoc, 0);
	glUniform1i(stoneTextureLoc, 1); 
	glUniform1i(snowTextureLoc, 2);
	updateShaderMVP(terrainShader, terrainTransform, camera);
	draw(terrain);

	//Water with lighting and time effect
	glUseProgram(waterShader);

    //not the most efficent way but i dont think the performance cost is too big

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


	glUseProgram(regularShader);
	

	Transform newTransform = {0};
	//draw cubes 
	for(u32 i = 0; i < MAX; i++)
	{
		newTransform = 
		{
			positions[i],
			rotations[i],
			scales[i]
		};

		updateShaderMVP(regularShader,newTransform,camera);

		draw(knight);
	}
}

void destroy()
{

	freeEntityArena(WorldObjects);
    //big ol free
	freeMesh(terrain);
	freeShader(terrainShader);
	freeShader(waterShader);
	freeTexture(grassTexture);
	freeTexture(stoneTexture);
	freeTexture(snowTexture);
	freeTexture(waterTexture);
	freeTexture(cubeMapTexture);
	freeShader(skyboxShader);
	freeMesh(cubeMapMesh);
	freeShader(regularShader);
	freeShader(warbleShader);	
	freeMesh(knight);
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
