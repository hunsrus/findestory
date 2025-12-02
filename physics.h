#include "tinyphysicsengine.h"

#define FPS 60

#define ACC ((25 * 30) / FPS )

#define JOINT_SIZE (TPE_F / 4)
#define BALL_SIZE (5 * TPE_F / 4)

#define HEIGHTMAP_3D_RESOLUTION 8
#define HEIGHTMAP_3D_STEP (TPE_F * 2)

#define ROOM_SIZE (HEIGHTMAP_3D_RESOLUTION * HEIGHTMAP_3D_STEP + JOINT_SIZE)

#define HEIGHTMAP_3D_POINTS (HEIGHTMAP_3D_RESOLUTION * HEIGHTMAP_3D_RESOLUTION)
#define WATER_JOINTS (HEIGHTMAP_3D_RESOLUTION * HEIGHTMAP_3D_RESOLUTION)
#define WATER_CONNECTIONS (2 * ((HEIGHTMAP_3D_RESOLUTION - 1) * HEIGHTMAP_3D_RESOLUTION))

#define SCALE_3D 0.001f

#define MAX_BODIES 128
#define MAX_JOINTS 1024
#define MAX_CONNECTIONS 2048

TPE_World tpe_world;

TPE_Joint joints[WATER_JOINTS + MAX_BODIES - 1];
TPE_Connection connections[WATER_CONNECTIONS];
TPE_Body bodies[MAX_BODIES];

TPE_Vec3 helper_heightmapPointLocation(int index)
{
    return TPE_vec3(
        (-1 * HEIGHTMAP_3D_RESOLUTION * HEIGHTMAP_3D_STEP) / 2 + (index % HEIGHTMAP_3D_RESOLUTION) * HEIGHTMAP_3D_STEP + HEIGHTMAP_3D_STEP / 2,0,
        (-1 * HEIGHTMAP_3D_RESOLUTION * HEIGHTMAP_3D_STEP) / 2 + (index / HEIGHTMAP_3D_RESOLUTION) * HEIGHTMAP_3D_STEP + HEIGHTMAP_3D_STEP / 2);
}

TPE_Vec3 environmentDistance(TPE_Vec3 p, TPE_Unit maxD)
{
    return TPE_envAABoxInside(p,TPE_vec3(0,0,0),TPE_vec3(ROOM_SIZE,ROOM_SIZE,ROOM_SIZE));
}

uint8_t TPE_bodyEnvironmentCollideMOD(const TPE_Body *body,
  TPE_ClosestPointFunction env)
{
  for (uint16_t i = 0; i < body->jointCount; ++i)
  {
    const TPE_Joint *joint = body->joints + i;

    TPE_Unit size = TPE_JOINT_SIZE(*joint);

    if (TPE_DISTANCE(joint->position,env(joint->position,size)) <= size*1.05)
      return 1;
  }

  return 0;
}