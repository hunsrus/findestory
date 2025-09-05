#define RLIGHTS_IMPLEMENTATION      //Importante para que defina las funciones de rlights y eso
#define PLATFORM_DESKTOP

#include "tinyphysicsengine.h"
#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> // for measuring time

#include "rlights.h"
#include "rlgl.h"

#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB
    #define GLSL_VERSION            100
#endif

#define FPS 60

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
TPE_Body tpe_bodies[MAX_BODIES];
TPE_Joint tpe_joints[MAX_JOINTS];
TPE_Connection tpe_connections[MAX_CONNECTIONS];

TPE_Joint joints[WATER_JOINTS + 1];
TPE_Connection connections[WATER_CONNECTIONS];
TPE_Body bodies[2];

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

Shader initShader(void)
{
    // Load shader and set up some uniforms--------------------------------------------------------------
    Shader shader = LoadShader("../src/sha/basic_lighting.vs", 
                               "../src/sha/lighting.fs");
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

    int ambientLoc = GetShaderLocation(shader, "ambient");
    float aux[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    SetShaderValue(shader, ambientLoc, aux, SHADER_UNIFORM_VEC4);

    return shader;
}

int main(void)
{
    InitWindow(640,480,"findestory");

    TPE_worldInit(&tpe_world,tpe_bodies,0,0);

    Camera3D camera;
    camera.position = (Vector3){10.0f, 10.0f, 10.0f};
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // build the water body:

    for (int i = 0; i < HEIGHTMAP_3D_POINTS; ++i)
        joints[i] = TPE_joint(helper_heightmapPointLocation(i),JOINT_SIZE);

    int index = 0;

    for (int j = 0; j < HEIGHTMAP_3D_RESOLUTION; ++j)
    {
        for (int i = 0; i < HEIGHTMAP_3D_RESOLUTION - 1; ++i)
        {
        connections[index].joint1 = j * HEIGHTMAP_3D_RESOLUTION + i;
        connections[index].joint2 = connections[index].joint1 + 1;

        index++;

        connections[index].joint1 = i * HEIGHTMAP_3D_RESOLUTION + j;
        connections[index].joint2 = connections[index].joint1 + HEIGHTMAP_3D_RESOLUTION;

        index++;
        }
    }

    TPE_bodyInit(&bodies[0],joints,WATER_JOINTS,connections,WATER_CONNECTIONS,
        2 * TPE_F);

    bodies[0].flags |= TPE_BODY_FLAG_SOFT;
    bodies[0].flags |= TPE_BODY_FLAG_ALWAYS_ACTIVE;

    // create the ball body:
    joints[WATER_JOINTS] = TPE_joint(TPE_vec3(0,0,ROOM_SIZE / 4),BALL_SIZE);
    TPE_bodyInit(&bodies[1],joints + WATER_JOINTS,1,connections,0,200);

    bodies[1].flags |= TPE_BODY_FLAG_ALWAYS_ACTIVE;

    TPE_worldInit(&tpe_world,bodies,2,environmentDistance);

    int i = 0, j = 0, k = 0;
    int filas = HEIGHTMAP_3D_RESOLUTION;
    int columnas = HEIGHTMAP_3D_RESOLUTION;
    
    Mesh mesh = {0};
    
    mesh.vboId = (unsigned int *)RL_CALLOC(7, sizeof(unsigned int));
    
    // Triangles definition (indices)
    int numFaces = (filas - 1)*(columnas - 1);
    int *triangles = (int *)RL_MALLOC(numFaces*6*sizeof(int));
    int t = 0;
    for (int face = 0; face < numFaces; face++)
    {
        // Retrieve lower left corner from face ind
        int i = face % (filas - 1) + (face/(columnas - 1)*filas);

        triangles[t++] = i + filas;
        triangles[t++] = i + 1;
        triangles[t++] = i;

        triangles[t++] = i + filas;
        triangles[t++] = i + filas + 1;
        triangles[t++] = i + 1;
    }

    mesh.vertexCount = filas*columnas;
    mesh.triangleCount = numFaces*2;
    mesh.vertices = (float *)RL_MALLOC(mesh.vertexCount*3*sizeof(float));
    mesh.texcoords = (float *)RL_MALLOC(mesh.vertexCount*2*sizeof(float));
    mesh.normals = (float *)RL_MALLOC(mesh.vertexCount*3*sizeof(float));
    mesh.indices = (unsigned short *)RL_MALLOC(mesh.triangleCount*3*sizeof(unsigned short));
    k = 0;
    for(i = 0; i < filas; i++)
    {
        for(j = 0; j < columnas; j++)
        {
            mesh.vertices[k*3] = (float)joints[k].position.x*SCALE_3D;
            mesh.vertices[k*3 + 1] = (float)joints[k].position.y*SCALE_3D;
            mesh.vertices[k*3 + 2] = (float)joints[k].position.z*SCALE_3D;

            mesh.texcoords[2*k] = 0.0f;
            mesh.texcoords[2*k + 1] = 0.0f;
            
            mesh.normals[3*k] = 0.0f;
            mesh.normals[3*k + 1] = 1.0f;
            mesh.normals[3*k + 2] = 0.0f;
            k++;
        }
    }
    for (int i = 0; i < mesh.triangleCount*3; i++) mesh.indices[i] = triangles[i];
    
    UploadMesh(&mesh, true);

    SetTargetFPS(FPS);

    Shader shader = initShader();
    Matrix meshTransform = MatrixIdentity();
    Material meshMaterial = LoadMaterialDefault();
    meshMaterial.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
    meshMaterial.shader = shader;

    Light lights[MAX_LIGHTS] = { 0 };
    lights[0] = CreateLight(LIGHT_POINT, (Vector3){ 100, 100, 100 }, Vector3Zero(), WHITE, shader);
    lights[0].position = (Vector3){100.0f, 100.0f, 100.0f};

    while (!WindowShouldClose())
    {
        // helper_cameraFreeMovement();
        UpdateCamera(&camera, CAMERA_ORBITAL);

        SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], &camera.position.x, SHADER_UNIFORM_VEC3);
        UpdateLightValues(shader, lights[0]);
        // Actualiza shader de luz con la posicion de vista de la camara
        float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
        SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);

        // update the 3D model vertex positions:

        for (int i = 0; i < WATER_JOINTS; ++i)
        {
            mesh.vertices[i*3] = (float)joints[i].position.x*SCALE_3D;
            mesh.vertices[i*3 + 1] = (float)joints[i].position.y*SCALE_3D;
            mesh.vertices[i*3 + 2] = (float)joints[i].position.z*SCALE_3D;
        }
        
        UpdateMeshBuffer(mesh, 0, mesh.vertices, sizeof(float) * mesh.vertexCount * 3, 0);

        // pin the joints at the edges of the grid:
        for (int index = 0; index < WATER_JOINTS; ++index)
            if (index % HEIGHTMAP_3D_RESOLUTION == 0 || index % HEIGHTMAP_3D_RESOLUTION == HEIGHTMAP_3D_RESOLUTION - 1 ||
                index / HEIGHTMAP_3D_RESOLUTION == 0 || index / HEIGHTMAP_3D_RESOLUTION == HEIGHTMAP_3D_RESOLUTION - 1)
                TPE_jointPin(&joints[index],helper_heightmapPointLocation(index));

        TPE_worldStep(&tpe_world);

    #define G ((5 * 30) / FPS)
        TPE_bodyApplyGravity(&tpe_world.bodies[1],
            bodies[1].joints[0].position.y > 0 ? G : (-2 * G));

    #define ACC ((25 * 30) / FPS )
        if (IsKeyDown(KEY_W))
            TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,0,ACC));
        else if (IsKeyDown(KEY_S))
            TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,0,-1 * ACC));
        else if (IsKeyDown(KEY_D))
            TPE_bodyAccelerate(&bodies[1],TPE_vec3(ACC,0,0));
        else if (IsKeyDown(KEY_A))
            TPE_bodyAccelerate(&bodies[1],TPE_vec3(-1 * ACC,0,0));
        else if (IsKeyDown(KEY_C))
            TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,ACC,0));
        else if (IsKeyDown(KEY_X))
            TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,-1 * ACC,0));

        BeginDrawing();
            ClearBackground(GRAY);
            BeginMode3D(camera);
                Vector3 spherePos;
                spherePos.x = (float)bodies[1].joints[0].position.x*SCALE_3D; 
                spherePos.y = (float)bodies[1].joints[0].position.y*SCALE_3D;
                spherePos.z = (float)bodies[1].joints[0].position.z*SCALE_3D;
                DrawSphereEx(spherePos,BALL_SIZE*SCALE_3D,10, 10, RED);
                BeginShaderMode(shader);
                DrawMesh(mesh, meshMaterial, meshTransform);
                    // for (int i = 0; i < WATER_JOINTS; ++i)
                    // {
                    //     Vector3 jointPos = {mesh.vertices[i*3],mesh.vertices[i*3 + 1],mesh.vertices[i*3 + 2]};
                    //     DrawSphereEx(jointPos,JOINT_SIZE*SCALE_3D,10, 10, GREEN);
                    // }
                EndShaderMode();
            EndMode3D();
        EndDrawing();
    }

    CloseWindow();

    return 0;
}