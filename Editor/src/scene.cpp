#define DRUID_H 
#include "scene.h"

SceneMetaData bakeScene(Scene* scene)
{
   //check if the scene is valid
   if(scene == NULL)
    {
         ERROR("bakeScene: Invalid scene");
         return {0};
    }
     SceneMetaData data = {0};

     // Count total entities across all archetypes/arenas
     u32 totalEntities = 0;
     if(scene->archetypes && scene->archetypeCount > 0)
     {
          for(u32 a = 0; a < scene->archetypeCount; a++)
          {
               Archetype *arch = &scene->archetypes[a];
               if(!arch || !arch->arena) continue;
               for(u32 ai = 0; ai < arch->arenaCount; ai++)
               {
                    totalEntities += arch->arena[ai].count;
               }
          }
     }

     data.entityCount = totalEntities;

     if(totalEntities == 0)
     {
          // nothing to bake
          return data;
     }

     // Create a temporary layout for saved entities: position, rotation, scale, archetypeID
     FieldInfo saved_fields[] = {
          FIELD(Vec3, position),
          FIELD(Vec4, rotation),
          FIELD(Vec3, scale),
          FIELD(u32, archetypeID),
     };

     StructLayout savedLayout = {"SavedEntity", saved_fields, (u32)(sizeof(saved_fields) / sizeof(FieldInfo))};

     // Create an archetype to hold the baked data
     if(!createArchetype(&savedLayout, totalEntities, &data.savedEntities))
     {
          ERROR("bakeScene: Failed to create saved archetype");
          return data;
     }

     // Get pointers to output fields
     void **outFields = getArchetypeFields(&data.savedEntities, 0);
     if(!outFields)
     {
          ERROR("bakeScene: Failed to get saved archetype fields");
          return data;
     }

     Vec3 *outPositions = (Vec3 *)outFields[0];
     Vec4 *outRotations = (Vec4 *)outFields[1];
     Vec3 *outScales = (Vec3 *)outFields[2];
     u32 *outArchIDs = (u32 *)outFields[3];

     // Iterate again and copy transforms into the saved archetype
     u32 writeIndex = 0;
     for(u32 a = 0; a < scene->archetypeCount; a++)
     {
          Archetype *arch = &scene->archetypes[a];
          if(!arch || !arch->arena) continue;

          for(u32 ai = 0; ai < arch->arenaCount; ai++)
          {
               EntityArena *arena = &arch->arena[ai];
               if(!arena || arena->count == 0) continue;

               // Ensure this archetype layout has at least three fields for transform
               if(arch->layout == NULL || arch->layout->count < 3)
               {
                    WARN("bakeScene: Archetype %s has insufficient fields for transform", (arch->layout)?arch->layout->name:"(null)");
                    continue;
               }

               void **fields = getArchetypeFields(arch, ai);
               if(!fields) continue;

               Vec3 *positions = (Vec3 *)fields[0];
               Vec4 *rotations = (Vec4 *)fields[1];
               Vec3 *scales = (Vec3 *)fields[2];

               for(u32 e = 0; e < arena->count; e++)
               {
                    if(writeIndex >= totalEntities) break; // safety
                    outPositions[writeIndex] = positions[e];
                    outRotations[writeIndex] = rotations[e];
                    outScales[writeIndex] = scales[e];
                    outArchIDs[writeIndex] = arch->id;
                    writeIndex++;
               }
          }
     }

     // Update saved archetype arena count to reflect actual written entities
     if(data.savedEntities.arena && data.savedEntities.arenaCount > 0)
     {
          data.savedEntities.arena[0].count = writeIndex;
     }

     data.entityCount = writeIndex;
     return data;
}
