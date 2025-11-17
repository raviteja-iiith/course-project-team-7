#include "common.h"

char root_path[MAX_PATH];
char backup_path[MAX_PATH];

#define MAX_SENTENCE_LOCKS 100

typedef struct {
    int sentence_idx;
    char lock_holder[MAX_USERNAME];
} SentenceLock;

typedef struct {
    char filename[MAX_FILENAME];
    char content[MAX_BUFFER];
    char backup_content[MAX_BUFFER];
    SentenceLock sentence_locks[MAX_SENTENCE_LOCKS];
    int lock_count;
    int has_backup;
    CheckpointData checkpoints[MAX_CHECKPOINTS];
    int checkpoint_count;
} FileData;

FileData files[MAX_FILES];
int file_count = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void save_file_to_disk(const char* filename, const char* content) {
    char filepath[MAX_PATH + MAX_FILENAME];
#ifdef _WIN32
    snprintf(filepath, sizeof(filepath), "%s\\%s", root_path, filename);
#else
    snprintf(filepath, sizeof(filepath), "%s/%s", root_path, filename);
#endif
    FILE* f = fopen(filepath, "w");
    if (f) {
        fputs(content, f);
        fclose(f);
    }
}

void load_file_from_disk(const char* filename, char* content) {
    char filepath[MAX_PATH + MAX_FILENAME];
#ifdef _WIN32
    snprintf(filepath, sizeof(filepath), "%s\\%s", root_path, filename);
#else
    snprintf(filepath, sizeof(filepath), "%s/%s", root_path, filename);
#endif
    content[0] = '\0';  // Initialize to empty
    FILE* f = fopen(filepath, "r");
    if (f) {
        fread(content, 1, MAX_BUFFER - 1, f);
        content[MAX_BUFFER - 1] = '\0';  // Ensure null termination
        fclose(f);
    }
}

int find_file(const char* filename) {
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i].filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

void split_into_sentences(const char* text, char sentences[][MAX_SENTENCE_LENGTH], int* count) {
    *count = 0;
    int j = 0;
    
    // If text is empty, return 0 sentences
    if (!text || text[0] == '\0') {
        return;
    }
    
    for (int i = 0; text[i]; i++) {
        if (j < MAX_SENTENCE_LENGTH - 1) {
            sentences[*count][j++] = text[i];
        }
        
        // Split ONLY on sentence delimiters: . ! ?
        if (text[i] == '.' || text[i] == '!' || text[i] == '?') {
            sentences[*count][j] = '\0';
            (*count)++;
            j = 0;
            if (*count >= 100) break;  // Safety check
        }
    }
    
    // Add last sentence if any remaining text (sentence without delimiter)
    if (j > 0 && *count < 100) {
        sentences[*count][j] = '\0';
        (*count)++;
    }
}

void split_into_words(const char* sentence, char words[][256], int* count) {
    *count = 0;
    int j = 0;
    for (int i = 0; sentence[i]; i++) {
        if (sentence[i] == ' ') {
            if (j > 0 && *count < 100) {
                words[*count][j] = '\0';
                (*count)++;
                j = 0;
            }
        } else {
            if (j < 255) {
                words[*count][j++] = sentence[i];
            }
        }
    }
    if (j > 0 && *count < 100) {
        words[*count][j] = '\0';
        (*count)++;
    }
}

void handle_create(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    if (find_file(filename) >= 0) {
        sprintf(buffer, "ERROR File exists");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    strcpy(files[file_count].filename, filename);
    files[file_count].content[0] = '\0';
    files[file_count].lock_count = 0;
    files[file_count].has_backup = 0;
    files[file_count].checkpoint_count = 0;
    file_count++;
    
    pthread_mutex_unlock(&file_mutex);
    
    save_file_to_disk(filename, "");
    
    sprintf(buffer, "ACK Created");
    send(sock, buffer, strlen(buffer), 0);
    
    log_message("SS", "File created");
}

void handle_delete(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    // Delete from disk
    char filepath[MAX_PATH + MAX_FILENAME];
#ifdef _WIN32
    snprintf(filepath, sizeof(filepath), "%s\\%s", root_path, filename);
#else
    snprintf(filepath, sizeof(filepath), "%s/%s", root_path, filename);
#endif
    remove(filepath);
    
    // Remove from memory
    for (int i = idx; i < file_count - 1; i++) {
        files[i] = files[i + 1];
    }
    file_count--;
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Deleted");
    send(sock, buffer, strlen(buffer), 0);
    
    log_message("SS", "File deleted");
}

void handle_read(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    
    if (idx < 0) {
        // File not in memory, try to load from disk
        char filepath[MAX_PATH + MAX_FILENAME];
#ifdef _WIN32
        snprintf(filepath, sizeof(filepath), "%s\\%s", root_path, filename);
#else
        snprintf(filepath, sizeof(filepath), "%s/%s", root_path, filename);
#endif
        
        FILE* f = fopen(filepath, "r");
        if (f) {
            fclose(f);
            // File exists on disk, add it to memory
            idx = file_count;
            strcpy(files[idx].filename, filename);
            files[idx].lock_count = 0;
            files[idx].has_backup = 0;
            files[idx].checkpoint_count = 0;
            load_file_from_disk(filename, files[idx].content);
            file_count++;
        } else {
            sprintf(buffer, "ERROR File not found");
            send(sock, buffer, strlen(buffer), 0);
            pthread_mutex_unlock(&file_mutex);
            return;
        }
    }
    
    load_file_from_disk(filename, files[idx].content);
    strcpy(buffer, files[idx].content);
    
    pthread_mutex_unlock(&file_mutex);
    
    send(sock, buffer, strlen(buffer), 0);
    
    log_message("SS", "File read");
}

void handle_stream(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        // File not in memory, try to load from disk
        char filepath[MAX_PATH + MAX_FILENAME];
#ifdef _WIN32
        snprintf(filepath, sizeof(filepath), "%s\\%s", root_path, filename);
#else
        snprintf(filepath, sizeof(filepath), "%s/%s", root_path, filename);
#endif
        
        FILE* f = fopen(filepath, "r");
        if (f) {
            fclose(f);
            // File exists on disk, add it to memory
            idx = file_count;
            strcpy(files[idx].filename, filename);
            files[idx].lock_count = 0;
            files[idx].has_backup = 0;
            files[idx].checkpoint_count = 0;
            load_file_from_disk(filename, files[idx].content);
            file_count++;
        } else {
            sprintf(buffer, "ERROR File not found");
            send(sock, buffer, strlen(buffer), 0);
            pthread_mutex_unlock(&file_mutex);
            return;
        }
    }
    
    load_file_from_disk(filename, files[idx].content);
    
    // Split content into words properly
    char* content = files[idx].content;
    pthread_mutex_unlock(&file_mutex);
    
    char word[256];
    int word_len = 0;
    
    for (int i = 0; content[i]; i++) {
        if (content[i] == ' ' || content[i] == '\n' || content[i] == '\t') {
            if (word_len > 0) {
                word[word_len] = '\0';
                send(sock, word, word_len, 0);
                usleep(100000); // 0.1 seconds
                send(sock, " ", 1, 0);
                word_len = 0;
            }
        } else {
            if (word_len < 255) {
                word[word_len++] = content[i];
            }
        }
    }
    
    // Send last word if any
    if (word_len > 0) {
        word[word_len] = '\0';
        send(sock, word, word_len, 0);
        usleep(100000);
    }
    
    sprintf(buffer, "\nSTOP");
    send(sock, buffer, strlen(buffer), 0);
    
    log_message("SS", "File streamed");
}

void handle_write_start(int sock, const char* filename, int sentence_idx, const char* username) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        // File not in memory, try to load from disk
        char filepath[MAX_PATH + MAX_FILENAME];
#ifdef _WIN32
        snprintf(filepath, sizeof(filepath), "%s\\%s", root_path, filename);
#else
        snprintf(filepath, sizeof(filepath), "%s/%s", root_path, filename);
#endif
        
        FILE* f = fopen(filepath, "r");
        if (f) {
            fclose(f);
            // File exists on disk, add it to memory
            idx = file_count;
            strcpy(files[idx].filename, filename);
            files[idx].lock_count = 0;
            files[idx].has_backup = 0;
            files[idx].checkpoint_count = 0;
            load_file_from_disk(filename, files[idx].content);
            file_count++;
        } else {
            sprintf(buffer, "ERROR File not found");
            send(sock, buffer, strlen(buffer), 0);
            pthread_mutex_unlock(&file_mutex);
            return;
        }
    }
    
    load_file_from_disk(filename, files[idx].content);
    
    char sentences[100][MAX_SENTENCE_LENGTH];
    int sent_count = 0;
    split_into_sentences(files[idx].content, sentences, &sent_count);
    
    if (sentence_idx < 0 || sentence_idx > sent_count) {
        sprintf(buffer, "ERROR Sentence index out of range");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    // Check if THIS SPECIFIC sentence is already locked
    for (int i = 0; i < files[idx].lock_count; i++) {
        if (files[idx].sentence_locks[i].sentence_idx == sentence_idx) {
            sprintf(buffer, "ERROR Sentence %d is locked by user '%s'", 
                    sentence_idx, files[idx].sentence_locks[i].lock_holder);
            send(sock, buffer, strlen(buffer), 0);
            pthread_mutex_unlock(&file_mutex);
            return;
        }
    }
    
    // Check if we can add more locks
    if (files[idx].lock_count >= MAX_SENTENCE_LOCKS) {
        sprintf(buffer, "ERROR Maximum concurrent write sessions reached");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    // Lock this specific sentence
    files[idx].sentence_locks[files[idx].lock_count].sentence_idx = sentence_idx;
    strcpy(files[idx].sentence_locks[files[idx].lock_count].lock_holder, username);
    files[idx].lock_count++;
    
    // Backup only on first write session
    if (!files[idx].has_backup) {
        strcpy(files[idx].backup_content, files[idx].content);
        files[idx].has_backup = 1;
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Lock acquired for sentence %d", sentence_idx);
    send(sock, buffer, strlen(buffer), 0);
    
    char log_msg[256];
    sprintf(log_msg, "Write lock acquired on sentence %d of file %s", sentence_idx, filename);
    log_message("SS", log_msg);
}

void handle_write_update(int sock, const char* filename, int word_idx, const char* content) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0 || files[idx].lock_count == 0) {
        sprintf(buffer, "ERROR Invalid state - no active write session");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    // Find which sentence is locked for this session (use the last lock added)
    // In a real implementation, you'd track which lock belongs to which connection
    int sent_idx = files[idx].sentence_locks[files[idx].lock_count - 1].sentence_idx;
    
    char sentences[100][MAX_SENTENCE_LENGTH];
    int sent_count = 0;
    split_into_sentences(files[idx].content, sentences, &sent_count);
    
    if (sent_idx >= sent_count) {
        // Appending new sentence at the end
        if (strlen(files[idx].content) > 0) {
            // Add space if content doesn't end with space
            if (files[idx].content[strlen(files[idx].content)-1] != ' ') {
                strcat(files[idx].content, " ");
            }
        }
        strcat(files[idx].content, content);
    } else {
        char words[100][256];
        int word_count = 0;
        split_into_words(sentences[sent_idx], words, &word_count);
        
        // 1-based word indexing as per spec
        if (word_idx < 1 || word_idx > word_count + 1) {
            sprintf(buffer, "ERROR Word index out of range");
            send(sock, buffer, strlen(buffer), 0);
            pthread_mutex_unlock(&file_mutex);
            return;
        }
        
        // Rebuild sentence with new word inserted at position word_idx (1-based)
        char new_sentence[MAX_SENTENCE_LENGTH] = "";
        for (int i = 0; i < word_idx - 1; i++) {
            if (i > 0) strcat(new_sentence, " ");
            strcat(new_sentence, words[i]);
        }
        if (word_idx > 1) strcat(new_sentence, " ");
        strcat(new_sentence, content);
        for (int i = word_idx - 1; i < word_count; i++) {
            strcat(new_sentence, " ");
            strcat(new_sentence, words[i]);
        }
        
        // Rebuild full content - replace the modified sentence
        char new_content[MAX_BUFFER] = "";
        for (int i = 0; i < sent_count; i++) {
            if (i == sent_idx) {
                strcat(new_content, new_sentence);
            } else {
                strcat(new_content, sentences[i]);
            }
            // Add space after each sentence (they already have their delimiters)
            if (i < sent_count - 1) {
                strcat(new_content, " ");
            }
        }
        
        strcpy(files[idx].content, new_content);
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Updated");
    send(sock, buffer, strlen(buffer), 0);
}

void handle_write_end(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    // Release the last lock (in real implementation, you'd track which lock belongs to this connection)
    if (files[idx].lock_count > 0) {
        files[idx].lock_count--;
    }
    
    // Save to disk
    save_file_to_disk(filename, files[idx].content);
    
    // Update backup to current content for next UNDO (captures state before the NEXT write)
    strcpy(files[idx].backup_content, files[idx].content);
    files[idx].has_backup = 1;
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Write Successful!");
    send(sock, buffer, strlen(buffer), 0);
    
    log_message("SS", "Write completed and lock released");
}

void handle_undo(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    if (!files[idx].has_backup) {
        sprintf(buffer, "ERROR No changes to undo");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    strcpy(files[idx].content, files[idx].backup_content);
    save_file_to_disk(filename, files[idx].content);
    files[idx].has_backup = 0;
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Undo Successful!");
    send(sock, buffer, strlen(buffer), 0);
    
    log_message("SS", "Undo completed");
}

void handle_getfile(int sock, const char* filename) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    load_file_from_disk(filename, files[idx].content);
    strcpy(buffer, files[idx].content);
    
    pthread_mutex_unlock(&file_mutex);
    
    send(sock, buffer, strlen(buffer), 0);
}

void handle_checkpoint(int sock, const char* filename, const char* tag) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    if (files[idx].checkpoint_count >= MAX_CHECKPOINTS) {
        sprintf(buffer, "ERROR Maximum checkpoints reached");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    load_file_from_disk(filename, files[idx].content);
    
    int cp_idx = files[idx].checkpoint_count;
    strcpy(files[idx].checkpoints[cp_idx].tag, tag);
    strcpy(files[idx].checkpoints[cp_idx].content, files[idx].content);
    files[idx].checkpoints[cp_idx].timestamp = time(NULL);
    files[idx].checkpoint_count++;
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Checkpoint created successfully!");
    send(sock, buffer, strlen(buffer), 0);
    log_message("SS", "Checkpoint created");
}

void handle_viewcheckpoint(int sock, const char* filename, const char* tag) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    int found = -1;
    for (int i = 0; i < files[idx].checkpoint_count; i++) {
        if (strcmp(files[idx].checkpoints[i].tag, tag) == 0) {
            found = i;
            break;
        }
    }
    
    if (found < 0) {
        sprintf(buffer, "ERROR Checkpoint not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    strcpy(buffer, files[idx].checkpoints[found].content);
    pthread_mutex_unlock(&file_mutex);
    
    send(sock, buffer, strlen(buffer), 0);
}

void handle_revert(int sock, const char* filename, const char* tag) {
    char buffer[MAX_BUFFER];
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    int found = -1;
    for (int i = 0; i < files[idx].checkpoint_count; i++) {
        if (strcmp(files[idx].checkpoints[i].tag, tag) == 0) {
            found = i;
            break;
        }
    }
    
    if (found < 0) {
        sprintf(buffer, "ERROR Checkpoint not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    strcpy(files[idx].content, files[idx].checkpoints[found].content);
    save_file_to_disk(filename, files[idx].content);
    
    pthread_mutex_unlock(&file_mutex);
    
    sprintf(buffer, "ACK Reverted to checkpoint successfully!");
    send(sock, buffer, strlen(buffer), 0);
    log_message("SS", "Checkpoint reverted");
}

void handle_listcheckpoints(int sock, const char* filename) {
    char buffer[MAX_BUFFER] = {0};
    
    pthread_mutex_lock(&file_mutex);
    
    int idx = find_file(filename);
    if (idx < 0) {
        sprintf(buffer, "ERROR File not found");
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&file_mutex);
        return;
    }
    
    if (files[idx].checkpoint_count == 0) {
        strcpy(buffer, "(no checkpoints)\n");
    } else {
        for (int i = 0; i < files[idx].checkpoint_count; i++) {
            char line[256];
            char time_str[64];
            struct tm* t = localtime(&files[idx].checkpoints[i].timestamp);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", t);
            sprintf(line, "--> %s (%s)\n", files[idx].checkpoints[i].tag, time_str);
            strcat(buffer, line);
        }
    }
    
    pthread_mutex_unlock(&file_mutex);
    
    send(sock, buffer, strlen(buffer), 0);
}

void* nm_handler(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    
    while (1) {
        char buffer[MAX_BUFFER];
        int bytes = recv(sock, buffer, MAX_BUFFER, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = '\0';
        
        char command[64], filename[MAX_FILENAME];
        
        sscanf(buffer, "%s", command);
        
        if (strcmp(command, "CREATE") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_create(sock, filename);
        } else if (strcmp(command, "DELETE") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_delete(sock, filename);
        } else if (strcmp(command, "GETFILE") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_getfile(sock, filename);
        }
    }
    
    close(sock);
    return NULL;
}

void* client_handler(void* arg) {
    int sock = *(int*)arg;
    free(arg);
    
    char buffer[MAX_BUFFER];
    char command[64], filename[MAX_FILENAME], content[MAX_BUFFER];
    int idx;
    int in_write_session = 0;
    
    // Keep connection open for WRITE sessions
    while (1) {
        int bytes = recv(sock, buffer, MAX_BUFFER, 0);
        if (bytes <= 0) {
            break;  // Connection closed
        }
        
        buffer[bytes] = '\0';
        sscanf(buffer, "%s", command);
        
        // Handle CREATE/DELETE commands from naming server
        if (strcmp(command, "CREATE") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_create(sock, filename);
            break;  // Single command, close connection
        } else if (strcmp(command, "DELETE") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_delete(sock, filename);
            break;  // Single command, close connection
        } else if (strcmp(command, "GETFILE") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_getfile(sock, filename);
            break;  // Single command, close connection
        } else if (strcmp(command, "read") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_read(sock, filename);
            break;  // Single command, close connection
        } else if (strcmp(command, "STREAM") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_stream(sock, filename);
            break;  // Single command, close connection
        } else if (strcmp(command, "WRITE_START") == 0) {
            char write_username[MAX_USERNAME];
            sscanf(buffer, "%s %s %d %s", command, filename, &idx, write_username);
            handle_write_start(sock, filename, idx, write_username);
            in_write_session = 1;  // Entered write session, keep connection open
        } else if (strcmp(command, "WRITE_UPDATE") == 0) {
            char* ptr = buffer + strlen("WRITE_UPDATE ");
            char* fname_end = strchr(ptr, ' ');
            if (fname_end) {
                int fname_len = fname_end - ptr;
                strncpy(filename, ptr, fname_len);
                filename[fname_len] = '\0';
                ptr = fname_end + 1;
                
                sscanf(ptr, "%d", &idx);
                ptr = strchr(ptr, ' ');
                if (ptr) {
                    strcpy(content, ptr + 1);
                    handle_write_update(sock, filename, idx, content);
                }
            }
        } else if (strcmp(command, "WRITE_END") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_write_end(sock, filename);
            break;  // Write session ended, close connection
        } else if (strcmp(command, "UNDO") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_undo(sock, filename);
            break;  // Single command, close connection
        } else if (strcmp(command, "CHECKPOINT") == 0) {
            char tag[MAX_USERNAME];
            sscanf(buffer, "%s %s %s", command, filename, tag);
            handle_checkpoint(sock, filename, tag);
            break;  // Single command, close connection
        } else if (strcmp(command, "VIEWCHECKPOINT") == 0) {
            char tag[MAX_USERNAME];
            sscanf(buffer, "%s %s %s", command, filename, tag);
            handle_viewcheckpoint(sock, filename, tag);
            break;  // Single command, close connection
        } else if (strcmp(command, "REVERT") == 0) {
            char tag[MAX_USERNAME];
            sscanf(buffer, "%s %s %s", command, filename, tag);
            handle_revert(sock, filename, tag);
            break;  // Single command, close connection
        } else if (strcmp(command, "LISTCHECKPOINTS") == 0) {
            sscanf(buffer, "%s %s", command, filename);
            handle_listcheckpoints(sock, filename);
            break;  // Single command, close connection
        } else {
            break;  // Unknown command, close connection
        }
    }
    
    close(sock);
    return NULL;
}

void scan_files() {
#ifdef _WIN32
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    char search_path[MAX_PATH + 4];
    snprintf(search_path, sizeof(search_path), "%s\\*", root_path);
    
    hFind = FindFirstFile(search_path, &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    
    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (file_count >= MAX_FILES) break;
            strcpy(files[file_count].filename, findData.cFileName);
            files[file_count].lock_count = 0;
            files[file_count].has_backup = 0;
            load_file_from_disk(findData.cFileName, files[file_count].content);
            file_count++;
        }
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
#else
    DIR* dir = opendir(root_path);
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            if (file_count >= MAX_FILES) break;
            strcpy(files[file_count].filename, entry->d_name);
            files[file_count].lock_count = 0;
            files[file_count].has_backup = 0;
            load_file_from_disk(entry->d_name, files[file_count].content);
            file_count++;
        }
    }
    closedir(dir);
#endif
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        printf("Usage: %s <nm_ip> <nm_port> <client_port> <root_path>\n", argv[0]);
        return 1;
    }
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif
    
    char* nm_ip = argv[1];
    int nm_port = atoi(argv[2]);
    int client_port = atoi(argv[3]);
    int nm_comm_port = client_port;  // Use same port for both client and NM connections
    strcpy(root_path, argv[4]);
    
    // Create root directory if it doesn't exist
    mkdir(root_path, 0777);
    
    // Scan existing files
    scan_files();
    
    // Register with naming server
    int nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);
    inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr);
    
    if (connect(nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Cannot connect to naming server");
        return 1;
    }
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "REGISTER_SS 127.0.0.1 %d %d %s %d", 
            nm_comm_port, client_port, root_path, file_count);
    send(nm_sock, buffer, strlen(buffer), 0);
    
    // Small delay to ensure naming server processed the registration message
    usleep(50000);  // 50ms delay
    
    // Send file list
    for (int i = 0; i < file_count; i++) {
        send(nm_sock, files[i].filename, strlen(files[i].filename), 0);
        usleep(100000);  // Increased delay between files to 100ms
    }
    
    int bytes = recv(nm_sock, buffer, MAX_BUFFER, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Registration: %s\n", buffer);
    }
    
    // Start NM connection handler
    int* nm_sock_ptr = malloc(sizeof(int));
    *nm_sock_ptr = nm_sock;
    pthread_t nm_thread;
    pthread_create(&nm_thread, NULL, nm_handler, nm_sock_ptr);
    pthread_detach(nm_thread);
    
    // Start client server
    int client_server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(client_server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(client_port);
    
    if (bind(client_server, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        return 1;
    }
    
    listen(client_server, 50);
    printf("Storage Server started on client port %d\n", client_port);
    log_message("SS", "Storage Server started");
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int* client_sock = malloc(sizeof(int));
        *client_sock = accept(client_server, (struct sockaddr*)&client_addr, &client_len);
        
        if (*client_sock < 0) {
            free(client_sock);
            continue;
        }
        
        pthread_t thread;
        pthread_create(&thread, NULL, client_handler, client_sock);
        pthread_detach(thread);
    }
    
    return 0;
}