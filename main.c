#define RLIGHTS_IMPLEMENTATION      // importante para que defina las funciones de rlights y eso
#define PLATFORM_DESKTOP

// #include "tinyphysicsengine.h"
#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> // for measuring time

#include <pthread.h>

// --------------- PORQUERIAS PARA WINDOWS ---------------

#if defined(_WIN32)
// To avoid conflicting windows.h symbols with raylib, some flags are defined
// WARNING: Those flags avoid inclusion of some Win32 headers that could be required
// by user at some point and won't be included...
//-------------------------------------------------------------------------------------

// If defined, the following flags inhibit definition of the indicated items.
#define NOGDICAPMASKS     // CC_*, LC_*, PC_*, CP_*, TC_*, RC_
#define NOVIRTUALKEYCODES // VK_*
#define NOWINMESSAGES     // WM_*, EM_*, LB_*, CB_*
#define NOWINSTYLES       // WS_*, CS_*, ES_*, LBS_*, SBS_*, CBS_*
#define NOSYSMETRICS      // SM_*
#define NOMENUS           // MF_*
#define NOICONS           // IDI_*
#define NOKEYSTATES       // MK_*
#define NOSYSCOMMANDS     // SC_*
#define NORASTEROPS       // Binary and Tertiary raster ops
#define NOSHOWWINDOW      // SW_*
#define OEMRESOURCE       // OEM Resource values
#define NOATOM            // Atom Manager routines
#define NOCLIPBOARD       // Clipboard routines
#define NOCOLOR           // Screen colors
#define NOCTLMGR          // Control and Dialog routines
#define NODRAWTEXT        // DrawText() and DT_*
#define NOGDI             // All GDI defines and routines
#define NOKERNEL          // All KERNEL defines and routines
#define NOUSER            // All USER defines and routines
//#define NONLS             // All NLS defines and routines
#define NOMB              // MB_* and MessageBox()
#define NOMEMMGR          // GMEM_*, LMEM_*, GHND, LHND, associated routines
#define NOMETAFILE        // typedef METAFILEPICT
#define NOMINMAX          // Macros min(a,b) and max(a,b)
#define NOMSG             // typedef MSG and associated routines
#define NOOPENFILE        // OpenFile(), OemToAnsi, AnsiToOem, and OF_*
#define NOSCROLL          // SB_* and scrolling routines
#define NOSERVICE         // All Service Controller routines, SERVICE_ equates, etc.
#define NOSOUND           // Sound driver routines
#define NOTEXTMETRIC      // typedef TEXTMETRIC and associated routines
#define NOWH              // SetWindowsHook and WH_*
#define NOWINOFFSETS      // GWL_*, GCL_*, associated routines
#define NOCOMM            // COMM driver routines
#define NOKANJI           // Kanji support stuff.
#define NOHELP            // Help engine interface.
#define NOPROFILER        // Profiler interface.
#define NODEFERWINDOWPOS  // DeferWindowPos routines
#define NOMCX             // Modem Configuration Extensions

// Type required before windows.h inclusion
typedef struct tagMSG *LPMSG;

#include <winsock2.h>
#include <windows.h>

// Type required by some unused function...
typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG  biWidth;
  LONG  biHeight;
  WORD  biPlanes;
  WORD  biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG  biXPelsPerMeter;
  LONG  biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

#include <objbase.h>
#include <mmreg.h>
#include <mmsystem.h>

// Some required types defined for MSVC/TinyC compiler
#if defined(_MSC_VER) || defined(__TINYC__)
    #include "propidl.h"
#endif
#endif

// --------------- SHADER RELATED ------------------
#include "rlights.h"
#include "rlgl.h"

#if defined(PLATFORM_DESKTOP)
    #define GLSL_VERSION            330
#else   // PLATFORM_RPI, PLATFORM_ANDROID, PLATFORM_WEB
    #define GLSL_VERSION            100
#endif

Camera3D camera;

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

// --------------- PHYSICS ------------------
#include "physics.h"

Vector3 spherePos;

// --------------- NETWORK ------------------
#include "network.h"

void DrawClient(ClientState *state, bool is_local)
{
    // int font_size = 20;
    // int text_width = MeasureText(text, font_size);
    Vector3 position = (Vector3){state->x*SCALE_3D, state->y*SCALE_3D, state->z*SCALE_3D};
    DrawSphereEx(position, BALL_SIZE*SCALE_3D, 10, 10, GREEN);

    if(is_local)
    {
        DrawCylinderEx(Vector3Add(position, (Vector3){0.0f,20.0f,0.0f}), Vector3Add(position, (Vector3){0.0f,15.0f,0.0f}), BALL_SIZE*SCALE_3D, BALL_SIZE*SCALE_3D, 10, GREEN);
    }
}

// --------------- WORLD GEN ------------------
#include "perlin.h"

#define FLT_MAX 340282346638528859811704183484516925440.0f
// cantidad de chunks de tamaño (en dos direcciones, es decir
// MAP_WIDTH_CHUNKS se aplica de forma simétrica, si es 4, son
// 4 chunks para el lado +x y 4 para el lado -x)
#define MAP_WIDTH_CHUNKS 2
#define MAP_HEIGHT_CHUNKS 2
// #define CHUNK_SIZE 64
// #define CHUNK_SIZE (int)(ROOM_SIZE*SCALE_3D)
#define CHUNK_SIZE 4
#define VIEW_DISTANCE CHUNK_SIZE*2

// estructura para almacenar un chunk de terreno
typedef struct TerrainChunk {
    Mesh mesh;
    Model model;
    BoundingBox boundingBox;
    Vector3 position;
    Vector3 size;
    Matrix transform;
} TerrainChunk;

float mapear(float val, float valMin, float valMax, float outMin, float outMax)
{
    return (val - valMin)*(outMax-outMin)/(valMax-valMin) + outMin;
}

void CalculateNormals(TerrainChunk *chunk) {
    int vertexCount = chunk->mesh.vertexCount;
    int triangleCount = chunk->mesh.triangleCount;
    float *vertices = chunk->mesh.vertices;
    float *normals = chunk->mesh.normals;
    unsigned short *indices = chunk->mesh.indices;

    // Inicializar todas las normales a (0, 0, 0)
    for (int i = 0; i < vertexCount * 3; i++) {
        normals[i] = 0.0f;
    }

    // Calcular la normal de cada triángulo y sumarla a las normales de sus vértices
    for (int i = 0; i < triangleCount * 3; i += 3) {
        int v1 = indices[i];
        int v2 = indices[i + 1];
        int v3 = indices[i + 2];

        float v1x = vertices[v1 * 3];
        float v1y = vertices[v1 * 3 + 1];
        float v1z = vertices[v1 * 3 + 2];

        float v2x = vertices[v2 * 3];
        float v2y = vertices[v2 * 3 + 1];
        float v2z = vertices[v2 * 3 + 2];

        float v3x = vertices[v3 * 3];
        float v3y = vertices[v3 * 3 + 1];
        float v3z = vertices[v3 * 3 + 2];

        float edge1x = v2x - v1x;
        float edge1y = v2y - v1y;
        float edge1z = v2z - v1z;

        float edge2x = v3x - v1x;
        float edge2y = v3y - v1y;
        float edge2z = v3z - v1z;

        float normalx = edge1y * edge2z - edge1z * edge2y;
        float normaly = edge1z * edge2x - edge1x * edge2z;
        float normalz = edge1x * edge2y - edge1y * edge2x;

        normals[v1 * 3] += normalx;
        normals[v1 * 3 + 1] += normaly;
        normals[v1 * 3 + 2] += normalz;

        normals[v2 * 3] += normalx;
        normals[v2 * 3 + 1] += normaly;
        normals[v2 * 3 + 2] += normalz;

        normals[v3 * 3] += normalx;
        normals[v3 * 3 + 1] += normaly;
        normals[v3 * 3 + 2] += normalz;
    }

    // Normalizar las normales
    for (int i = 0; i < vertexCount; i++) {
        float normalx = normals[i * 3];
        float normaly = normals[i * 3 + 1];
        float normalz = normals[i * 3 + 2];

        float length = sqrtf(normalx * normalx + normaly * normaly + normalz * normalz);

        normals[i * 3] = normalx / length;
        normals[i * 3 + 1] = normaly / length;
        normals[i * 3 + 2] = normalz / length;
    }
}

// Función para generar un chunk de terreno
TerrainChunk GenerateTerrainChunk(int width, int height, float scale, Vector3 position) {
    TerrainChunk chunk;
    chunk.position = position;
    chunk.size = (Vector3){ width * scale, scale, height * scale };

    int vertexCount = width * height;
    int triangleCount = (width - 1) * (height - 1) * 2;

    // Asignar memoria dinámicamente para los arrays
    chunk.mesh = (Mesh){ 0 };
    chunk.mesh.vertexCount = vertexCount;
    chunk.mesh.triangleCount = triangleCount;
    chunk.mesh.vertices = (float*)MemAlloc(vertexCount * 3 * sizeof(float)); // 3 floats por vértice (x, y, z)
    chunk.mesh.normals = (float*)MemAlloc(vertexCount * 3 * sizeof(float));  // 3 floats por normal (x, y, z)
    chunk.mesh.texcoords = (float*)MemAlloc(vertexCount * 2 * sizeof(float)); // 2 floats por texcoord (u, v)
    chunk.mesh.indices = (unsigned short*)MemAlloc(triangleCount * 3 * sizeof(unsigned short)); // 3 indices por triángulo

    int vCounter = 0;
    int nCounter = 0;
    int tCounter = 0;
    int iCounter = 0;

    float paso = 0.1f;
    // unsigned int semilla = 180100;
    unsigned int semilla = 118;

    for (int z = 0; z < height; z++) {
        for (int x = 0; x < width; x++) {
            //float y = 0.0f; // Puedes modificar esta altura para darle forma al terreno
            float globalZ = position.z+z*scale+MAP_HEIGHT_CHUNKS*CHUNK_SIZE;
            float globalX = position.x+x*scale+MAP_WIDTH_CHUNKS*CHUNK_SIZE;
            // float y = pnoise2d((double)globalZ*paso,(double)globalX*paso,(double)2, 1, semilla);
            float y = 1.5f*pnoise2d((double)globalZ*paso*2,(double)globalX*paso*2,(double)2, 1, semilla)+
                                        1.25f*pnoise2d((double)globalZ*paso*4,(double)globalX*paso*4,(double)2, 1, semilla)+
                                        0.05f*pnoise2d((double)globalZ*paso*8,(double)globalX*paso*8,(double)2, 1, semilla);

            // Agregar vértices
            chunk.mesh.vertices[vCounter++] = x * scale;
            chunk.mesh.vertices[vCounter++] = y;
            chunk.mesh.vertices[vCounter++] = z * scale;

            // Agregar normales
            chunk.mesh.normals[nCounter++] = 0.0f;
            chunk.mesh.normals[nCounter++] = 1.0f;
            chunk.mesh.normals[nCounter++] = 0.0f;

            // Agregar coordenadas de textura
            chunk.mesh.texcoords[tCounter++] = (float)x / width;
            chunk.mesh.texcoords[tCounter++] = (float)z / height;

            // Agregar índices para los triángulos
            if (x < width - 1 && z < height - 1) {
                chunk.mesh.indices[iCounter++] = (z * width) + x;
                chunk.mesh.indices[iCounter++] = ((z + 1) * width) + x;
                chunk.mesh.indices[iCounter++] = (z * width) + (x + 1);

                chunk.mesh.indices[iCounter++] = (z * width) + (x + 1);
                chunk.mesh.indices[iCounter++] = ((z + 1) * width) + x;
                chunk.mesh.indices[iCounter++] = ((z + 1) * width) + (x + 1);
            }
        }
    }

    CalculateNormals(&chunk);
    // CalculateNormals2(&chunk);
    // Sube la malla a la GPU
    UploadMesh(&chunk.mesh, false);

    chunk.model = LoadModelFromMesh(chunk.mesh);
    chunk.transform = MatrixTranslate(position.x, position.y, position.z);
    chunk.boundingBox = GetMeshBoundingBox(chunk.mesh);
    // hay que transformar la posición de la bounding box para detectar correctamente las colisiones
    chunk.boundingBox.min = Vector3Transform(chunk.boundingBox.min,chunk.transform);
    chunk.boundingBox.max = Vector3Transform(chunk.boundingBox.max,chunk.transform);

    return chunk;
}

void RenderTerrainChunk(TerrainChunk chunk, Texture2D texture, Shader shader) {
    Material material = LoadMaterialDefault();
    material.maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    material.shader = shader;

    //Matrix mat = MatrixTranslate(chunk.position.x, chunk.position.y, chunk.position.z);
    DrawMesh(chunk.mesh, material, chunk.transform);
    //chunk.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    //DrawModel(chunk.model,chunk.position,1.0f,WHITE);
    //UnloadMaterial(material);
}

void UnloadTerrainChunk(TerrainChunk chunk) {
    UnloadMesh(chunk.mesh);
}

float dist2Chunk(TerrainChunk chunk, Vector3 refPoint)
{
    return Vector3Length(Vector3Subtract(chunk.position,refPoint));
}

// --------------- MAIN ------------------

int main(int argc, char *argv[])
{
    InitWindow(480,480,"findestory");
    SetTargetFPS(FPS);

    // Crear terreno en chunks
    const float scale = 0.5f;
    unsigned int TOTAL_CHUNKS = MAP_HEIGHT_CHUNKS*2*MAP_WIDTH_CHUNKS*2;
    unsigned int chunkID = 0;
    TerrainChunk *terrainChunks = (TerrainChunk *)RL_MALLOC(TOTAL_CHUNKS*sizeof(TerrainChunk));
    for (int z = -MAP_HEIGHT_CHUNKS; z < MAP_HEIGHT_CHUNKS; z++) {
        for (int x = -MAP_WIDTH_CHUNKS; x < MAP_WIDTH_CHUNKS; x++) {
            Vector3 position = { x * (CHUNK_SIZE-1) * scale, 0, z * (CHUNK_SIZE-1) * scale};

            TerrainChunk chunk = GenerateTerrainChunk(CHUNK_SIZE, CHUNK_SIZE, scale, position);
            
            terrainChunks[chunkID] = chunk;
            chunkID++;
        }
    }

    // Texture2D terrainTexture = LoadTextureFromImage(LoadImage("src/img/favicon.png"));
    Texture2D terrainTexture = LoadTextureFromImage(GenImageColor(CHUNK_SIZE,CHUNK_SIZE,BROWN));

    TPE_worldInit(&tpe_world,tpe_bodies,0,0);

    camera.position = (Vector3){10.0f, 10.0f, 10.0f};
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    TPE_Vec3 acceleration = { 0 };

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

    Shader shader = initShader();
    Matrix meshTransform = MatrixIdentity();
    Material meshMaterial = LoadMaterialDefault();
    // meshMaterial.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
    meshMaterial.maps[MATERIAL_MAP_DIFFUSE].color = (Color){0,90,190,100};
    meshMaterial.shader = shader;

    Light lights[MAX_LIGHTS] = { 0 };
    lights[0] = CreateLight(LIGHT_POINT, (Vector3){ 100, 100, 100 }, Vector3Zero(), WHITE, shader);
    lights[0].position = (Vector3){100.0f, 100.0f, 100.0f};

    pthread_t server_thread;
    int ev;

    while (!WindowShouldClose())
    {
        if(IsKeyPressed(KEY_P) && !SERVER_STARTED && !CLIENT_STARTED)
        {
            if(!UDP_DRIVER_REGISTERED)
            {
                #ifdef __EMSCRIPTEN__
                    NBN_WebRTC_Register(); // Register the WebRTC driver
                #else
                    NBN_UDP_Register(); // Register the UDP driver
                #endif // __EMSCRIPTEN__
                UDP_DRIVER_REGISTERED = true;
            }
            // init server thread
            pthread_create(&server_thread, NULL, (void*)(serverHandler), NULL);
        }

        if(IsKeyPressed(KEY_O) && !SERVER_STARTED && !CLIENT_STARTED)
        {
            // TODO
            // revisar, esto podría ejecutarse justo mientras se está iniciando el server y se pisarían
            if(!UDP_DRIVER_REGISTERED)
            {
                #ifdef __EMSCRIPTEN__
                    NBN_WebRTC_Register(); // Register the WebRTC driver
                #else
                    NBN_UDP_Register(); // Register the UDP driver
                #endif // __EMSCRIPTEN__
                UDP_DRIVER_REGISTERED = true;
            }
            // Initialize the client with a protocol name (must be the same than the one used by the server), the server ip address and port

            // Start the client with a protocol name (must be the same than the one used by the server)
            // the server host and port
            if (NBN_GameClient_StartEx(PROTOCOL_NAME, "127.0.0.1", PORT, NULL, 0) < 0)
            {
                TraceLog(LOG_WARNING, "Game client failed to start. Exit");

                return 1;
            }

            CLIENT_STARTED = true;

            // Register messages, have to be done after NBN_GameClient_StartEx
            // Messages need to be registered on both client and server side
            NBN_GameClient_RegisterMessage(
                UPDATE_STATE_MESSAGE,
                (NBN_MessageBuilder)UpdateStateMessage_Create,
                (NBN_MessageDestructor)UpdateStateMessage_Destroy,
                (NBN_MessageSerializer)UpdateStateMessage_Serialize);
            NBN_GameClient_RegisterMessage(
                GAME_STATE_MESSAGE,
                (NBN_MessageBuilder)GameStateMessage_Create,
                (NBN_MessageDestructor)GameStateMessage_Destroy,
                (NBN_MessageSerializer)GameStateMessage_Serialize);
        }

        if(CLIENT_STARTED)
        {
            while ((ev = NBN_GameClient_Poll()) != NBN_NO_EVENT)
            {
                if (ev < 0)
                {
                    TraceLog(LOG_WARNING, "An occured while polling network events. Exit");

                    break;
                }

                HandleGameClientEvent(ev);
            }
        }

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

        // Recorremos cada triángulo
        for (int t = 0; t < mesh.triangleCount; t++) {
            // int i0 = triangles[t*3 + 0];
            // int i1 = triangles[t*3 + 1];
            // int i2 = triangles[t*3 + 2];
            int i0 = mesh.indices[t*3];
            int i1 = mesh.indices[t*3+1];
            int i2 = mesh.indices[t*3+2];

            // Obtener posiciones de los 3 vértices
            Vector3 v0 = {
                mesh.vertices[i0*3 + 0],
                mesh.vertices[i0*3 + 1],
                mesh.vertices[i0*3 + 2]
            };
            Vector3 v1 = {
                mesh.vertices[i1*3 + 0],
                mesh.vertices[i1*3 + 1],
                mesh.vertices[i1*3 + 2]
            };
            Vector3 v2 = {
                mesh.vertices[i2*3 + 0],
                mesh.vertices[i2*3 + 1],
                mesh.vertices[i2*3 + 2]
            };

            // Calcular dos aristas
            Vector3 e1 = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
            Vector3 e2 = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };

            // Normal de la cara (producto vectorial)
            Vector3 n = {
                e1.y*e2.z - e1.z*e2.y,
                e1.z*e2.x - e1.x*e2.z,
                e1.x*e2.y - e1.y*e2.x
            };

            // Acumular la normal en cada vértice
            mesh.normals[i0*3 + 0] += n.x;
            mesh.normals[i0*3 + 1] += n.y;
            mesh.normals[i0*3 + 2] += n.z;

            mesh.normals[i1*3 + 0] += n.x;
            mesh.normals[i1*3 + 1] += n.y;
            mesh.normals[i1*3 + 2] += n.z;

            mesh.normals[i2*3 + 0] += n.x;
            mesh.normals[i2*3 + 1] += n.y;
            mesh.normals[i2*3 + 2] += n.z;
        }

        // Normalizar cada normal de vértice
        for (int i = 0; i < mesh.vertexCount; i++) {
            float x = mesh.normals[i*3 + 0];
            float y = mesh.normals[i*3 + 1];
            float z = mesh.normals[i*3 + 2];
            float len = sqrtf(x*x + y*y + z*z);
            if (len > 0.0f) {
                mesh.normals[i*3 + 0] = x / len;
                mesh.normals[i*3 + 1] = y / len;
                mesh.normals[i*3 + 2] = z / len;
            }
        }
        
        UpdateMeshBuffer(mesh, 0, mesh.vertices, sizeof(float) * mesh.vertexCount * 3, 0);
        UpdateMeshBuffer(mesh, 2, mesh.normals, sizeof(float) * mesh.vertexCount * 3, 0);

        // pin the joints at the edges of the grid:
        for (int index = 0; index < WATER_JOINTS; ++index)
            if (index % HEIGHTMAP_3D_RESOLUTION == 0 || index % HEIGHTMAP_3D_RESOLUTION == HEIGHTMAP_3D_RESOLUTION - 1 ||
                index / HEIGHTMAP_3D_RESOLUTION == 0 || index / HEIGHTMAP_3D_RESOLUTION == HEIGHTMAP_3D_RESOLUTION - 1)
                TPE_jointPin(&joints[index],helper_heightmapPointLocation(index));

        TPE_worldStep(&tpe_world);

    #define G ((5 * 30) / FPS)
        TPE_bodyApplyGravity(&tpe_world.bodies[1],
            bodies[1].joints[0].position.y > 0 ? G : (-2 * G));
        
        acceleration = TPE_vec3(0,0,0);
        if (IsKeyDown(KEY_W))
            acceleration.z = ACC;
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,0,ACC));
        else if (IsKeyDown(KEY_S))
            acceleration.z = -ACC;
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,0,-1 * ACC));
        else if (IsKeyDown(KEY_D))
            acceleration.x = ACC;
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(ACC,0,0));
        else if (IsKeyDown(KEY_A))
            acceleration.x = -ACC;
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(-1 * ACC,0,0));
        else if (IsKeyDown(KEY_C))
            acceleration.y = ACC;
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,ACC,0));
        else if (IsKeyDown(KEY_X))
            acceleration.y = -ACC;
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,-1 * ACC,0));

        TPE_bodyAccelerate(&bodies[1], acceleration);

        spherePos.x = (float)bodies[1].joints[0].position.x*SCALE_3D; 
        spherePos.y = (float)bodies[1].joints[0].position.y*SCALE_3D;
        spherePos.z = (float)bodies[1].joints[0].position.z*SCALE_3D;
        
        if(CLIENT_STARTED)
        {
            if (connected && !disconnected)
            {
                if(!spawned) break;
                
                local_client_state.x = (float)bodies[1].joints[0].position.x; 
                local_client_state.y = (float)bodies[1].joints[0].position.y;
                local_client_state.z = (float)bodies[1].joints[0].position.z;
                local_client_state.velocity[0] = (float)bodies[1].joints[0].velocity[0];
                local_client_state.velocity[1] = (float)bodies[1].joints[0].velocity[1];
                local_client_state.velocity[2] = (float)bodies[1].joints[0].velocity[2];

                for (int i = 0; i < MAX_CLIENTS - 1; i++)
                {
                    if(other_clients[i])
                    {
                        if(other_clients[i]->client_id != local_client_state.client_id)
                        {   
                            bodies[2+i].joints[0].velocity[0] = other_clients[i]->velocity[0];
                            bodies[2+i].joints[0].velocity[1] = other_clients[i]->velocity[1];
                            bodies[2+i].joints[0].velocity[2] = other_clients[i]->velocity[2];           
                        }
                    }
                }

                // Send the latest local client state to the server
                if (SendPositionUpdate() < 0)
                {
                    TraceLog(LOG_WARNING, "Failed to send client state update");

                    return -1;
                }
            }

            if (!disconnected)
            {
                if (NBN_GameClient_SendPackets() < 0)
                {
                    TraceLog(LOG_ERROR, "An occured while flushing the send queue. Exit");

                    break;
                }
            }
        }

        BeginDrawing();
            ClearBackground(LIGHTGRAY);
            BeginMode3D(camera);
                DrawSphereEx(spherePos,BALL_SIZE*SCALE_3D,10, 10, RED);
                BeginShaderMode(shader);
                    for(chunkID = 0; chunkID < TOTAL_CHUNKS; chunkID++)
                    {
                        // if(dist2Chunk(terrainChunks[chunkID], jugador.pos) < VIEW_DISTANCE)
                            RenderTerrainChunk(terrainChunks[chunkID], terrainTexture, shader);
                    }
                    DrawMesh(mesh, meshMaterial, meshTransform);
                    // for (int i = 0; i < WATER_JOINTS; ++i)
                    // {
                    //     Vector3 jointPos = {mesh.vertices[i*3],mesh.vertices[i*3 + 1],mesh.vertices[i*3 + 2]};
                    //     DrawSphereEx(jointPos,JOINT_SIZE*SCALE_3D,10, 10, GREEN);
                    // }
                EndShaderMode();
                if(CLIENT_STARTED)
                {
                    if (disconnected)
                    {
                        if (server_close_code == -1)
                        {
                            if (connected)
                                DrawText("Connection to the server was lost", 265, 280, 20, RED);
                            else
                                DrawText("Server cannot be reached", 265, 280, 20, RED);
                        }
                        else if (server_close_code == SERVER_FULL_CODE)
                        {
                            DrawText("Cannot connect, server is full", 265, 280, 20, RED);
                        }
                    }
                    else if (connected && spawned)
                    {
                        // Start by drawing the remote clients
                        for (int i = 0; i < MAX_CLIENTS - 1; i++)
                        {
                            if(other_clients[i])
                            {
                                if(other_clients[i]->client_id != local_client_state.client_id)
                                {   
                                    DrawClient(other_clients[i], false);
                                    // fprintf(stdout, "id: %d, local id: %d\n", other_clients[i]->client_id, local_client_state.client_id);
                                }
                            }
                        }

                        // Then draw the local client
                        DrawClient(&local_client_state, true);
                    }
                }
            EndMode3D();
            DrawFPS(10,10);
        EndDrawing();
    }

    CloseWindow();

    UnloadTexture(terrainTexture);
    for(chunkID = 0; chunkID < TOTAL_CHUNKS; chunkID++)
    {
        UnloadTerrainChunk(terrainChunks[chunkID]);
    }

    if(CLIENT_STARTED)
    {
        NBN_GameClient_Stop();
        fprintf(stderr, "[CLIENT] Connection closed\n");
    }

    if(SERVER_STARTED)
    {
        running = false;
        // Wait for thread to finish
        pthread_join(server_thread, NULL);
        fprintf(stderr, "[SERVER] Server has stopped\n");
    }

    return 0;
}