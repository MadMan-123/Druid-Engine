#include <iostream>
#include <druid.h>
Application* game;
Camera camera;
bool rightMouseWasPressed;
float speed = 1000.0f;
float rotateSpeed = 90.0f;

Transform currentTransform, terrainTransform;
glm::vec3 light = glm::vec3(0,2,0);

int timePos;
Mesh* mesh, *terrain;




Texture texture, bumpTexture;
Shader* shader, *terrainShader;

float counter = 0.0f;



void init()
{
	terrainTransform.pos = glm::vec3(-50,-10,-50);
	terrainTransform.rot = glm::vec3(0,0,0);
	terrainTransform.scale = glm::vec3(1,1,1);

	currentTransform.pos = glm::vec3(0, 0, 0);
	currentTransform.rot = glm::vec3(0, 0, 0);
	currentTransform.scale = glm::vec3(1, 1,1);
	//mesh1.init(vertices, sizeof(vertices) / sizeof(vertices[0]), indices, sizeof(indices) / sizeof(indices[0])); //size calcuated by number of bytes of an array / no bytes of one element
	mesh = loadModel("..\\res\\monkey3.obj");
	
	shader = initShader("..\\res\\bump"); //new shader
	
	terrainShader = initShader("..\\res\\terrain");
	
	timePos = glGetUniformLocation(terrainShader->program, "uTime");

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



	terrain = createTerrainMesh(10,10,10);
}

void moveCamera(float dt)
{
       	if (game->input[SDL_SCANCODE_W])
       		moveForward(&camera, (speed * 100 )* dt);
       	//move left and right
       	if (game->input[SDL_SCANCODE_A])
       		moveRight(&camera, (speed * 100)* dt);
       	if (game->input[SDL_SCANCODE_D])
       		moveRight(&camera, (-speed * 100)* dt);
       	if (game->input[SDL_SCANCODE_S])
       		moveForward(&camera, (-speed * 100)* dt);
}

void rotateCamera(float dt)
{
	bool rightMouseIsPressed = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
      
      	if (rightMouseIsPressed) {
      		float x, y;
      		// This gets the relative motion since the last call
      		SDL_GetRelativeMouseState(&x, &y);
          
      		// Only apply rotation if this isn't the first frame the button is pressed
      		if (rightMouseWasPressed) {
      			rotateY(&camera, (((rotateSpeed * 100)* dt) * -x));
             
      			float camPitch = glm::degrees(asin(camera.forward.y));
      			float newPitch = camPitch + (y * ((rotateSpeed * 100)* dt));
      			const float maxPitch = 85.0f;
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
	moveCamera(dt);
      	//pitch and rotate based on mouse movement
      	rotateCamera(dt);			

	
	//rotate the current transform 
	currentTransform.rot += glm::vec3(0,90,0)* 50.0f * dt;	
	printf("%.2f\n",game->fps);
}



void render(float dt)
{

	
	clearDisplay(0.01f, 0.01f, 0.01f, 1);
	//draw the mesh

	//glUniform3f(lightPos,light.x,light.y,light.z);	
	
	glUniform1f(timePos,SDL_GetTicks() / 1000);	
	
	bind(terrainShader);	
	
	

	updateShader(terrainShader,terrainTransform,camera);
	

		
	draw(terrain);

		
}

void destroy()
{
	freeMesh(mesh);
	freeMesh(terrain);
	freeShader(shader);
	freeShader(terrainShader);
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
