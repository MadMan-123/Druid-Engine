#include <druid.h>

// Blank testbed: minimal application that hooks into the engine but does nothing.

Application *game = NULL;

Camera cam = {0};
f32 fov = 70.0f;
u32 defaultShader = 0;

DEFINE_ARCHETYPE(StaticEntites,
	FIELD(bool, isActive),
	FIELD(Vec3, position),
	FIELD(Vec4, rotation),
	FIELD(Vec3, scale),
	FIELD(u32, modelID)
);


//each field array
bool* isActive = NULL;
Vec3* positions = NULL;
Vec4* rotations = NULL;
Vec3* scales = NULL;
u32* modelIDs = NULL;


void init(void) 
{
    // intentionally empty
	initCamera(&cam, (Vec3){0,0,5}, fov, 16.0f/9.0f, 0.1f, 100.0f);

	//get the default shader from the resources

	u32 index = 0;
	findInMap(&resources->shaderIDs, "default", &index);
	defaultShader = resources->shaderHandles[index];
	assert(defaultShader != 0 && "Default shader not found");
	createEntityArena(&StaticEntites, 128);

	//loop through the entities and set their model to default model 0
	//get the field pointer for each field
	isActive = (bool*)&StaticEntites.fields[0];
	positions = (Vec3*)&StaticEntites.fields[1];
	rotations = (Vec4*)&StaticEntites.fields[2];
	scales = (Vec3*)&StaticEntites.fields[3];
	modelIDs = (u32*)&StaticEntites.fields[4];



	for (u32 i = 0; i < StaticEntites.count; i++) 
	{
		isActive[i] = false;
		modelIDs[i] = 0;
	}


}

void update(f32 dt) {
    // intentionally empty
}

void render(f32 dt) {
    clearDisplay(0.2f, 0.2f, 0.2f, 1.0f);
	
	//render model 0
	glUseProgram(defaultShader);
	Model* model = &resources->modelBuffer[0];
	for(int i = 0; i < StaticEntites.count; i++) {
		if(!isActive[i]) continue;
		Transform t = {positions[i], rotations[i], scales[i]};
		updateShaderMVP(defaultShader, t, cam);
		//draw the model 
		draw(model, defaultShader);



			
	}

	
}

void destroy(void) {
    // intentionally empty
}

int main(int argc, char **argv) {
    game = createApplication(init, update, render, destroy);
    assert(game != NULL && "Application could not be created");
    game->width = 1280; game->height = 720;
    run(game);
    destroyApplication(game);
    return 0;
}


