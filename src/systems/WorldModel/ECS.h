#pragma once
#include "../../../include/druid.h"
#define MEMBER_COUNT 16
#define SIZE 1024

typedef struct 
{
   u32 size;
   void* buffer[MEMBER_COUNT];
}ComponentData;


typedef struct {
      ComponentData data;
}ECS;

DAPI ECS* createECS();

