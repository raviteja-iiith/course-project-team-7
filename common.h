#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <direct.h>
    #include <io.h>
    #include <process.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    #define mkdir(dir, mode) _mkdir(dir)
    #define usleep(x) Sleep((x)/1000)
    typedef int socklen_t;
    typedef HANDLE pthread_t;
    typedef CRITICAL_SECTION pthread_mutex_t;
    #define PTHREAD_MUTEX_INITIALIZER {0}
    #define pthread_mutex_lock EnterCriticalSection
    #define pthread_mutex_unlock LeaveCriticalSection
    #define pthread_create(thread, attr, func, arg) ((*thread = (HANDLE)_beginthreadex(NULL, 0, (unsigned (__stdcall *)(void *))func, arg, 0, NULL)) ? 0 : -1)
    #define pthread_detach(thread) CloseHandle(thread)
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <pthread.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <fcntl.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
#endif

#define MAX_BUFFER 8192
#define MAX_PATH 512
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_CLIENTS 100
#define MAX_STORAGE_SERVERS 20
#define MAX_FILES 1000
#define MAX_SENTENCE_LENGTH 4096
#define MAX_ACCESS_USERS 50
#define MAX_FOLDERS 100
#define MAX_CHECKPOINTS 20

// Error codes
#define ERR_FILE_NOT_FOUND 1
#define ERR_PERMISSION_DENIED 2
#define ERR_FILE_EXISTS 3
#define ERR_SENTENCE_LOCKED 4
#define ERR_INVALID_INDEX 5
#define ERR_STORAGE_SERVER_DOWN 6
#define ERR_GENERAL 99

// Message types
#define MSG_REGISTER_SS 101
#define MSG_REGISTER_CLIENT 102
#define MSG_CREATE_FILE 103
#define MSG_DELETE_FILE 104
#define MSG_READ_FILE 105
#define MSG_WRITE_FILE 106
#define MSG_INFO_FILE 107
#define MSG_VIEW_FILES 108
#define MSG_STREAM_FILE 109
#define MSG_ADD_ACCESS 110
#define MSG_REM_ACCESS 111
#define MSG_LIST_USERS 112
#define MSG_EXEC_FILE 113
#define MSG_UNDO 114
#define MSG_ACK 200
#define MSG_ERROR 201
#define MSG_DATA 202
#define MSG_SS_GET_FILE 203

typedef struct {
    char username[MAX_USERNAME];
    int access_type; // 0=none, 1=read, 2=read+write
} AccessInfo;

typedef struct {
    char filename[MAX_FILENAME];
    char owner[MAX_USERNAME];
    time_t created;
    time_t last_modified;
    time_t last_accessed;
    int word_count;
    int char_count;
    AccessInfo access_list[MAX_ACCESS_USERS];
    int access_count;
    int ss_index;
    char folder[MAX_FILENAME];  // Folder path (empty string = root)
} FileMetadata;

typedef struct {
    char foldername[MAX_FILENAME];
    char owner[MAX_USERNAME];
    time_t created;
} FolderMetadata;

typedef struct {
    char tag[MAX_USERNAME];
    char content[MAX_BUFFER];
    time_t timestamp;
} CheckpointData;

typedef struct {
    char ip[32];
    int nm_port;
    int client_port;
    int active;
    char root_path[MAX_PATH];
} StorageServerInfo;

typedef struct {
    char username[MAX_USERNAME];
    char ip[32];
    int port;
    int active;
} ClientInfo;

void log_message(const char* component, const char* message);
void log_detailed(const char* component, const char* message, const char* username, const char* ip, int port);
void get_timestamp(char* buffer, size_t size);

#endif