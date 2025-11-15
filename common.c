#include "common.h"

void log_message(const char* component, const char* message) {
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    FILE* log_file = fopen("system.log", "a");
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, component, message);
        fclose(log_file);
    }
    printf("[%s] [%s] %s\n", timestamp, component, message);
}

void log_detailed(const char* component, const char* message, const char* username, const char* ip, int port) {
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));
    
    char detailed_msg[MAX_BUFFER];
    if (username && ip && port > 0) {
        snprintf(detailed_msg, sizeof(detailed_msg), "%s [User: %s, IP: %s, Port: %d]", 
                 message, username, ip, port);
    } else if (username) {
        snprintf(detailed_msg, sizeof(detailed_msg), "%s [User: %s]", message, username);
    } else if (ip) {
        snprintf(detailed_msg, sizeof(detailed_msg), "%s [IP: %s]", message, ip);
    } else {
        snprintf(detailed_msg, sizeof(detailed_msg), "%s", message);
    }
    
    FILE* log_file = fopen("system.log", "a");
    if (log_file) {
        fprintf(log_file, "[%s] [%s] %s\n", timestamp, component, detailed_msg);
        fclose(log_file);
    }
    printf("[%s] [%s] %s\n", timestamp, component, detailed_msg);
}

void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}
