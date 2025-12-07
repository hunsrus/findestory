// [TODO] tratar de no incluir estas cosas, hacer todo más generico
#include "tinyphysicsengine.h"
#include <raylib.h>
#include <stdlib.h>

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

// is a udp driver already registered?
static bool UDP_DRIVER_REGISTERED = false;

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
    TPE_Unit velocity[3];
} UpdateStateMessage;

// Client state, represents a client over the network
typedef struct
{
    uint32_t client_id;
    TPE_Unit x;
    TPE_Unit y;
    TPE_Unit z;
    TPE_Unit velocity[3];
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
    NBN_SerializeInt(stream, msg->velocity[0], INT16_MIN, INT16_MAX);
    NBN_SerializeInt(stream, msg->velocity[1], INT16_MIN, INT16_MAX);
    NBN_SerializeInt(stream, msg->velocity[2], INT16_MIN, INT16_MAX);
    

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
        NBN_SerializeInt(stream, msg->client_states[i].velocity[0], INT16_MIN, INT16_MAX);
        NBN_SerializeInt(stream, msg->client_states[i].velocity[1], INT16_MIN, INT16_MAX);
        NBN_SerializeInt(stream, msg->client_states[i].velocity[2], INT16_MIN, INT16_MAX);
    }

    return 0;
}

// --------------- SERVER CLIENT ---------------

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

static void AcceptConnection(TPE_Unit x, TPE_Unit y, TPE_Unit z, TPE_Unit velocity[3], NBN_ConnectionHandle conn)
{
    NBN_WriteStream ws;
    uint8_t data[32];

    NBN_WriteStream_Init(&ws, data, sizeof(data));

    NBN_SerializeInt((NBN_Stream *)&ws, x, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt((NBN_Stream *)&ws, y, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt((NBN_Stream *)&ws, z, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt((NBN_Stream *)&ws, velocity[0], INT16_MIN, INT16_MAX);
    NBN_SerializeInt((NBN_Stream *)&ws, velocity[1], INT16_MIN, INT16_MAX);
    NBN_SerializeInt((NBN_Stream *)&ws, velocity[2], INT16_MIN, INT16_MAX);
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
    // Vector3 spawn = spawns[client_handle % MAX_CLIENTS];

    // Build some "initial" data that will be sent to the connected client

    TPE_Vec3 spawn_pos = bodies[1].joints[0].position;
    TPE_Unit spawn_vel[3];
    spawn_vel[0] = bodies[1].joints[0].velocity[0];
    spawn_vel[1] = bodies[1].joints[0].velocity[1];
    spawn_vel[2] = bodies[1].joints[0].velocity[2];

    AcceptConnection(spawn_pos.x, spawn_pos.y, spawn_pos.z, spawn_vel , client_handle);

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
    client->state = (ClientState){.client_id = client_handle, .x = spawn_pos.x, .y = spawn_pos.y, .z = spawn_pos.z, .velocity[0] = spawn_vel[0], .velocity[1] = spawn_vel[1], .velocity[2] = spawn_vel[3]};

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
    sender->state.velocity[0] = msg->velocity[0];
    sender->state.velocity[1] = msg->velocity[1];
    sender->state.velocity[2] = msg->velocity[2];

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
                .velocity[0] = client->state.velocity[0],
                .velocity[1] = client->state.velocity[1],
                .velocity[2] = client->state.velocity[2],
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

int serverHandler(void* args)
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

// --------------- CLIENT SPECIFIC ---------------

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

static bool CLIENT_STARTED = false;

static void SpawnLocalClient(int x, int y, int z, TPE_Unit velocity[3], uint32_t client_id)
{
    TraceLog(LOG_INFO, "Received spawn message, position: (%d, %d), client id: %d", x, y, client_id);

    // Update the local client state based on spawn info sent by the server
    local_client_state.client_id = client_id;
    local_client_state.x = x;
    local_client_state.y = y;
    local_client_state.z = z;
    local_client_state.velocity[0] = velocity[0];
    local_client_state.velocity[1] = velocity[1];
    local_client_state.velocity[2] = velocity[2];

    spawned = true;
}

static void HandleConnection(void)
{
    uint8_t data[32];
    unsigned int data_len = NBN_GameClient_ReadServerData(data);
    NBN_ReadStream rs;
    TPE_Unit aux_velocity[3] = { 0 };

    NBN_ReadStream_Init(&rs, data, data_len);

    int x = 0;
    int y = 0;
    int z = 0;
    unsigned int client_id = 0;

    NBN_SerializeInt(((NBN_Stream *)&rs), x, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt(((NBN_Stream *)&rs), y, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt(((NBN_Stream *)&rs), z, -ROOM_SIZE/2, ROOM_SIZE);
    NBN_SerializeInt(((NBN_Stream *)&rs), aux_velocity[0], INT16_MIN, INT16_MAX);
    NBN_SerializeInt(((NBN_Stream *)&rs), aux_velocity[1], INT16_MIN, INT16_MAX);
    NBN_SerializeInt(((NBN_Stream *)&rs), aux_velocity[2], INT16_MIN, INT16_MAX);
    NBN_SerializeUInt(((NBN_Stream *)&rs), client_id, 0, UINT_MAX);

    SpawnLocalClient(x, y, z, aux_velocity, client_id);

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

    // generate a physics body for the new client
    TPE_Joint *clientJoints = (TPE_Joint*)MemAlloc(sizeof(TPE_Joint));
    clientJoints[0] = TPE_joint(TPE_vec3(client->x,client->y,client->z),BALL_SIZE);
    TPE_Connection *clientConnections = (TPE_Connection*)MemAlloc(sizeof(TPE_Connection)*0);
    TPE_bodyInit(&bodies[tpe_world.bodyCount++],clientJoints,1,clientConnections,0,200);

    bodies[tpe_world.bodyCount-1].flags |= TPE_BODY_FLAG_ALWAYS_ACTIVE;

    TPE_worldInit(&tpe_world,bodies,tpe_world.bodyCount,environmentDistance);
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
    msg->velocity[0] = local_client_state.velocity[0];
    msg->velocity[1] = local_client_state.velocity[1];
    msg->velocity[2] = local_client_state.velocity[2];

    // Unreliably send it to the server
    if (NBN_GameClient_SendUnreliableMessage(UPDATE_STATE_MESSAGE, msg) < 0)
        return -1;

    return 0;
}

