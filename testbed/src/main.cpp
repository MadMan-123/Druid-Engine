#include <iostream>
#include "druid.h"
Application* game;
Camera camera;
bool rightMouseWasPressed;
float speed = 0.1f;
float rotateSpeed = 0.05f;

GLint lightPos = -1;
GLint cameraPos = -1;
Transform currentTransform;

glm::vec3 light = glm::vec3(0,2,0);

Mesh* mesh;
Texture texture, bumpTexture;
Shader* shader;

float counter = 0.0f;



void init()
{

	currentTransform.pos = glm::vec3(0, 0, 0);
	currentTransform.rot = glm::vec3(0, 0, 0);
	currentTransform.scale = glm::vec3(1, 1,1);
	//mesh1.init(vertices, sizeof(vertices) / sizeof(vertices[0]), indices, sizeof(indices) / sizeof(indices[0])); //size calcuated by number of bytes of an array / no bytes of one element
	mesh = loadModel("..\\res\\monkey3.obj");
	
	shader = initShader("..\\res\\bump"); //new shader
	
	initTexture(&texture,"..\\res\\bricks.jpg");
	initTexture(&bumpTexture, "..\\res\\normal.jpg");
	initCamera(
		&camera,
		{0,0,-30},
		70.0f,
		game->display->screenWidth/game->display->screenHeight,
		0.01f,
		1000
		);
	counter = 0.0f;

	lightPos = glGetUniformLocation(shader->program,"lightPos");

	cameraPos = glGetUniformLocation(shader->program,"camPos");

}

void moveCamera()
{
       	if (game->input[SDL_SCANCODE_W])
       		moveForward(&camera, speed);
       	//move left and right
       	if (game->input[SDL_SCANCODE_A])
       		moveRight(&camera, speed);
       	if (game->input[SDL_SCANCODE_D])
       		moveRight(&camera, -speed);
       	if (game->input[SDL_SCANCODE_S])
       		moveForward(&camera, -speed);
}

void rotateCamera()
{
	bool rightMouseIsPressed = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
      
      	if (rightMouseIsPressed) {
      		float x, y;
      		// This gets the relative motion since the last call
      		SDL_GetRelativeMouseState(&x, &y);
          
      		// Only apply rotation if this isn't the first frame the button is pressed
      		if (rightMouseWasPressed) {
      			rotateY(&camera, rotateSpeed * -x);
             
      			float camPitch = glm::degrees(asin(camera.forward.y));
      			float newPitch = camPitch + (y * rotateSpeed);
      			const float maxPitch = 89.0f;
      			newPitch = glm::clamp(newPitch, -maxPitch, maxPitch);
      			float pitchDelta = newPitch - camPitch;

      			//try and prevent the camera from flipping from gimbal lock
      			if (abs(newPitch) < maxPitch)
		  			pitch(&camera, pitchDelta);
	
      			
      		}
      	}
      
      	// Clear the relative state when button is first pressed
      	if (rightMouseIsPressed && !rightMouseWasPressed) {
      		SDL_GetRelativeMouseState(NULL, NULL); // Clear any accumulated movement
      	}
      	
      	rightMouseWasPressed = rightMouseIsPressed;


}


void update(float dt)
{
	moveCamera();
      	//pitch and rotate based on mouse movement
      	rotateCamera();			

	
	//rotate the current transform 
	currentTransform.rot += glm::vec3(0,90,0)* 50.0f * dt;	
	printf("%.2f\n",game->fps);
}



void render(float dt)
{

	
	clearDisplay(0.01f, 0.01f, 0.01f, 1);
	//draw the mesh

	//glUniform3f(lightPos,light.x,light.y,light.z);	
	
	bind(shader);
	
	auto cPos = camera.pos;
	glUniform3f(cameraPos, cPos.x,cPos.y,cPos.z);

	updateShader(shader,currentTransform, camera);

	GLuint diffuseLoc = glGetUniformLocation(shader->program,"diffuse");
	GLuint normalLoc = glGetUniformLocation(shader->program, "normal");

	bind(&texture, 0);
	glUniform1i(diffuseLoc,0);

	bind(&bumpTexture,1);
	glUniform1i(normalLoc,1);

	draw(mesh);
	
}

void destroy()
{
	freeMesh(mesh);
	freeShader(shader);
	freeTexture(&texture);
	freeTexture(&bumpTexture);

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
