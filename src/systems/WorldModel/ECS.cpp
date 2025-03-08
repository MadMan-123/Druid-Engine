#include "ECS.h"

#include "boost/pfr.hpp"
#include <cassert>
#include <cstdlib>

struct A 
{
    int a;
    float b; 
};

//pass in the type of struct (as so we can index into the struct) and the pointer to the struct

ECS * createECS()
{
   
   //create the ECS
   ECS * ecs = (ECS*)malloc(sizeof(ECS));
   //assert that the ECS was created
   assert(ecs != NULL && "ECS could not be created");

   //initialize the ECS
   i32 packedComSize = 0;

   for (int i = 0; i < MEMBER_COUNT; ++i)
   {
      //get the size of the component
      //get element of struct by index
         
   }

   const u32 bytes = SIZE * (packedComSize);
   A a;
   //allocate memory for the ECS
   ecs->data.buffer[0] = malloc(bytes);

   //build the buffer pointers
   for (int i = 0; i < MEMBER_COUNT; ++i)
   {
      //get the type of the component
      
      //ecs->data.buffer[i + 1] = (boost::pfr::get<i,A>(a))(ecs->data.buffer[0]) + SIZE;
   }
   
   
   return ecs;
}


