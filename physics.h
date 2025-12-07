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

// TPE_Joint *joints;
// TPE_Connection *connections;
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

    if (TPE_DISTANCE(joint->position,env(joint->position,size)) <= size*1.015)
      return 1;
  }

  return 0;
}

TPE_Body* generateHumanMainBody(TPE_Vec3 initPos)
{
  // TPE_Vec3 initPos = TPE_vec3(0,8000,ROOM_SIZE / 4);

  uint16_t bodyCount = tpe_world.bodyCount;

  uint32_t jointCount = 3;
  uint32_t connectionCount = 2;
  TPE_Joint *joints = (TPE_Joint*)MemAlloc(sizeof(TPE_Joint)*jointCount);
  TPE_Connection *connections = (TPE_Connection*)MemAlloc(sizeof(TPE_Connection)*connectionCount);

  TPE_Body *body = &bodies[bodyCount];

  // base 0
  joints[0] = TPE_joint(initPos,BALL_SIZE);
  // torso 1
  initPos.y += BALL_SIZE*2*0.8;
  joints[1] = TPE_joint(initPos,BALL_SIZE*0.8);
  // cabeza 2
  initPos.y += BALL_SIZE*1.5;
  joints[2] = TPE_joint(initPos,BALL_SIZE*0.5);
  
  // connection base-chest
  connections[0].joint1 = 0;
  connections[0].joint2 = 1;
  connections[0].length = BALL_SIZE;
  // connection chest-head
  connections[1].joint1 = 1;
  connections[1].joint2 = 2;
  connections[1].length = BALL_SIZE*1.5;

  TPE_bodyInit(body,joints,jointCount,connections,connectionCount,10000);

  body->flags |= TPE_BODY_FLAG_ALWAYS_ACTIVE;
  body->flags |= TPE_BODY_FLAG_NONROTATING;
  // body->flags |= TPE_BODY_FLAG_SIMPLE_CONN;
  // body->flags |= TPE_BODY_FLAG_SOFT;

  body->friction = 100;
  body->elasticity = 128;

  tpe_world.bodyCount++;

  return body;
}

TPE_Body* generateHumanArm(void)
{
  TPE_Vec3 initPos = TPE_vec3(0,0,0);
  uint16_t bodyCount = tpe_world.bodyCount;

  uint32_t jointCount = 2;
  uint32_t connectionCount = 1;
  TPE_Joint *joints = (TPE_Joint*)MemAlloc(sizeof(TPE_Joint)*jointCount);
  TPE_Connection *connections = (TPE_Connection*)MemAlloc(sizeof(TPE_Connection)*connectionCount);

  TPE_Body *body = &bodies[bodyCount];

  // hombro 0
  initPos.y += BALL_SIZE*2;
  // initPos.x -= BALL_SIZE*2;
  joints[0] = TPE_joint(initPos,BALL_SIZE/3);
  // mano 1
  initPos.y -= BALL_SIZE;
  // initPos.x -= BALL_SIZE;
  joints[1] = TPE_joint(initPos,BALL_SIZE/3);

  // connection left shoulder-arm
  connections[0].joint1 = 0;
  connections[0].joint2 = 1;
  connections[0].length = BALL_SIZE;

  TPE_bodyInit(body,joints,jointCount,connections,connectionCount,1);
  
  body->flags |= TPE_BODY_FLAG_ALWAYS_ACTIVE;

  tpe_world.bodyCount++;

  return body;
}