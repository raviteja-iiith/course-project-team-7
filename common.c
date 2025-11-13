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

void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", t);
}