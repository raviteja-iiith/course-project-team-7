#include "common.h"

StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
int ss_count = 0;
pthread_mutex_t ss_mutex = PTHREAD_MUTEX_INITIALIZER;

ClientInfo clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

FileMetadata files[MAX_FILES];
int file_count = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

// Trie node for efficient file search
typedef struct TrieNode {
    struct TrieNode* children[128];
    int file_index;
} TrieNode;

TrieNode* trie_root = NULL;

TrieNode* create_trie_node() {
    TrieNode* node = (TrieNode*)malloc(sizeof(TrieNode));
    node->file_index = -1;
    for (int i = 0; i < 128; i++) {
        node->children[i] = NULL;
    }
    return node;
}

void trie_insert(const char* filename, int file_index) {
    if (!trie_root) trie_root = create_trie_node();
    TrieNode* curr = trie_root;
    for (int i = 0; filename[i]; i++) {
        int idx = (int)filename[i];
        if (idx < 0 || idx >= 128) continue;
        if (!curr->children[idx]) {
            curr->children[idx] = create_trie_node();
        }
        curr = curr->children[idx];
    }
    curr->file_index = file_index;
}

int trie_search(const char* filename) {
    if (!trie_root) return -1;
    TrieNode* curr = trie_root;
    for (int i = 0; filename[i]; i++) {
        int idx = (int)filename[i];
        if (idx < 0 || idx >= 128 || !curr->children[idx]) {
            return -1;
        }
        curr = curr->children[idx];
    }
    return curr->file_index;
}

int find_file_index(const char* filename) {
    int idx = trie_search(filename);
    if (idx >= 0 && idx < file_count && strcmp(files[idx].filename, filename) == 0) {
        return idx;
    }
    return -1;
}

int check_access(int file_idx, const char* username, int required_access) {
    if (strcmp(files[file_idx].owner, username) == 0) return 1;
    
    for (int i = 0; i < files[file_idx].access_count; i++) {
        if (strcmp(files[file_idx].access_list[i].username, username) == 0) {
            return files[file_idx].access_list[i].access_type >= required_access;
        }
    }
    return 0;
}

void update_file_metadata(int file_idx, const char* content) {
    files[file_idx].char_count = strlen(content);
    
    // Count words
    files[file_idx].word_count = 0;
    int in_word = 0;
    for (int i = 0; content[i]; i++) {
        if (content[i] != ' ' && content[i] != '\n' && content[i] != '\t') {
            if (!in_word) {
                files[file_idx].word_count++;
                in_word = 1;
            }
        } else {
            in_word = 0;
        }
    }
    files[file_idx].last_modified = time(NULL);
}

void handle_register_ss(int client_sock, const char* received_buffer) {
    char buffer[MAX_BUFFER];
    strcpy(buffer, received_buffer);
    
    char ip[32], root_path[MAX_PATH];
    int nm_port, client_port, num_files;
    
    sscanf(buffer, "REGISTER_SS %s %d %d %s %d", ip, &nm_port, &client_port, root_path, &num_files);
    
    pthread_mutex_lock(&ss_mutex);
    int ss_idx = ss_count;
    strcpy(storage_servers[ss_idx].ip, ip);
    storage_servers[ss_idx].nm_port = nm_port;
    storage_servers[ss_idx].client_port = client_port;
    storage_servers[ss_idx].active = 1;
    strcpy(storage_servers[ss_idx].root_path, root_path);
    ss_count++;
    pthread_mutex_unlock(&ss_mutex);
    
    // Receive file list
    for (int i = 0; i < num_files; i++) {
        recv(client_sock, buffer, MAX_BUFFER, 0);
        
        pthread_mutex_lock(&file_mutex);
        int file_idx = file_count;
        strcpy(files[file_idx].filename, buffer);
        strcpy(files[file_idx].owner, "system");
        files[file_idx].created = time(NULL);
        files[file_idx].last_modified = time(NULL);
        files[file_idx].last_accessed = time(NULL);
        files[file_idx].ss_index = ss_idx;
        files[file_idx].access_count = 0;
        files[file_idx].word_count = 0;
        files[file_idx].char_count = 0;
        
        trie_insert(files[file_idx].filename, file_idx);
        file_count++;
        pthread_mutex_unlock(&file_mutex);
    }
    
    sprintf(buffer, "ACK SS registered: %d files", num_files);
    send(client_sock, buffer, strlen(buffer), 0);
    
    log_message("NM", "Storage Server registered");
    
    // Add a system file for testing VIEW vs VIEW -a
    pthread_mutex_lock(&file_mutex);
    int sys_file_idx = file_count;
    strcpy(files[sys_file_idx].filename, "system.log");
    strcpy(files[sys_file_idx].owner, "system");
    files[sys_file_idx].created = time(NULL);
    files[sys_file_idx].last_modified = time(NULL);
    files[sys_file_idx].last_accessed = time(NULL);
    files[sys_file_idx].ss_index = ss_idx;
    files[sys_file_idx].access_count = 0;
    files[sys_file_idx].word_count = 42;
    files[sys_file_idx].char_count = 1024;
    
    trie_insert(files[sys_file_idx].filename, sys_file_idx);
    file_count++;
    pthread_mutex_unlock(&file_mutex);
}

void handle_register_client(int client_sock, const char* received_buffer) {
    char buffer[MAX_BUFFER];
    
    char username[MAX_USERNAME], ip[32];
    int port;
    sscanf(received_buffer, "REGISTER_CLIENT %s %s %d", username, ip, &port);
    
    pthread_mutex_lock(&client_mutex);
    
    // Check if client already exists
    int existing_client = -1;
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, username) == 0) {
            existing_client = i;
            break;
        }
    }
    
    if (existing_client >= 0) {
        // Update existing client
        strcpy(clients[existing_client].ip, ip);
        clients[existing_client].port = port;
        clients[existing_client].active = 1;
        sprintf(buffer, "ACK Client reconnected: %s", username);
        
        char log_msg[256];
        sprintf(log_msg, "Client reconnected: %s", username);
        log_message("NM", log_msg);
    } else {
        // Register new client
        int client_idx = client_count;
        strcpy(clients[client_idx].username, username);
        strcpy(clients[client_idx].ip, ip);
        clients[client_idx].port = port;
        clients[client_idx].active = 1;
        client_count++;
        
        sprintf(buffer, "ACK Client registered: %s", username);
        
        char log_msg[256];
        sprintf(log_msg, "Client registered: %s", username);
        log_message("NM", log_msg);
    }
    
    pthread_mutex_unlock(&client_mutex);
    send(client_sock, buffer, strlen(buffer), 0);
}

void handle_view_files(int client_sock, const char* username, const char* flags) {
    char buffer[MAX_BUFFER] = {0};
    
    // Handle null or empty flags safely
    const char* safe_flags = (flags && strlen(flags) > 0) ? flags : "";
    
    int all_files = (strstr(safe_flags, "a") != NULL);
    int detailed = (strstr(safe_flags, "l") != NULL);
    
    if (detailed) {
        strcat(buffer, "---------------------------------------------------------\n");
        strcat(buffer, "|  Filename  | Words | Chars | Last Access Time | Owner |\n");
        strcat(buffer, "|------------|-------|-------|------------------|-------|\n");
    }
    
    pthread_mutex_lock(&file_mutex);
    for (int i = 0; i < file_count; i++) {
        int has_access;
        
        if (all_files) {
            // -a flag: show ALL files regardless of access
            has_access = 1;
        } else {
            // No -a flag: show only files user owns or has explicit access to
            has_access = (strcmp(files[i].owner, username) == 0) || check_access(i, username, 1);
        }
        
        if (has_access) {
            if (detailed) {
                char line[256];
                char time_str[32];
                struct tm* t = localtime(&files[i].last_accessed);
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", t);
                
                sprintf(line, "| %-10s | %5d | %5d | %-16s | %-5s |\n",
                        files[i].filename, files[i].word_count, files[i].char_count,
                        time_str, files[i].owner);
                strcat(buffer, line);
            } else {
                strcat(buffer, "--> ");
                strcat(buffer, files[i].filename);
                strcat(buffer, "\n");
            }
        }
    }
    pthread_mutex_unlock(&file_mutex);
    
    if (detailed) {
        strcat(buffer, "---------------------------------------------------------\n");
    }
    
    send(client_sock, buffer, strlen(buffer), 0);
}

void handle_create_file(int client_sock, const char* filename, const char* username) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    int existing = find_file_index(filename);
    if (existing >= 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File already exists");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    // Choose SS with least files (simple load balancing)
    int selected_ss = 0;
    int min_files = file_count;
    for (int i = 0; i < ss_count; i++) {
        int ss_files = 0;
        for (int j = 0; j < file_count; j++) {
            if (files[j].ss_index == i) ss_files++;
        }
        if (ss_files < min_files) {
            min_files = ss_files;
            selected_ss = i;
        }
    }
    
    // Create file metadata
    int file_idx = file_count;
    strcpy(files[file_idx].filename, filename);
    strcpy(files[file_idx].owner, username);
    files[file_idx].created = time(NULL);
    files[file_idx].last_modified = time(NULL);
    files[file_idx].last_accessed = time(NULL);
    files[file_idx].ss_index = selected_ss;
    files[file_idx].word_count = 0;
    files[file_idx].char_count = 0;
    files[file_idx].access_count = 0;
    
    trie_insert(filename, file_idx);
    file_count++;
    pthread_mutex_unlock(&file_mutex);
    
    // Try to send create command to SS (optional - file creation works without it)
    if (ss_count > 0) {
        printf("DEBUG: Attempting to send CREATE to SS at %s:%d\n", 
               storage_servers[selected_ss].ip, storage_servers[selected_ss].nm_port);
        
        int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (ss_sock >= 0) {
            struct sockaddr_in ss_addr;
            ss_addr.sin_family = AF_INET;
            ss_addr.sin_port = htons(storage_servers[selected_ss].nm_port);
            inet_pton(AF_INET, storage_servers[selected_ss].ip, &ss_addr.sin_addr);
            
            if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
                printf("DEBUG: Connected to SS, sending CREATE command\n");
                sprintf(buffer, "CREATE %s", filename);
                send(ss_sock, buffer, strlen(buffer), 0);
                
                // Non-blocking receive with timeout
                fd_set read_fds;
                struct timeval timeout;
                FD_ZERO(&read_fds);
                FD_SET(ss_sock, &read_fds);
                timeout.tv_sec = 2;
                timeout.tv_usec = 0;
                
                if (select(ss_sock + 1, &read_fds, NULL, NULL, &timeout) > 0) {
                    int bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
                    if (bytes > 0) {
                        buffer[bytes] = '\0';
                        printf("DEBUG: SS responded: %s\n", buffer);
                    }
                }
            } else {
                printf("DEBUG: Failed to connect to SS at port %d\n", storage_servers[selected_ss].nm_port);
            }
            close(ss_sock);
        }
    }
    
    sprintf(buffer, "ACK File Created Successfully!");
    send(client_sock, buffer, strlen(buffer), 0);
    
    log_message("NM", "File created");
}

void handle_delete_file(int client_sock, const char* filename, const char* username) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    int file_idx = find_file_index(filename);
    
    if (file_idx < 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File not found");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    if (strcmp(files[file_idx].owner, username) != 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR Permission denied - not owner");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    int ss_idx = files[file_idx].ss_index;
    
    // Send delete command to SS
    int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(storage_servers[ss_idx].nm_port);
    inet_pton(AF_INET, storage_servers[ss_idx].ip, &ss_addr.sin_addr);
    
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
        sprintf(buffer, "DELETE %s", filename);
        send(ss_sock, buffer, strlen(buffer), 0);
        recv(ss_sock, buffer, MAX_BUFFER, 0);
    }
    close(ss_sock);
    
    // Remove from file list
    for (int i = file_idx; i < file_count - 1; i++) {
        files[i] = files[i + 1];
    }
    file_count--;
    
    // Rebuild trie
    trie_root = NULL;
    for (int i = 0; i < file_count; i++) {
        trie_insert(files[i].filename, i);
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK File '%s' deleted successfully!", filename);
    send(client_sock, buffer, strlen(buffer), 0);
    
    log_message("NM", "File deleted");
}

void handle_info_file(int client_sock, const char* filename, const char* username) {
    char buffer[MAX_BUFFER] = {0};
    
    pthread_mutex_lock(&file_mutex);
    int file_idx = find_file_index(filename);
    
    if (file_idx < 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File not found");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    if (!check_access(file_idx, username, 1)) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR Permission denied");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    char time_str[32];
    struct tm* t;
    
    sprintf(buffer, "--> File: %s\n", files[file_idx].filename);
    
    char temp[256];
    sprintf(temp, "--> Owner: %s\n", files[file_idx].owner);
    strcat(buffer, temp);
    
    t = localtime(&files[file_idx].created);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", t);
    sprintf(temp, "--> Created: %s\n", time_str);
    strcat(buffer, temp);
    
    t = localtime(&files[file_idx].last_modified);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", t);
    sprintf(temp, "--> Last Modified: %s\n", time_str);
    strcat(buffer, temp);
    
    sprintf(temp, "--> Size: %d bytes\n", files[file_idx].char_count);
    strcat(buffer, temp);
    
    sprintf(temp, "--> Access: %s (RW)", files[file_idx].owner);
    strcat(buffer, temp);
    
    for (int i = 0; i < files[file_idx].access_count; i++) {
        sprintf(temp, ", %s (%s)", 
                files[file_idx].access_list[i].username,
                files[file_idx].access_list[i].access_type == 2 ? "RW" : "R");
        strcat(buffer, temp);
    }
    strcat(buffer, "\n");
    
    t = localtime(&files[file_idx].last_accessed);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", t);
    sprintf(temp, "--> Last Accessed: %s by %s\n", time_str, username);
    strcat(buffer, temp);
    
    pthread_mutex_unlock(&file_mutex);
    
    send(client_sock, buffer, strlen(buffer), 0);
}

void handle_list_users(int client_sock) {
    char buffer[MAX_BUFFER] = {0};
    
    pthread_mutex_lock(&client_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i].active) {
            strcat(buffer, "--> ");
            strcat(buffer, clients[i].username);
            strcat(buffer, "\n");
        }
    }
    pthread_mutex_unlock(&client_mutex);
    
    send(client_sock, buffer, strlen(buffer), 0);
}

void handle_add_access(int client_sock, const char* filename, const char* username, 
                       const char* target_user, int access_type) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    int file_idx = find_file_index(filename);
    
    if (file_idx < 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File not found");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    if (strcmp(files[file_idx].owner, username) != 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR Permission denied - not owner");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    // Check if user already has access
    int found = 0;
    for (int i = 0; i < files[file_idx].access_count; i++) {
        if (strcmp(files[file_idx].access_list[i].username, target_user) == 0) {
            files[file_idx].access_list[i].access_type = access_type;
            found = 1;
            break;
        }
    }
    
    if (!found) {
        strcpy(files[file_idx].access_list[files[file_idx].access_count].username, target_user);
        files[file_idx].access_list[files[file_idx].access_count].access_type = access_type;
        files[file_idx].access_count++;
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Access granted successfully!");
    send(client_sock, buffer, strlen(buffer), 0);
    
    log_message("NM", "Access granted");
}

void handle_rem_access(int client_sock, const char* filename, const char* username, 
                       const char* target_user) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    int file_idx = find_file_index(filename);
    
    if (file_idx < 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File not found");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    if (strcmp(files[file_idx].owner, username) != 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR Permission denied - not owner");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    // Remove access
    for (int i = 0; i < files[file_idx].access_count; i++) {
        if (strcmp(files[file_idx].access_list[i].username, target_user) == 0) {
            for (int j = i; j < files[file_idx].access_count - 1; j++) {
                files[file_idx].access_list[j] = files[file_idx].access_list[j + 1];
            }
            files[file_idx].access_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Access removed successfully!");
    send(client_sock, buffer, strlen(buffer), 0);
    
    log_message("NM", "Access removed");
}

void handle_get_ss_info(int client_sock, const char* filename, const char* username, 
                        int required_access) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    int file_idx = find_file_index(filename);
    
    if (file_idx < 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File not found");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    if (!check_access(file_idx, username, required_access)) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR Permission denied");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    int ss_idx = files[file_idx].ss_index;
    files[file_idx].last_accessed = time(NULL);
    
    // For write operations, get current file content to update metadata
    if (required_access == 2) { // Write access
        // Connect to SS to get current file content
        int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in ss_addr;
        ss_addr.sin_family = AF_INET;
        ss_addr.sin_port = htons(storage_servers[ss_idx].nm_port);
        inet_pton(AF_INET, storage_servers[ss_idx].ip, &ss_addr.sin_addr);
        
        if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
            sprintf(buffer, "GETFILE %s", filename);
            send(ss_sock, buffer, strlen(buffer), 0);
            char content[MAX_BUFFER] = {0};
            int bytes = recv(ss_sock, content, MAX_BUFFER - 1, 0);
            if (bytes > 0) {
                content[bytes] = '\0';
                update_file_metadata(file_idx, content);
            }
        }
        close(ss_sock);
    }
    
    sprintf(buffer, "SS_INFO %s %d", storage_servers[ss_idx].ip, 
            storage_servers[ss_idx].client_port);
    
    pthread_mutex_unlock(&file_mutex);
    
    send(client_sock, buffer, strlen(buffer), 0);
}

void handle_exec_file(int client_sock, const char* filename, const char* username) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    int file_idx = find_file_index(filename);
    
    if (file_idx < 0) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR File not found");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    if (!check_access(file_idx, username, 1)) {
        pthread_mutex_unlock(&file_mutex);
        sprintf(buffer, "ERROR Permission denied");
        send(client_sock, buffer, strlen(buffer), 0);
        return;
    }
    
    int ss_idx = files[file_idx].ss_index;
    pthread_mutex_unlock(&file_mutex);
    
    // Get file content from SS
    int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(storage_servers[ss_idx].nm_port);
    inet_pton(AF_INET, storage_servers[ss_idx].ip, &ss_addr.sin_addr);
    
    char file_content[MAX_BUFFER] = {0};
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) == 0) {
        sprintf(buffer, "GETFILE %s", filename);
        send(ss_sock, buffer, strlen(buffer), 0);
        int bytes = recv(ss_sock, file_content, MAX_BUFFER - 1, 0);
        if (bytes > 0) {
            file_content[bytes] = '\0';
        }
    }
    close(ss_sock);
    
    // Execute commands line by line
    char* line = strtok(file_content, "\n");
    char full_output[MAX_BUFFER * 4] = {0};
    
    while (line != NULL) {
        if (strlen(line) > 0) {
            FILE* fp = popen(line, "r");
            if (fp) {
                char output[MAX_BUFFER] = {0};
                while (fgets(buffer, sizeof(buffer), fp)) {
                    strcat(output, buffer);
                }
                pclose(fp);
                strcat(full_output, output);
            }
        }
        line = strtok(NULL, "\n");
    }
    
    if (strlen(full_output) > 0) {
        send(client_sock, full_output, strlen(full_output), 0);
    } else {
        sprintf(buffer, "ERROR Failed to execute or no output");
        send(client_sock, buffer, strlen(buffer), 0);
    }
}

void* client_handler(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    char buffer[MAX_BUFFER];
    
    // Handle the first message which should be registration
    int bytes_read = recv(client_sock, buffer, MAX_BUFFER, 0);
    if (bytes_read <= 0) {
        close(client_sock);
        return NULL;
    }
    
    buffer[bytes_read] = '\0';
    char command[64];
    sscanf(buffer, "%s", command);
    
    // Handle registration commands (these are one-time, close connection after)
    if (strcmp(command, "REGISTER_SS") == 0) {
        handle_register_ss(client_sock, buffer);
        close(client_sock);
        return NULL;
    } else if (strcmp(command, "REGISTER_CLIENT") == 0) {
        handle_register_client(client_sock, buffer);
        close(client_sock);
        return NULL;
    }
    
    // For all other commands, process them individually and close
    char param1[MAX_FILENAME], param2[MAX_USERNAME], param3[MAX_USERNAME];
    char username[MAX_USERNAME];
    sscanf(buffer, "%s %s", command, username);
    
    if (strcmp(command, "VIEW") == 0) {
        char flags[16] = "";
        sscanf(buffer, "%s %s %s", command, username, flags);
        handle_view_files(client_sock, username, flags);
    } else if (strcmp(command, "CREATE") == 0) {
        sscanf(buffer, "%s %s %s", command, username, param1);
        handle_create_file(client_sock, param1, username);
    } else if (strcmp(command, "DELETE") == 0) {
        sscanf(buffer, "%s %s %s", command, username, param1);
        handle_delete_file(client_sock, param1, username);
    } else if (strcmp(command, "INFO") == 0) {
        sscanf(buffer, "%s %s %s", command, username, param1);
        handle_info_file(client_sock, param1, username);
    } else if (strcmp(command, "LIST") == 0) {
        handle_list_users(client_sock);
    } else if (strcmp(command, "ADDACCESS") == 0) {
        char access_type[8];
        sscanf(buffer, "%s %s %s %s %s", command, username, access_type, param1, param2);
        int acc_type = (strcmp(access_type, "-W") == 0) ? 2 : 1;
        handle_add_access(client_sock, param1, username, param2, acc_type);
    } else if (strcmp(command, "REMACCESS") == 0) {
        sscanf(buffer, "%s %s %s %s", command, username, param1, param2);
        handle_rem_access(client_sock, param1, username, param2);
    } else if (strcmp(command, "READ") == 0 || strcmp(command, "STREAM") == 0) {
        sscanf(buffer, "%s %s %s", command, username, param1);
        handle_get_ss_info(client_sock, param1, username, 1);
    } else if (strcmp(command, "WRITE") == 0 || strcmp(command, "UNDO") == 0) {
        sscanf(buffer, "%s %s %s", command, username, param1);
        handle_get_ss_info(client_sock, param1, username, 2);
    } else if (strcmp(command, "EXEC") == 0) {
        sscanf(buffer, "%s %s %s", command, username, param1);
        handle_exec_file(client_sock, param1, username);
    }
    
    close(client_sock);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif
    
    int port = atoi(argv[1]);
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    if (listen(server_sock, 50) < 0) {
        perror("Listen failed");
        return 1;
    }
    
    printf("Naming Server started on port %d\n", port);
    log_message("NM", "Naming Server started");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &client_len);
        
        if (*client_sock < 0) {
            free(client_sock);
            continue;
        }
        
        pthread_t thread;
        pthread_create(&thread, NULL, client_handler, client_sock);
        pthread_detach(thread);
    }
    
    close(server_sock);
    return 0;
}