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
#define MAP_WIDTH_CHUNKS 20
#define MAP_HEIGHT_CHUNKS 20
// #define CHUNK_SIZE 64
#define CHUNK_SIZE (int)(ROOM_SIZE*SCALE_3D)
// #define CHUNK_SIZE 4
#define VIEW_DISTANCE CHUNK_SIZE*40

// estructura para almacenar un chunk de terreno
typedef struct TerrainChunk {
    Mesh mesh;
    TPE_Unit **height;
    Vector3 position;
    Vector3 size;
    Matrix transform;
    Texture2D texture;
} TerrainChunk;

TerrainChunk **terrainChunks;

float mapear(float val, float valMin, float valMax, float outMin, float outMax)
{
    return (val - valMin)*(outMax-outMin)/(valMax-valMin) + outMin;
}

float distPuntos(float x1, float y1, float z1, float x2, float y2, float z2)
{
    return sqrtf((x1 - x2)*(x1 - x2) + (y1 - y2)*(y1 - y2) + (z1 - z2)*(z1 - z2));
}

void CalculateNormals(Mesh* mesh, Mesh* chunkMesh) {
    int vertexCount = mesh->vertexCount;
    int triangleCount = mesh->triangleCount;
    float *vertices = mesh->vertices;
    float *normals = mesh->normals;
    unsigned short *indices = mesh->indices;

    // Inicializar todas las normales a (0, 0, 0)
    // for (int i = 0; i < vertexCount * 3; i++) {
    //     normals[i] = 0.0f;
    // }
    int size = sqrt(mesh->vertexCount)-2;

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

    int nCounter = 0;
    int nCounterAux = 0;
    int auxSize = size+2;

    for (int z = 0; z < auxSize; z++) {
        for (int x = 0; x < auxSize; x++) {
            if(x >= 1 && x < auxSize-1 && z >= 1 && z < auxSize-1)
            {
                chunkMesh->normals[nCounter++] = normals[nCounterAux++];
                chunkMesh->normals[nCounter++] = normals[nCounterAux++];
                chunkMesh->normals[nCounter++] = normals[nCounterAux++];
            }else{
                nCounterAux+=3;
            }  
        }
    }
}

#define OFFSETCOLOR 10

// Función para generar un chunk de terreno
TerrainChunk GenerateTerrainChunk(int size, float scale, Vector3 position) {
    Mesh auxMesh;
    int auxSize = size+2;
    int auxVertexCount = auxSize * auxSize;
    int auxTriangleCount = (auxSize - 1) * (auxSize - 1) * 2;
    auxMesh = (Mesh){ 0 };
    auxMesh.vertexCount = auxVertexCount;
    auxMesh.triangleCount = auxTriangleCount;
    auxMesh.vertices = (float*)MemAlloc(auxVertexCount * 3 * sizeof(float));
    auxMesh.normals = (float*)MemAlloc(auxVertexCount * 3 * sizeof(float));
    auxMesh.texcoords = (float*)MemAlloc(auxVertexCount * 2 * sizeof(float));
    auxMesh.indices = (unsigned short*)MemAlloc(auxTriangleCount * 3 * sizeof(unsigned short));

    int vCounterAux = 0;
    int nCounterAux = 0;
    int tCounterAux = 0;
    int iCounterAux = 0;

    TerrainChunk chunk;
    chunk.position = position;
    chunk.size = (Vector3){ size * scale, scale, size * scale };
    chunk.height = MemAlloc(CHUNK_SIZE*sizeof(TPE_Unit *));
    for(int i = 0; i < CHUNK_SIZE; i++)
    {
        chunk.height[i] = MemAlloc(CHUNK_SIZE*sizeof(TPE_Unit));
    }

    int vertexCount = size * size;
    int triangleCount = (size - 1) * (size - 1) * 2;

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

    float paso = 0.08f;
    // unsigned int semilla = 180100;
    // unsigned int semilla = 118;
    unsigned int semilla = 1000;
    srand(semilla);

    int ladoMenor = (MAP_WIDTH_CHUNKS > MAP_HEIGHT_CHUNKS)? MAP_HEIGHT_CHUNKS:MAP_WIDTH_CHUNKS;
    // int cantPicos = ladoMenor/2;
    int cantPicos = 6;

    ladoMenor = ladoMenor*2*CHUNK_SIZE;
    float marginW = MAP_WIDTH_CHUNKS*2*CHUNK_SIZE/5.0f;
    float marginH = MAP_HEIGHT_CHUNKS*2*CHUNK_SIZE/5.0f;
    int maxHeight = 30;

    float picos[cantPicos][4]; //[0] = X; [1] = Y; [2] = Radio; [3] = Peso

    for(int i = 0; i < cantPicos; i++)
    {
        picos[i][3] = ((rand()%(int)(10-0+1)))/10.0f;
        picos[i][2] = ((rand()%(int)(ladoMenor/7-ladoMenor/20+1))+ladoMenor/20);
        picos[i][1] = ((rand()%(int)(MAP_WIDTH_CHUNKS*2*CHUNK_SIZE-(marginW+picos[i][2])*2+1))+marginW+picos[i][2]);
        picos[i][0] = ((rand()%(int)(MAP_HEIGHT_CHUNKS*2*CHUNK_SIZE-(marginH+picos[i][2])*2+1))+marginH+picos[i][2]);
    }

    Image textureImage = GenImageChecked(CHUNK_SIZE-1,CHUNK_SIZE-1,1,1,BLACK,MAGENTA);

    Color color[6];
    float rangoColor[3];
    int k;
    color[0] = (Color){245, 245, 86, 255};
    color[1] = (Color){173, 240, 65, 255};
    color[2] = (Color){93, 190, 35, 255};
    color[3] = (Color){201, 117, 61, 255};

    rangoColor[0] = mapear(0.3f,0.0f,1.0f,-6.0f,maxHeight/2.0f);
    rangoColor[1] = mapear(0.6f,0.0f,1.0f,-6.0f,maxHeight/2.0f);
    rangoColor[2] = mapear(0.9f,0.0f,1.0f,-6.0f,maxHeight/2.0f);

    for (int z = 0; z < auxSize; z++) {
        for (int x = 0; x < auxSize; x++) {
            float y = -6.0f;
            int xcoord = x-1;
            int zcoord = z-1;

            //float y = 0.0f;
            float globalZ = position.z+zcoord*scale+MAP_HEIGHT_CHUNKS*CHUNK_SIZE;
            float globalX = position.x+xcoord*scale+MAP_WIDTH_CHUNKS*CHUNK_SIZE;

            for(int k = 0; k < cantPicos; k++)
            {
                float dist = distPuntos(picos[k][0],picos[k][1],0,(float)globalX,(float)globalZ,0);
                float val = mapear(dist,0.0f,picos[k][2]*2.0f,(float)maxHeight*picos[k][3],0.0f);
                if((val > y))
                {
                    y = (val+y)/2.0f;
                }
            }
            // float y = pnoise2d((double)globalZ*paso,(double)globalX*paso,(double)2, 1, semilla);
            // y += 1.5f*pnoise2d((double)globalX*paso*2,(double)globalZ*paso*2,(double)2, 1, semilla)+
            //                             1.25f*pnoise2d((double)globalX*paso*4,(double)globalZ*paso*4,(double)2, 1, semilla)+
            //                             0.05f*pnoise2d((double)globalX*paso*8,(double)globalZ*paso*8,(double)2, 1, semilla);

            y +=  1.5f*pnoise2d((double)globalX*paso,(double)globalZ*paso,(double)2, 1, semilla)+
                                        1.0f*pnoise2d((double)globalX*paso*2,(double)globalZ*paso*2,(double)2, 1, semilla)+
                                        0.5f*pnoise2d((double)globalX*paso*4,(double)globalZ*paso*4,(double)2, 1, semilla)+
                                        0.25f*pnoise2d((double)globalX*paso*8,(double)globalZ*paso*8,(double)2, 1, semilla)+
                                        0.05f*pnoise2d((double)globalX*paso*16,(double)globalZ*paso*16,(double)2, 1, semilla);

            if(y <= rangoColor[0]) k = 0;
            else if(y <= rangoColor[1]) k = 1;
            else if(y <= rangoColor[2]) k = 2;
            else k = 3;


            // if(x >= 1 && x < auxSize-1 && z >= 1 && z < auxSize-1)
            if(xcoord >= 0 && xcoord < size && zcoord >= 0 && zcoord < size)
            {
                ImageDrawPixel(&textureImage, xcoord, zcoord, (Color){rand()%((color[k].r+OFFSETCOLOR)-(color[k].r-OFFSETCOLOR)+1)+(color[k].r-OFFSETCOLOR),rand()%((color[k].g+OFFSETCOLOR)-(color[k].g-OFFSETCOLOR)+1)+(color[k].g-OFFSETCOLOR),rand()%((color[k].b+OFFSETCOLOR)-(color[k].b-OFFSETCOLOR)+1)+(color[k].b-OFFSETCOLOR),255});
                // fprintf(stdout, "[DEBUG] nCounterAux: %d", nCounterAux);
                // Agregar vértices
                // [TODO] X e Y siempre son iguales, no hace falta recalcular para cada chunk. optimizar.
                chunk.mesh.vertices[vCounter++] = xcoord * scale;
                chunk.mesh.vertices[vCounter++] = y;
                chunk.mesh.vertices[vCounter++] = zcoord * scale;

                // Agregar normales
                chunk.mesh.normals[nCounter++] = 0.0f;
                chunk.mesh.normals[nCounter++] = 0.0f;
                chunk.mesh.normals[nCounter++] = 0.0f;

                // Agregar coordenadas de textura
                chunk.mesh.texcoords[tCounter++] = (float)xcoord / (size-1);
                chunk.mesh.texcoords[tCounter++] = (float)zcoord / (size-1);
                // Agregar índices para los triángulos
                if (x < auxSize-2 && z < auxSize-2) {
                    chunk.mesh.indices[iCounter++] = (zcoord * size) + xcoord;
                    chunk.mesh.indices[iCounter++] = ((zcoord + 1) * size) + xcoord;
                    chunk.mesh.indices[iCounter++] = (zcoord * size) + (xcoord + 1);

                    chunk.mesh.indices[iCounter++] = (zcoord * size) + (xcoord + 1);
                    chunk.mesh.indices[iCounter++] = ((zcoord + 1) * size) + xcoord;
                    chunk.mesh.indices[iCounter++] = ((zcoord + 1) * size) + (xcoord + 1);
                }

                // altura en unidades útiles para el motor de físicas
                chunk.height[xcoord][zcoord] = (TPE_Unit)(y/SCALE_3D);
            }

            auxMesh.vertices[vCounterAux++] = x * scale;
            auxMesh.vertices[vCounterAux++] = y;
            auxMesh.vertices[vCounterAux++] = z * scale;

            auxMesh.normals[nCounterAux++] = 0.0f;
            auxMesh.normals[nCounterAux++] = 0.0f;
            auxMesh.normals[nCounterAux++] = 0.0f;

            if (x < auxSize - 1 && z < auxSize - 1) {
                auxMesh.indices[iCounterAux++] = (z * auxSize) + x;
                auxMesh.indices[iCounterAux++] = ((z + 1) * auxSize) + x;
                auxMesh.indices[iCounterAux++] = (z * auxSize) + (x + 1);

                auxMesh.indices[iCounterAux++] = (z * auxSize) + (x + 1);
                auxMesh.indices[iCounterAux++] = ((z + 1) * auxSize) + x;
                auxMesh.indices[iCounterAux++] = ((z + 1) * auxSize) + (x + 1);
            }
        
        }
    }

    CalculateNormals(&auxMesh, &chunk.mesh);

    // Sube la malla a la GPU
    UploadMesh(&chunk.mesh, false);

    chunk.transform = MatrixTranslate(position.x, position.y, position.z);

    chunk.texture = LoadTextureFromImage(textureImage);

    return chunk;
}

void RenderTerrainChunk(TerrainChunk chunk, Material material, Shader shader) {
    //Matrix mat = MatrixTranslate(chunk.position.x, chunk.position.y, chunk.position.z);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = chunk.texture;
    DrawMesh(chunk.mesh, material, chunk.transform);
    //chunk.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    //DrawModel(chunk.model,chunk.position,1.0f,WHITE);
    //UnloadMaterial(material);
}

void UnloadTerrainChunk(TerrainChunk chunk) {
    UnloadTexture(chunk.texture);
    UnloadMesh(chunk.mesh);
}

float dist2Chunk(TerrainChunk chunk, Vector3 refPoint)
{
    return Vector3Length(Vector3Subtract(chunk.position,refPoint));
}

static bool TRIGGER_PRINTF = false;

static int ENV_X;
static int ENV_Y;
static int ENV_Z;
static int ENV_X_AUX;
static int ENV_Y_AUX;
static int ENV_Z_AUX;
static int ENV_X_CHU;
static int ENV_Z_CHU;
static int ENV_X_PER;
static int ENV_Y_PER;
static int ENV_Z_PER;

TPE_Unit storedHeight(int32_t x, int32_t y)
{
    int zaux = y+MAP_WIDTH_CHUNKS*(CHUNK_SIZE-1);
    int xaux = x+MAP_HEIGHT_CHUNKS*(CHUNK_SIZE-1);

    int xChunk = floor((float)x/(CHUNK_SIZE-1)*1.0f)+MAP_WIDTH_CHUNKS;
    int zChunk = floor((float)y/(CHUNK_SIZE-1)*1.0f)+MAP_HEIGHT_CHUNKS;

    int xcoord = (x+MAP_WIDTH_CHUNKS*(CHUNK_SIZE-1))%(CHUNK_SIZE-1);
    int zcoord = (y+MAP_HEIGHT_CHUNKS*(CHUNK_SIZE-1))%(CHUNK_SIZE-1);

    ENV_X = x;
    ENV_Y = 0;
    ENV_Z = y;
    ENV_X_CHU = xChunk;
    ENV_Z_CHU = zChunk;
    ENV_X_AUX = xcoord;
    ENV_Y_AUX = 0;
    ENV_Z_AUX = zcoord;

    if((xcoord < CHUNK_SIZE) && (zcoord < CHUNK_SIZE) && (xChunk < MAP_WIDTH_CHUNKS*2) && (zChunk < MAP_HEIGHT_CHUNKS*2) && (xcoord >= 0) && (zcoord >= 0) && (xChunk >= 0) && (zChunk >= 0))
    {
        ENV_Y = (TPE_Unit)terrainChunks[xChunk][zChunk].height[xcoord][zcoord];
        return (TPE_Unit)terrainChunks[xChunk][zChunk].height[xcoord][zcoord];
    } else
    {
        return 0;
    }
}

TPE_Unit height(int32_t x, int32_t y)
{
    float scale = 1.0f;
    
    int zcoord = y+MAP_HEIGHT_CHUNKS*CHUNK_SIZE;
    int xcoord = x+MAP_WIDTH_CHUNKS*CHUNK_SIZE;

    float paso = 0.1f;
    // unsigned int semilla = 180100;
    unsigned int semilla = 118;

    float height = 1.5f*pnoise2d((double)xcoord*paso*2,(double)zcoord*paso*2,(double)2, 1, semilla)+
                                1.25f*pnoise2d((double)xcoord*paso*4,(double)zcoord*paso*4,(double)2, 1, semilla)+
                                0.05f*pnoise2d((double)xcoord*paso*8,(double)zcoord*paso*8,(double)2, 1, semilla);

    ENV_X_PER = xcoord;
    ENV_Y_PER = (TPE_Unit)(height/SCALE_3D);
    ENV_Z_PER = zcoord;

    storedHeight(x,y);

    return (TPE_Unit)(height/SCALE_3D);
}


TPE_Vec3 heightmapEnvironmentDistance(TPE_Vec3 p, TPE_Unit maxD)
{
//   return TPE_envHeightmap(p,TPE_vec3(0,0,0),(TPE_Unit)(1/SCALE_3D),height,maxD);
  return TPE_envHeightmap(p,TPE_vec3(0,0,0),(TPE_Unit)(1/SCALE_3D),storedHeight,maxD);
}

// --------------- UTILIDADES ------------------

static int CAMERA_MODE = 0;

enum CAMERA_MODES {FIRST_PERSON = 0, STATIC, THIRD_PERSON, LAST_ELEMENT};

static bool ON_TERRAIN = false;
static bool ON_WATER = false;

typedef struct{
    TPE_Body *body;
    Vector3 position;
    Vector3 view;
    Vector3 target;
    Vector3 up;
    Vector3 velocity;
    TPE_Vec3 acceleration;
}Player;

void camaraOrientarMouse(int pantallaAncho, int pantallaAlto, Player *jugador)
{
    float sensibilidad = 0.01f, anguloMax = PI*0.8f, anguloMin = PI-PI*0.8f;
    static float anguloY;
    Vector2 mousePos = {GetMouseX(),GetMouseY()}, pantallaCentro = {pantallaAncho/2, pantallaAlto/2}, mouseDireccion;
    Vector3 eje;
    Quaternion quatVista = {jugador->view.x,jugador->view.y,jugador->view.z,0.0f}, giro = {0};
    
    if((mousePos.x == pantallaCentro.x) && (mousePos.y == pantallaCentro.y)) return;
    
    SetMousePosition(pantallaCentro.x,pantallaCentro.y);
    mouseDireccion.x = (pantallaCentro.x - mousePos.x)*sensibilidad;
    mouseDireccion.y = (pantallaCentro.y - mousePos.y)*sensibilidad;
    
    eje = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(jugador->target, jugador->position), jugador->up));
    //Orienta eje x
    giro = QuaternionFromAxisAngle(jugador->up, mouseDireccion.x);
    quatVista = QuaternionMultiply(QuaternionMultiply(giro,quatVista),QuaternionInvert(giro));
    jugador->view = (Vector3){quatVista.x, quatVista.y, quatVista.z};
    //Orienta eje y
    giro = QuaternionFromAxisAngle(eje, mouseDireccion.y);
    quatVista = QuaternionMultiply(QuaternionMultiply(giro,quatVista),QuaternionInvert(giro));
    anguloY = acos(Vector3DotProduct((Vector3){quatVista.x,quatVista.y,quatVista.z}, jugador->up)/Vector3Length((Vector3){quatVista.x,quatVista.y,quatVista.z})*Vector3Length(jugador->up));
    //Antes de asignar el giro en el eje y chequea los limites
    if((anguloY > anguloMax) || (anguloY < anguloMin)) return;
    jugador->view = (Vector3){quatVista.x, quatVista.y, quatVista.z};
}

// --------------- MAIN ------------------

int main(int argc, char *argv[])
{
    int WINDOW_WIDTH = 480;
    int WINDOW_HEIGHT = 480;
    InitWindow(WINDOW_WIDTH,WINDOW_HEIGHT,"findestory");
    SetTargetFPS(FPS);

    Shader shader = initShader();

    // Crear terreno en chunks
    const float scale = 1.0f;
    // [TODO] cambiar la forma de representar la cantidad de chunks simétrica porque es un asco
    terrainChunks = (TerrainChunk **)RL_MALLOC(MAP_WIDTH_CHUNKS*2*sizeof(TerrainChunk *));
    for(int i = 0; i < MAP_WIDTH_CHUNKS*2; i++)
    {
        terrainChunks[i] = (TerrainChunk *)RL_MALLOC(MAP_HEIGHT_CHUNKS*2*sizeof(TerrainChunk));
    }
    for (int z = -MAP_HEIGHT_CHUNKS; z < MAP_HEIGHT_CHUNKS; z++) {
        for (int x = -MAP_WIDTH_CHUNKS; x < MAP_WIDTH_CHUNKS; x++) {
            int xindex = x+MAP_WIDTH_CHUNKS;
            int zindex = z+MAP_HEIGHT_CHUNKS;
            Vector3 position = { x * (CHUNK_SIZE-1) * scale, 0, z * (CHUNK_SIZE-1) * scale};

            TerrainChunk chunk = GenerateTerrainChunk(CHUNK_SIZE, scale, position);
            
            terrainChunks[xindex][zindex] = chunk;
        }
    }

    // Texture2D terrainTexture = LoadTextureFromImage(LoadImage("src/img/favicon.png"));
    // Texture2D terrainTexture = LoadTextureFromImage(GenImageColor(CHUNK_SIZE,CHUNK_SIZE,BROWN));

    Material material = LoadMaterialDefault();
    // material.maps[MATERIAL_MAP_DIFFUSE].texture = terrainTexture;
    material.shader = shader;

    // TPE_worldInit(&tpe_world,tpe_bodies,0,heightmapEnvironmentDistance);

    camera.position = (Vector3){10.0f, 10.0f, 10.0f};
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 80.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Vector3 ortoTangente = Vector3Zero();
    Vector3 tangente = Vector3Zero();

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

    // create the player
    Player player = {0};
    // joints[WATER_JOINTS] = TPE_joint(TPE_vec3(0,ROOM_SIZE*0.6,ROOM_SIZE / 4),BALL_SIZE);
    // joints[WATER_JOINTS] = TPE_joint(TPE_vec3(0,8000,ROOM_SIZE / 4),BALL_SIZE);
    bodies[1].joints = (TPE_Joint*)MemAlloc(sizeof(TPE_joint));
    bodies[1].joints[0] = TPE_joint(TPE_vec3(0,8000,ROOM_SIZE / 4),BALL_SIZE);
    bodies[1].jointCount = 1;
    TPE_bodyInit(&bodies[1],bodies[1].joints,bodies[1].jointCount,connections,0,1);

    bodies[1].flags |= TPE_BODY_FLAG_ALWAYS_ACTIVE;
    bodies[1].flags |= TPE_BODY_FLAG_NONROTATING;
    // bodies[1].flags |= TPE_BODY_FLAG_SIMPLE_CONN;

    bodies[1].friction = 400;
    bodies[1].elasticity = 128;

    player.body = &bodies[1];
    player.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    player.view = (Vector3){ 1.0f, 1.0f, 1.0f };

    // create test body
    bodies[2].joints = (TPE_Joint*)MemAlloc(sizeof(TPE_joint));
    bodies[2].joints[0] = TPE_joint(TPE_vec3(100,8000,ROOM_SIZE / 4),BALL_SIZE);
    bodies[2].jointCount = 1;
    TPE_bodyInit(&bodies[2],bodies[2].joints,bodies[2].jointCount,connections,0,1);

    // update physics word
    TPE_worldInit(&tpe_world,bodies,3,heightmapEnvironmentDistance);
    
    // generate water mesh
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
        if(IsKeyPressed(KEY_T)) TRIGGER_PRINTF = true;

        if(IsKeyPressed(KEY_V))
        {
            if(CAMERA_MODE < LAST_ELEMENT) CAMERA_MODE++;
            else CAMERA_MODE = 0;
        }

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

        // UpdateCamera(&camera, CAMERA_ORBITAL);
        UpdateCamera(&camera, CAMERA_CUSTOM);

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

    #define G ((8 * 30) / FPS)
        TPE_bodyApplyGravity(&tpe_world.bodies[1],
            bodies[1].joints[0].position.y > 0 ? G : (-2 * G));
        TPE_bodyApplyGravity(&tpe_world.bodies[2],
            bodies[2].joints[0].position.y > 0 ? G : (-2 * G));
        
        player.acceleration = TPE_vec3(0,0,0);
        
        // fprintf(stdout, "[DEBUG] terrain height: \t%f - ", (float)(height(bodies[1].joints[0].position.x*SCALE_3D, bodies[1].joints[0].position.z*SCALE_3D)*SCALE_3D));
        // fprintf(stdout, "body Y position: \t%f\n", (float)(bodies[1].joints[0].position.y-BALL_SIZE)*SCALE_3D);
        // fprintf(stdout, "[DEBUG] vertical distance: \t%f\n", ((float)(bodies[1].joints[0].position.y-BALL_SIZE)*SCALE_3D)-(float)(height(bodies[1].joints[0].position.x*SCALE_3D, bodies[1].joints[0].position.z*SCALE_3D)*SCALE_3D));

        // fprintf(stdout, "[DEBUG] body position \tx:%d\ty%d\tz:%d\n", bodies[1].joints[0].position.x,bodies[1].joints[0].position.y,bodies[1].joints[0].position.z);
        // if(abs((bodies[1].joints[0].position.y) - (storedHeight(bodies[1].joints[0].position.x*SCALE_3D, bodies[1].joints[0].position.z*SCALE_3D))) < BALL_SIZE*2)
        // {
        //     ON_TERRAIN = true;
            
        // }else ON_TERRAIN = false;

        if(TPE_bodyEnvironmentCollideMOD(&bodies[1], tpe_world.environmentFunction) > 0)
            ON_TERRAIN = true;
        else ON_TERRAIN = false;

        if(bodies[1].joints[0].position.y < BALL_SIZE*2)
        {
            ON_WATER = true;
        }else ON_WATER = false;

        TPE_Unit acceleration;
        // if(ON_WATER) acceleration = ACC/2;
        // if(ON_TERRAIN) acceleration = ACC;
        if(IsKeyDown(KEY_LEFT_SHIFT))
        {
            if(ON_WATER) acceleration = 3;
            if(ON_TERRAIN) acceleration = 3;
        }else
        {
            if(ON_WATER) acceleration = 1;
            if(ON_TERRAIN) acceleration = 1;
        }


        if(ON_TERRAIN || ON_WATER)
        {
            if (IsKeyDown(KEY_W))
            // player.acceleration.z = -acceleration;
            player.acceleration = TPE_vec3Plus(player.acceleration,TPE_vec3TimesPlain((TPE_Vec3){tangente.x*5,tangente.y*5,tangente.z*5},acceleration));
            // TPE_bodyAccelerate(&bodies[1],TPE_vec3(0,0,ACC));
            if (IsKeyDown(KEY_S))
                player.acceleration = TPE_vec3Plus(player.acceleration,TPE_vec3TimesPlain((TPE_Vec3){tangente.x*10,tangente.y*10,tangente.z*10},-acceleration));
            if (IsKeyDown(KEY_D))
                player.acceleration = TPE_vec3Plus(player.acceleration,TPE_vec3TimesPlain((TPE_Vec3){ortoTangente.x*10,ortoTangente.y*10,ortoTangente.z*10},-acceleration));
            if (IsKeyDown(KEY_A))
                player.acceleration = TPE_vec3Plus(player.acceleration,TPE_vec3TimesPlain((TPE_Vec3){ortoTangente.x*10,ortoTangente.y*10,ortoTangente.z*10},acceleration));
            if (IsKeyDown(KEY_SPACE))
                player.acceleration.y = acceleration*80;
            if (IsKeyDown(KEY_LEFT_CONTROL))
                player.acceleration.y = -acceleration*20;
        }

        TPE_bodyAccelerate(player.body, player.acceleration);

        spherePos.x = (float)bodies[1].joints[0].position.x*SCALE_3D; 
        spherePos.y = (float)bodies[1].joints[0].position.y*SCALE_3D;
        spherePos.z = (float)bodies[1].joints[0].position.z*SCALE_3D;

        player.position = spherePos;

        Vector3 p0 = player.position;
        Vector3 p1 = player.position;
        Vector3 p2 = player.position;
        Vector3 p3 = player.position;
        Vector3 p4 = player.position;
        Vector3 p5 = player.position;
        Vector3 p6 = player.position;
        Vector3 p7 = player.position;
        Vector3 p8 = player.position;
        p0.y = (float)storedHeight((TPE_Unit)(player.position.x),(TPE_Unit)(player.position.z))*SCALE_3D;
        p1.x -= 1;
        p1.y = (float)storedHeight((TPE_Unit)p1.x,(TPE_Unit)p1.z)*SCALE_3D;
        p2.x += 1;
        p2.y = (float)storedHeight((TPE_Unit)p2.x,(TPE_Unit)p2.z)*SCALE_3D;
        p3.z -= 1;
        p3.y = (float)storedHeight((TPE_Unit)p3.x,(TPE_Unit)p3.z)*SCALE_3D;
        p4.x += 1;
        p4.y = (float)storedHeight((TPE_Unit)p4.x,(TPE_Unit)p4.z)*SCALE_3D;
        p5.x -= 1;
        p5.z -= 1;
        p5.y = (float)storedHeight((TPE_Unit)p5.x,(TPE_Unit)p5.z)*SCALE_3D;
        p6.x -= 1;
        p6.z += 1;
        p6.y = (float)storedHeight((TPE_Unit)p6.x,(TPE_Unit)p6.z)*SCALE_3D;
        p7.x += 1;
        p7.z += 1;
        p7.y = (float)storedHeight((TPE_Unit)p7.x,(TPE_Unit)p7.z)*SCALE_3D;
        p8.x += 1;
        p8.z -= 1;
        p8.y = (float)storedHeight((TPE_Unit)p8.x,(TPE_Unit)p8.z)*SCALE_3D;

        Vector3 v1 = Vector3Subtract(p1,p0);
        Vector3 v2 = Vector3Subtract(p2,p0);
        Vector3 v3 = Vector3Subtract(p3,p0);
        Vector3 v4 = Vector3Subtract(p4,p0);
        Vector3 v5 = Vector3Subtract(p5,p0);
        Vector3 v6 = Vector3Subtract(p6,p0);
        Vector3 v7 = Vector3Subtract(p7,p0);
        Vector3 v8 = Vector3Subtract(p8,p0);

        Vector3 cruz1 = Vector3CrossProduct(v1,v2);
        Vector3 cruz2 = Vector3CrossProduct(v2,v3);
        Vector3 cruz3 = Vector3CrossProduct(v3,v4);
        Vector3 cruz4 = Vector3CrossProduct(v4,v5);
        Vector3 cruz5 = Vector3CrossProduct(v5,v6);
        Vector3 cruz6 = Vector3CrossProduct(v6,v7);
        Vector3 cruz7 = Vector3CrossProduct(v7,v8);
        Vector3 cruz8 = Vector3CrossProduct(v8,v1);

        Vector3 hitNormal = Vector3Scale(Vector3Add(Vector3Add(Vector3Add(Vector3Add(Vector3Add(Vector3Add(Vector3Add(cruz1,cruz2),cruz3),cruz4),cruz5),cruz6),cruz7),cruz8),1.0f/8.0f);
        hitNormal = Vector3Normalize(hitNormal);

        if(CAMERA_MODE == FIRST_PERSON)
        {
            HideCursor();
            camaraOrientarMouse(WINDOW_WIDTH, WINDOW_HEIGHT, &player);

            ortoTangente = Vector3CrossProduct(hitNormal, player.view);
            tangente = Vector3CrossProduct(hitNormal, ortoTangente);
            tangente = Vector3Scale(tangente,Vector3DotProduct(player.view,tangente)/pow(Vector3Length(tangente),2));
            tangente = Vector3Normalize(tangente);
            ortoTangente = Vector3Normalize(ortoTangente);
            
            player.target = Vector3Add(player.view, player.position);
            
            camera.up = player.up;
            camera.target = player.target;
            camera.position = player.position;
        }else if(CAMERA_MODE == STATIC)
        {
            ShowCursor();
            ortoTangente = Vector3CrossProduct(hitNormal, player.view);
            tangente = Vector3CrossProduct(hitNormal, ortoTangente);
            tangente = Vector3Scale(tangente,Vector3DotProduct(player.view,tangente)/pow(Vector3Length(tangente),2));
            tangente = Vector3Normalize(tangente);
            ortoTangente = Vector3Normalize(ortoTangente);

            camera.position = (Vector3){MAP_WIDTH_CHUNKS*CHUNK_SIZE, MAP_WIDTH_CHUNKS*CHUNK_SIZE, MAP_HEIGHT_CHUNKS*CHUNK_SIZE};
            camera.target = (Vector3){ 0.0f, -MAP_WIDTH_CHUNKS*CHUNK_SIZE/2, 0.0f };
            camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
            camera.fovy = 50.0f;
            camera.projection = CAMERA_PERSPECTIVE;
        }else if(CAMERA_MODE == THIRD_PERSON)
        {
            ShowCursor();

            camera.position = (Vector3){0.0f, 200.0f, 0.0f};
            camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
            camera.up = (Vector3){ 1.0f, 0.0f, 0.0f };
            camera.fovy = 80.0f;
            camera.projection = CAMERA_PERSPECTIVE;
        }
        
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

        TRIGGER_PRINTF = false;

        BeginDrawing();
            ClearBackground(SKYBLUE);
            BeginMode3D(camera);
                DrawSphereEx(spherePos,BALL_SIZE*SCALE_3D,10, 10, RED);
                DrawSphereEx(Vector3Scale((Vector3){bodies[2].joints[0].position.x,bodies[2].joints[0].position.y,bodies[2].joints[0].position.z},SCALE_3D),BALL_SIZE*SCALE_3D,10, 10, ORANGE);
                BeginShaderMode(shader);
                    for (int zindex = 0; zindex < MAP_HEIGHT_CHUNKS*2; zindex++)
                    {
                        for (int xindex = 0; xindex < MAP_WIDTH_CHUNKS*2; xindex++)
                        {
                            if(dist2Chunk(terrainChunks[xindex][zindex], player.position) < VIEW_DISTANCE)
                                RenderTerrainChunk(terrainChunks[xindex][zindex], material, shader);
                        }
                    }
                    // dibujar agua
                    // DrawMesh(mesh, meshMaterial, meshTransform);
                    // for (int i = 0; i < WATER_JOINTS; ++i)
                    // {
                    //     Vector3 jointPos = {mesh.vertices[i*3],mesh.vertices[i*3 + 1],mesh.vertices[i*3 + 2]};
                    //     DrawSphereEx(jointPos,JOINT_SIZE*SCALE_3D,10, 10, GREEN);
                    // }
                EndShaderMode();
                DrawPlane(Vector3Zero(),(Vector2){MAP_WIDTH_CHUNKS*2*CHUNK_SIZE,MAP_HEIGHT_CHUNKS*2*CHUNK_SIZE},(Color){0,121,241,200});

                DrawLine3D(spherePos,Vector3Add(spherePos,Vector3Scale(hitNormal,2.0f)),ORANGE);
                DrawLine3D(spherePos,Vector3Add(spherePos,Vector3Scale(tangente,2.0f)),GREEN);

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
                if(player.position.y < 0)
                {
                    DrawTriangle3D((Vector3){-MAP_WIDTH_CHUNKS*CHUNK_SIZE,0.0f,-MAP_HEIGHT_CHUNKS*CHUNK_SIZE},(Vector3){MAP_WIDTH_CHUNKS*CHUNK_SIZE,0.0f,-MAP_HEIGHT_CHUNKS*CHUNK_SIZE},(Vector3){MAP_WIDTH_CHUNKS*CHUNK_SIZE,0.0f,MAP_HEIGHT_CHUNKS*CHUNK_SIZE},(Color){0,121,241,200});
                    DrawTriangle3D((Vector3){-MAP_WIDTH_CHUNKS*CHUNK_SIZE,0.0f,-MAP_HEIGHT_CHUNKS*CHUNK_SIZE},(Vector3){MAP_WIDTH_CHUNKS*CHUNK_SIZE,0.0f,MAP_HEIGHT_CHUNKS*CHUNK_SIZE},(Vector3){-MAP_WIDTH_CHUNKS*CHUNK_SIZE,0.0f,MAP_HEIGHT_CHUNKS*CHUNK_SIZE},(Color){0,121,241,200});
                }
            EndMode3D();

            if(player.position.y < 0)
            {
                DrawRectangle(0,0,WINDOW_WIDTH,WINDOW_HEIGHT,(Color){0,121,241,100});
            }

            DrawFPS(10,10);

            DrawText(TextFormat("POS: %.2f\t%.2f\t%.2f",spherePos.x,spherePos.y,spherePos.z), 10, 20+10, 20, RED);
            DrawText(TextFormat("PHY: %d\t%d\t%d",(TPE_Unit)spherePos.x,storedHeight((TPE_Unit)spherePos.x,(TPE_Unit)spherePos.z),(TPE_Unit)spherePos.z), 10, 20*2+10, 20, RED);
            DrawText(TextFormat("ENV: %d\t%d\t%d",ENV_X,ENV_Y,ENV_Z), 10, 20*3+10, 20, RED);
            DrawText(TextFormat("AUX: %d\t%d\t%d\tCHU: %d\t%d",ENV_X_AUX,ENV_Y_AUX,ENV_Z_AUX,ENV_X_CHU,ENV_Z_CHU), 10, 20*4+10, 20, RED);
            DrawText(TextFormat("PER: %d\t%d\t%d",ENV_X_PER,ENV_Y_PER,ENV_Z_PER), 10, 20*5+10, 20, RED);
            DrawText(TextFormat("TAN: %d\t%d\t%d",player.acceleration.x,player.acceleration.y,player.acceleration.z), 10, 20*6+10, 20, RED);
            DrawText(TextFormat("COL: %d",TPE_bodyEnvironmentCollideMOD(&bodies[1],tpe_world.environmentFunction)), 10, 20*7+10, 20, RED);
            // DrawText(TextFormat("COL: %d",bodies[1].joints[0].sizeDivided), 10, 20*7+10, 20, RED);
        EndDrawing();
    }

    CloseWindow();

    for (int z = 0; z < MAP_HEIGHT_CHUNKS*2; z++) {
        for (int x = 0; x < MAP_WIDTH_CHUNKS*2; x++) {
            UnloadTerrainChunk(terrainChunks[x][z]);
        }
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