#pragma once
#include <druid.h>

// Defined in hub.cpp
extern Application *hubApplication;
extern c8 hubProjectDir[512];
extern b8 hubProjectSelected;

void hubStart();
void hubUpdate(f32 dt);
void hubRender(f32 dt);
void hubDestroy();
void hubProcessInput(void *appData);

