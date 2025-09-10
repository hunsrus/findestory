#define RLIGHTS_IMPLEMENTATION      //Importante para que defina las funciones de rlights y eso
#define PLATFORM_DESKTOP
#define SERVER_MODE
#define CLIENT_MODE

#include "tinyphysicsengine.h"
#include <raylib.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> // for measuring time

#include <pthread.h>

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

// --------------- PHYSICS RELATED ------------------

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
Vector3 spherePos;

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

// --------------- NETWORK RELATED ------------------
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <getopt.h>

// nbnet implementation
#define NBNET_IMPL

#define PROTOCOL_NAME "raylib-example"
#define PORT 42042

// nbnet logging, use raylib logging

#define NBN_LogInfo(...) TraceLog(LOG_INFO, __VA_ARGS__)
#define NBN_LogError(...) TraceLog(LOG_ERROR, __VA_ARGS__)
#define NBN_LogWarning(...) TraceLog(LOG_WARNING, __VA_ARGS__)
#define NBN_LogDebug(...) TraceLog(LOG_DEBUG, __VA_ARGS__)
#define NBN_LogTrace(...) TraceLog(LOG_TRACE, __VA_ARGS__)

#include "nbnet.h"

#ifdef __EMSCRIPTEN__
#include "net_drivers/webrtc.h"
#else
#include "net_drivers/udp.h"

#ifdef SOAK_WEBRTC_C_DRIVER
#include "net_drivers/webrtc_c.h"
#endif

#endif // __EMSCRIPTEN__

// Maximum number of connected clients at a time
#define MAX_CLIENTS 4

// A code passed by the server when closing a client connection due to being full (max client count reached)
#define SERVER_FULL_CODE 42

// Message ids
enum
{
    UPDATE_STATE_MESSAGE,
    GAME_STATE_MESSAGE
};

// Messages

typedef struct
{
    TPE_Unit x;
    TPE_Unit y;
    TPE_Unit z;
} UpdateStateMessage;

// Client state, represents a client over the network
typedef struct
{
    uint32_t client_id;
    TPE_Unit x;
    TPE_Unit y;
    TPE_Unit z;
} ClientState;

typedef struct
{
    unsigned int client_count;
    ClientState client_states[MAX_CLIENTS];
} GameStateMessage;

UpdateStateMessage *UpdateStateMessage_Create(void)
{
    return malloc(sizeof(UpdateStateMessage));
}

void UpdateStateMessage_Destroy(UpdateStateMessage *msg)
{
    free(msg);
}

int UpdateStateMessage_Serialize(UpdateStateMessage *msg, NBN_Stream *stream)
{
    NBN_SerializeInt(stream, msg->x, -ROOM_SIZE/2, ROOM_SIZE/2);
    NBN_SerializeInt(stream, msg->y, -ROOM_SIZE/2, ROOM_SIZE/2);
    NBN_SerializeInt(stream, msg->z, -ROOM_SIZE/2, ROOM_SIZE/2);

    return 0;
}

GameStateMessage *GameStateMessage_Create(void)
{
    return malloc(sizeof(GameStateMessage));
}

void GameStateMessage_Destroy(GameStateMessage *msg)
{
    free(msg);
}

int GameStateMessage_Serialize(GameStateMessage *msg, NBN_Stream *stream)
{
    NBN_SerializeUInt(stream, msg->client_count, 0, MAX_CLIENTS);

    for (unsigned int i = 0; i < msg->client_count; i++)
    {
        NBN_SerializeUInt(stream, msg->client_states[i].client_id, 0, UINT_MAX);
        NBN_SerializeInt(stream, msg->client_states[i].x, -ROOM_SIZE/2, ROOM_SIZE/2);
        NBN_SerializeInt(stream, msg->client_states[i].y, -ROOM_SIZE/2, ROOM_SIZE/2);
        NBN_SerializeInt(stream, msg->client_states[i].z, -ROOM_SIZE/2, ROOM_SIZE/2);
    }

    return 0;
}

// server specific

#ifdef SERVER_MODE

// For Sleep function
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h> 
#elif defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <windows.h>
#include <synchapi.h> 
#else
#include <time.h>
#endif

// A simple structure to represent connected clients
typedef struct
{
    // Underlying nbnet connection handle, used to send messages to that particular client
    NBN_ConnectionHandle client_handle;

    // Client state
    ClientState state;
} Client;

// Array of connected clients, NULL means that the slot is free (i.e no clients)
static Client *clients[MAX_CLIENTS] = {NULL};

// Number of currently connected clients
static unsigned int client_count = 0;

// Spawn positions
static Vector3 spawns[] = {
    (Vector3){0, 0, 0},
    (Vector3){0, 0, 0},
    (Vector3){0, 0, 0},
    (Vector3){0, 0, 0}
};

static void AcceptConnection(TPE_Unit x, TPE_Unit y, TPE_Unit z, NBN_ConnectionHandle conn)
{
    NBN_WriteStream ws;
    uint8_t data[32];

    NBN_WriteStream_Init(&ws, data, sizeof(data));

    NBN_SerializeInt((NBN_Stream *)&ws, x, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt((NBN_Stream *)&ws, y, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt((NBN_Stream *)&ws, z, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeUInt((NBN_Stream *)&ws, conn, 0, UINT_MAX);

    // Accept the connection
    NBN_GameServer_AcceptIncomingConnectionWithData(data, sizeof(data));
}

static int HandleNewConnection(void)
{
    TraceLog(LOG_INFO, "New connection");

    // If the server is full
    if (client_count == MAX_CLIENTS)
    {
        // Reject the connection (send a SERVER_FULL_CODE code to the client)
        TraceLog(LOG_INFO, "Connection rejected");
        NBN_GameServer_RejectIncomingConnectionWithCode(SERVER_FULL_CODE);

        return 0;
    }

    // Otherwise...

    NBN_ConnectionHandle client_handle;

    client_handle = NBN_GameServer_GetIncomingConnection();

    // Get a spawning position for the client
    Vector3 spawn = spawns[client_handle % MAX_CLIENTS];

    // Build some "initial" data that will be sent to the connected client

    TPE_Unit x = spawn.x;
    TPE_Unit y = spawn.y;
    TPE_Unit z = spawn.z;

    AcceptConnection(x, y, z, client_handle);

    TraceLog(LOG_INFO, "Connection accepted (ID: %d)", client_handle);

    Client *client = NULL;

    // Find a free slot in the clients array and create a new client
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] == NULL)
        {
            client = malloc(sizeof(Client));
            clients[i] = client;

            break;
        }
    }

    assert(client != NULL);

    client->client_handle = client_handle; // Store the nbnet connection ID

    // Fill the client state with initial spawning data
    client->state = (ClientState){.client_id = client_handle, .x = 0, .y = 0, .z = 0};

    client_count++;

    return 0;
}

static Client *FindClientById(uint32_t client_id)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->state.client_id == client_id)
            return clients[i];
    }

    return NULL;
}

static void DestroyClient(Client *client)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i] && clients[i]->state.client_id == client->state.client_id)
        {
            clients[i] = NULL;

            return;
        }
    }

    free(client);
}

static void HandleClientDisconnection()
{
    NBN_ConnectionHandle client_handle = NBN_GameServer_GetDisconnectedClient(); // Get the disconnected client

    TraceLog(LOG_INFO, "Client has disconnected (id: %d)", client_handle);

    Client *client = FindClientById(client_handle);

    assert(client);

    DestroyClient(client);

    client_count--;
}

static void HandleUpdateStateMessage(UpdateStateMessage *msg, Client *sender)
{
    // Update the state of the client with the data from the received UpdateStateMessage message
    sender->state.x = msg->x;
    sender->state.y = msg->y;
    sender->state.z = msg->z;

    UpdateStateMessage_Destroy(msg);
}

static void ServerHandleReceivedMessage(void)
{
    // Fetch info about the last received message
    NBN_MessageInfo msg_info = NBN_GameServer_GetMessageInfo();

    // Find the client that sent the message
    Client *sender = FindClientById(msg_info.sender);

    assert(sender != NULL);

    switch (msg_info.type)
    {
        case UPDATE_STATE_MESSAGE:
            // The server received a client state update
            HandleUpdateStateMessage(msg_info.data, sender);
            break;
    }
}

static int HandleGameServerEvent(int ev)
{
    switch (ev)
    {
        case NBN_NEW_CONNECTION:
            // A new client has requested a connection
            if (HandleNewConnection() < 0)
                return -1;
            break;

        case NBN_CLIENT_DISCONNECTED:
            // A previously connected client has disconnected
            HandleClientDisconnection();
            break;

        case NBN_CLIENT_MESSAGE_RECEIVED:
            // A message from a client has been received
            ServerHandleReceivedMessage();
            break;
    }

    return 0;
}

// Broadcasts the latest game state to all connected clients
static int BroadcastGameState(void)
{
    ClientState client_states[MAX_CLIENTS];
    unsigned int client_index = 0;

    // Loop over the clients and build an array of ClientState
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        Client *client = clients[i];

        if (client == NULL) continue;

        client_states[client_index] = (ClientState) {
                .client_id = client->state.client_id,
                .x = client->state.x,
                .y = client->state.y,
                .z = client->state.z,
        };
        client_index++;
    }

    assert(client_index == client_count);

    // Broadcast the game state to all clients
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        Client *client = clients[i];

        if (client == NULL) continue;

        GameStateMessage *msg = GameStateMessage_Create();

        // Fill message data
        msg->client_count = client_count;
        memcpy(msg->client_states, client_states, sizeof(ClientState) * client_count);

        // Unreliably send the message to all connected clients
        NBN_GameServer_SendUnreliableMessageTo(client->client_handle, GAME_STATE_MESSAGE, msg);

        // TraceLog(LOG_DEBUG, "Sent game state to client %d (%d, %d)", client->client_id, client_count, client_index);
    }

    return 0;
}

static bool running = true;
static bool SERVER_STARTED = false;

#ifndef __EMSCRIPTEN__
static void SigintHandler(int dummy)
{
    running = false;
}
#endif

void* serverHandler(void* args)
{
    #ifndef __EMSCRIPTEN__
    signal(SIGINT, SigintHandler);
#endif

    // Even though we do not display anything we still use raylib logging capacibilities
    SetTraceLogLevel(LOG_DEBUG);

// #ifdef __EMSCRIPTEN__
//     NBN_WebRTC_Register(); // Register the WebRTC driver
// #else
//     NBN_UDP_Register(); // Register the UDP driver

// #endif // __EMSCRIPTEN__

    // Start the server with a protocol name and a port
    if (NBN_GameServer_StartEx(PROTOCOL_NAME, PORT) < 0)
    {
        TraceLog(LOG_ERROR, "Game server failed to start. Exit");
        fprintf(stderr, "[SERVER] Game server failed to start. Exit\n");

        return 1;
    }

    // Register messages, have to be done after NBN_GameServer_StartEx
    NBN_GameServer_RegisterMessage(
            UPDATE_STATE_MESSAGE,
            (NBN_MessageBuilder)UpdateStateMessage_Create,
            (NBN_MessageDestructor)UpdateStateMessage_Destroy,
            (NBN_MessageSerializer)UpdateStateMessage_Serialize);
    NBN_GameServer_RegisterMessage(
            GAME_STATE_MESSAGE,
            (NBN_MessageBuilder)GameStateMessage_Create,
            (NBN_MessageDestructor)GameStateMessage_Destroy,
            (NBN_MessageSerializer)GameStateMessage_Serialize);

    float tick_dt = 1.f / FPS; // Tick delta time

    SERVER_STARTED = true;

    while (running)
    {
        int ev;

        // Poll for server events
        while ((ev = NBN_GameServer_Poll()) != NBN_NO_EVENT)
        {
            if (ev < 0)
            {
                TraceLog(LOG_ERROR, "An occured while polling network events. Exit");
                fprintf(stderr, "[SERVER] An error occured while polling network events. Exit\n");

                break;
            }

            if (HandleGameServerEvent(ev) < 0)
                break;
        }

        // Broadcast latest game state
        if (BroadcastGameState() < 0)
        {
            TraceLog(LOG_ERROR, "An occured while broadcasting game states. Exit");
            fprintf(stderr, "[SERVER] An error occured while broadcasting game states. Exit\n");

            break;
        }

        // Pack all enqueued messages as packets and send them
        if (NBN_GameServer_SendPackets() < 0)
        {
            TraceLog(LOG_ERROR, "An occured while flushing the send queue. Exit");
            fprintf(stderr, "[SERVER] An error occured while flushing the send queue. Exit\n");

            break;
        }

        NBN_GameServerStats stats = NBN_GameServer_GetStats();

        TraceLog(LOG_TRACE, "Upload: %f Bps | Download: %f Bps", stats.upload_bandwidth, stats.download_bandwidth);

        // Cap the simulation rate to TICK_RATE ticks per second (just like for the client)
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(tick_dt * 1000);
#elif defined(_WIN32) || defined(_WIN64)
        Sleep(tick_dt * 1000);
#else
        long nanos = tick_dt * 1e9;
        struct timespec t = {.tv_sec = nanos / 999999999, .tv_nsec = nanos % 999999999};

        nanosleep(&t, &t);
#endif
    }

    // Stop the server
    NBN_GameServer_Stop();

    return 0;
}
#endif

// client specific

#ifdef CLIENT_MODE

static bool connected = false;         // Connected to the server
static bool disconnected = false;      // Got disconnected from the server
static bool spawned = false;           // Has spawned
static int server_close_code;          // The server code used when closing the connection
static ClientState local_client_state; // The state of the local client

// Array to hold other client states (`MAX_CLIENTS - 1` because we don't need to store the state of the local client)
static ClientState *other_clients[MAX_CLIENTS - 1] = {NULL};

/*
 * Array of client ids that were updated in the last received GameStateMessage.
 * This is used to detect and destroy disconnected remote clients.
 */
static int updated_ids[MAX_CLIENTS];

// Number of currently connected clients
static unsigned int local_client_count = 0;

static void SpawnLocalClient(int x, int y, int z, uint32_t client_id)
{
    TraceLog(LOG_INFO, "Received spawn message, position: (%d, %d), client id: %d", x, y, client_id);

    // Update the local client state based on spawn info sent by the server
    local_client_state.client_id = client_id;
    local_client_state.x = x;
    local_client_state.y = y;
    local_client_state.z = z;

    spawned = true;
}

static void HandleConnection(void)
{
    uint8_t data[32];
    unsigned int data_len = NBN_GameClient_ReadServerData(data);
    NBN_ReadStream rs;

    NBN_ReadStream_Init(&rs, data, data_len);

    int x = 0;
    int y = 0;
    int z = 0;
    unsigned int client_id = 0;

    NBN_SerializeInt(((NBN_Stream *)&rs), x, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt(((NBN_Stream *)&rs), y, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt(((NBN_Stream *)&rs), z, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeUInt(((NBN_Stream *)&rs), client_id, 0, UINT_MAX);

    SpawnLocalClient(x, y, z, client_id);

    connected = true;
}

static void HandleDisconnection(void)
{
    int code = NBN_GameClient_GetServerCloseCode(); // Get the server code used when closing the client connection

    TraceLog(LOG_INFO, "Disconnected from server (code: %d)", code);

    disconnected = true;
    server_close_code = code;
}

static bool ClientExists(uint32_t client_id)
{
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
    {
        if (other_clients[i] && other_clients[i]->client_id == client_id)
            return true;
    }

    return false;
}

static void CreateClient(ClientState state)
{
    TraceLog(LOG_DEBUG, "CreateClient %d", state.client_id);
    assert(local_client_count < MAX_CLIENTS - 1);

    ClientState *client = NULL;

    // Create a new remote client state and store it in the remote clients array at the first free slot found
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
    {
        if (other_clients[i] == NULL)
        {
            client = malloc(sizeof(ClientState));
            other_clients[i] = client;

            break;
        }
    }

    assert(client != NULL);

    // Fill the newly created client state with client state info received from the server
    memcpy(client, &state, sizeof(ClientState));

    local_client_count++;

    TraceLog(LOG_INFO, "New remote client (ID: %d)", client->client_id);
}

static void UpdateClient(ClientState state)
{
    ClientState *client = NULL;

    // Find the client matching the client id of the received remote client state
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
    {
        if (other_clients[i] && other_clients[i]->client_id == state.client_id)
        {
            client = other_clients[i];

            break;
        }
    }

    assert(client != NULL);

    // Update the client state with the latest client state info received from the server
    memcpy(client, &state, sizeof(ClientState));
}

static void LocalDestroyClient(uint32_t client_id)
{
    // Find the client matching the client id and destroy it
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
    {
        ClientState *client = other_clients[i];

        if (client && client->client_id == client_id)
        {
            TraceLog(LOG_INFO, "Destroy disconnected client (ID: %d)", client->client_id);

            free(client);
            other_clients[i] = NULL;
            local_client_count--;

            return;
        }
    }
}

static void DestroyDisconnectedClients(void)
{
    /* Loop over all remote client states and remove the one that have not
     * been updated with the last received game state.
     * This is how we detect disconnected clients.
     */
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
    {
        if (other_clients[i] == NULL)
            continue;

        uint32_t client_id = other_clients[i]->client_id;
        bool disconnected = true;

        for (int j = 0; j < MAX_CLIENTS; j++)
        {
            if ((int)client_id == updated_ids[j])
            {
                disconnected = false;

                break;
            }
        }

        if (disconnected)
            LocalDestroyClient(client_id);
    }
}

static void HandleGameStateMessage(GameStateMessage *msg)
{
    if (!spawned)
        return;

    // Start by resetting the updated client ids array
    for (int i = 0; i < MAX_CLIENTS; i++)
        updated_ids[i] = -1;

    // Loop over the received client states
    for (unsigned int i = 0; i < msg->client_count; i++)
    {
        ClientState state = msg->client_states[i];

        // Ignore the state of the local client
        if (state.client_id != local_client_state.client_id)
        {
            // If the client already exists we update it with the latest received state
            if (ClientExists(state.client_id))
                UpdateClient(state);
            else // If the client does not exist, we create it
                CreateClient(state);

            updated_ids[i] = state.client_id;
        }
    }

    // Destroy disconnected clients
    DestroyDisconnectedClients();

    GameStateMessage_Destroy(msg);
}

static void ClientHandleReceivedMessage(void)
{
    // Fetch info about the last received message
    NBN_MessageInfo msg_info = NBN_GameClient_GetMessageInfo();

    switch (msg_info.type)
    {
    // We received the latest game state from the server
    case GAME_STATE_MESSAGE:
        HandleGameStateMessage(msg_info.data);
        break;
    }
}

static void HandleGameClientEvent(int ev)
{
    switch (ev)
    {
    case NBN_CONNECTED:
        // We are connected to the server
        HandleConnection();
        break;

    case NBN_DISCONNECTED:
        // The server has closed our connection
        HandleDisconnection();
        break;

    case NBN_MESSAGE_RECEIVED:
        // We received a message from the server
        ClientHandleReceivedMessage();
        break;
    }
}

static int SendPositionUpdate(void)
{
    UpdateStateMessage *msg = UpdateStateMessage_Create();

    // Fill message data
    msg->x = local_client_state.x;
    msg->y = local_client_state.y;
    msg->z = local_client_state.z;

    // Unreliably send it to the server
    if (NBN_GameClient_SendUnreliableMessage(UPDATE_STATE_MESSAGE, msg) < 0)
        return -1;

    return 0;
}

#endif

// --------------- MAIN ------------------

int main(int argc, char *argv[])
{
    InitWindow(128,128,"findestory");
    SetTargetFPS(FPS);

    #ifdef __EMSCRIPTEN__
        NBN_WebRTC_Register(); // Register the WebRTC driver
    #else
        NBN_UDP_Register(); // Register the UDP driver
    #endif // __EMSCRIPTEN__

#ifdef SERVER_MODE
    // init server thread
    pthread_t server_thread;
    pthread_create(&server_thread, NULL, serverHandler, NULL);

    // esperar a que el server inicie (principalmente para que registre el driver que va a usar también el cliente)
    while(!SERVER_STARTED){}
#endif
#ifdef CLIENT_MODE
    // init client

    // Initialize the client with a protocol name (must be the same than the one used by the server), the server ip address and port

    // Start the client with a protocol name (must be the same than the one used by the server)
    // the server host and port
    if (NBN_GameClient_StartEx(PROTOCOL_NAME, "127.0.0.1", PORT, NULL, 0) < 0)
    {
        TraceLog(LOG_WARNING, "Game client failed to start. Exit");

        return 1;
    }

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
#endif

    TPE_worldInit(&tpe_world,tpe_bodies,0,0);

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
#ifdef CLIENT_MODE
        int ev;

        while ((ev = NBN_GameClient_Poll()) != NBN_NO_EVENT)
        {
            if (ev < 0)
            {
                TraceLog(LOG_WARNING, "An occured while polling network events. Exit");

                break;
            }

            HandleGameClientEvent(ev);
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
            int i0 = triangles[t*3 + 0];
            int i1 = triangles[t*3 + 1];
            int i2 = triangles[t*3 + 2];

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

        spherePos.x = (float)bodies[1].joints[0].position.x*SCALE_3D; 
        spherePos.y = (float)bodies[1].joints[0].position.y*SCALE_3D;
        spherePos.z = (float)bodies[1].joints[0].position.z*SCALE_3D;
    
        if (connected && !disconnected)
        {
            if(!spawned) break;
            
            local_client_state.x = 0;
            local_client_state.y = 0;
            local_client_state.z = 0;

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
#endif



        BeginDrawing();
            ClearBackground(LIGHTGRAY);
            BeginMode3D(camera);
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
            DrawFPS(10,10);
        EndDrawing();
    }

    CloseWindow();

    // TODO
    // crear una función para cerrar el cliente y el server
    // que libere los recursos referidos al driver una única vez, porque lo comparten

// #ifdef CLIENT_MODE
//     NBN_GameClient_Stop();
//     while(!disconnected){};
//     fprintf(stderr, "[CLIENT] Connection closed\n");
// #endif

#ifdef SERVER_MODE
    running = false;
    // Wait for thread to finish
    pthread_join(server_thread, NULL);
    fprintf(stderr, "[SERVER] Server has stopped\n");
#endif

    return 0;
}