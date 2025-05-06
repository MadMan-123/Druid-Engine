#include <iostream>
#include <druid.h>
#include <cmath>
#include <math.h>
Application* game;
Camera camera;
bool rightMouseWasPressed;
float speed = 100000.0f;
float rotateSpeed = 90.0f;

Transform currentTransform, terrainTransform;
Vec3 light = {0,2,0};

int timePos;
Mesh* mesh, *terrain;




u32 texture, bumpTexture;
u32 shader, terrainShader;

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

void testMatrices() {
	// Create simple transform
	Transform transform;
	transform.pos = {1.0f, 2.0f, 3.0f};
	transform.rot = {0.1f, 0.2f, 0.3f};
	transform.scale = {1.0f, 1.0f, 1.0f};
    
	// Test model matrix
	Mat4 model = getModel(&transform);
	printMat4("Model Matrix", model);
    

	// Test view matrix
	Vec3 eye = {0.0f, 0.0f, 10.0f};
	Vec3 target = {0.0f, 0.0f, 0.0f};
	Vec3 up = {0.0f, 1.0f, 0.0f};
	Mat4 view = mat4LookAt(eye, target, up);
	printMat4("View Matrix", view);
    
	// Test projection matrix
	float fov = 3.14159f / 4.0f;  // 45 degrees
	float aspect = 800.0f / 600.0f;
	float near = 0.1f;
	float far = 100.0f;
	Mat4 proj = mat4Perspective(fov, aspect, near, far);
	printMat4("Projection Matrix", proj);
    
	// Test MVP
	Mat4 vp = mat4Mul(proj, view);
	Mat4 mvp = mat4Mul(vp, model);
	printMat4("MVP Matrix", mvp);
    
	// Check for problems
	mat4HasNaN(mvp);
}
void init()
{	
	
	terrainTransform.pos = {-50,-10,-50};
	terrainTransform.rot = {0,0,0};
	terrainTransform.scale = {1,1,1};

	currentTransform.pos = {0, 0, 0};
	currentTransform.rot = {0, 0, 0};
	currentTransform.scale = {1, 1,1};
	//mesh1.init(vertices, sizeof(vertices) / sizeof(vertices[0]), indices, sizeof(indices) / sizeof(indices[0])); //size calcuated by number of bytes of an array / no bytes of one element
	mesh = loadModel("..\\res\\monkey3.obj");
	
	shader = createGraphicsProgram("..\\res\\bump.vert","..\\res\\bump.frag"); //new shader
	
	terrainShader = createGraphicsProgram("..\\res\\terrain.vert","..\\res\\terrain.frag");
	
	timePos = glGetUniformLocation(terrainShader, "uTime");

	texture = initTexture("..\\res\\bricks.jpg");
	bumpTexture = initTexture( "..\\res\\normal.jpg");
	initCamera(
		&camera,
		{0,0,-30},
		70.0f,
		game->display->screenWidth/game->display->screenHeight,
		0.01f,
		1000
		);
	counter = 0.0f;



	terrain = createTerrainMesh(10,10,10);
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

void rotateCamera(float dt)
{
      	if (isMouseDown(SDL_BUTTON_RIGHT)) 
	{
      		f32 x, y;
      		// This gets the relative motion since the last call
      		getMouseDelta(&x, &y);
          
      		// Only apply rotation if this isn't the first frame the button is pressed
      			rotateY(&camera, (((rotateSpeed * 100)* dt) * -x));
             
      			float camPitch = degrees(asin(camera.forward.y));
      			float newPitch = camPitch + (y * ((rotateSpeed * 100)* dt));
      			const float maxPitch = 85.0f;
      			newPitch = clamp(newPitch, -maxPitch, maxPitch);
      			float pitchDelta = newPitch - camPitch;

      			//try and prevent the camera from flipping from gimbal lock
      			if (abs(newPitch) < maxPitch)
		  			pitch(&camera, pitchDelta);
      	}
}


void update(float dt)
{
	moveCamera(dt);
      	//pitch and rotate based on mouse movement
      	rotateCamera(dt);			

	
}



void render(float dt)
{

	
	clearDisplay(0.01f, 0.01f, 0.01f, 1);
	//draw the mesh

	//glUniform3f(lightPos,light.x,light.y,light.z);	
	
	glUniform1f(timePos,SDL_GetTicks() / 1000);	
	
	//bind the shader
	glUseProgram(terrainShader);
	
	updateShaderMVP(terrainShader,terrainTransform,camera);	
	//updateShader(terrainShader,terrainTransform,camera);
	
	
		
	draw(terrain);
	
		
}

void destroy()
{
	freeMesh(mesh);
	freeMesh(terrain);
	freeShader(shader);
	freeShader(terrainShader);
	freeTexture(texture);
	freeTexture(bumpTexture);

}


int main(int argc, char** argv) //argument used to call SDL main
{
	//run test
	testMatrices();

	//create the application
	game = createApplication(init, update, render, destroy);
	
	//assert that the application was created
	assert(game != NULL && "Application could not be created");
	
	//run the application
	run(game);

	destroyApplication(game);
	return 0;
}
