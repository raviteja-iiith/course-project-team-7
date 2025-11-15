#include "common.h"
#include <signal.h>

char username[MAX_USERNAME];
char nm_ip[32];
int nm_port;
volatile sig_atomic_t running = 1;

void handle_sigint(int sig) {
    printf("\n\nDisconnecting from LangOS Distributed File System...\n");
    running = 0;
}

int connect_to_nm() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(nm_port);
    inet_pton(AF_INET, nm_ip, &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Cannot connect to naming server");
        return -1;
    }
    
    return sock;
}

int connect_to_ss(const char* ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Cannot connect to storage server");
        return -1;
    }
    
    return sock;
}

void cmd_view(char* flags) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "VIEW %s %s", username, flags);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s", buffer);
    }
    
    close(sock);
}

void cmd_create(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "CREATE %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(sock);
}

void cmd_delete(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "DELETE %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(sock);
}

void cmd_read(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "READ %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    // Connect to SS
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "read %s", filename);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer);
    }
    
    close(ss_sock);
}

void cmd_write(char* filename, int sentence_idx) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "WRITE %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    if (bytes <= 0) {
        printf("No response from naming server\n");
        close(sock);
        return;
    }
    buffer[bytes] = '\0';
    
    printf("Debug: NM response: '%s'\n", buffer);
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    if (sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port) != 2) {
        printf("Invalid SS_INFO format: %s\n", buffer);
        close(sock);
        return;
    }
    
    printf("Debug: Connecting to SS at %s:%d\n", ss_ip, ss_port);
    close(sock);
    
    // Connect to SS
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    // Start write
    sprintf(buffer, "WRITE_START %s %d", filename, sentence_idx);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(ss_sock);
        return;
    }
    
    // Read write commands
    char line[MAX_BUFFER];
    while (1) {
        printf("Client: ");
        if (!fgets(line, sizeof(line), stdin)) break;
        
        line[strcspn(line, "\n")] = 0;
        
        if (strcmp(line, "ETIRW") == 0) {
            sprintf(buffer, "WRITE_END %s", filename);
            send(ss_sock, buffer, strlen(buffer), 0);
            
            bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                if (strncmp(buffer, "ACK", 3) == 0) {
                    printf("%s\n", buffer + 4);
                } else {
                    printf("%s\n", buffer);
                }
            } else {
                printf("Write Successful!\n");
            }
            break;
        }
        
        int word_idx;
        char content[MAX_BUFFER];
        if (sscanf(line, "%d %[^\n]", &word_idx, content) == 2) {
            sprintf(buffer, "WRITE_UPDATE %s %d %s", filename, word_idx, content);
            send(ss_sock, buffer, strlen(buffer), 0);
            
            bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
            buffer[bytes] = '\0';
            
            if (strncmp(buffer, "ERROR", 5) == 0) {
                printf("%s\n", buffer + 6);
            }
        }
    }
    
    close(ss_sock);
}

void cmd_info(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "INFO %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s", buffer);
    }
    
    close(sock);
}

void cmd_stream(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "STREAM %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    // Connect to SS
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "STREAM %s", filename);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    // Receive streaming content
    while (1) {
        bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = '\0';
        
        if (strstr(buffer, "STOP")) {
            printf("\n");
            break;
        }
        
        printf("%s", buffer);
        fflush(stdout);
    }
    
    close(ss_sock);
}

void cmd_list() {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "LIST %s", username);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    printf("%s", buffer);
    
    close(sock);
}

void cmd_addaccess(char* type, char* filename, char* target_user) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "ADDACCESS %s %s %s %s", username, type, filename, target_user);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(sock);
}

void cmd_remaccess(char* filename, char* target_user) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "REMACCESS %s %s %s", username, filename, target_user);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(sock);
}

void cmd_exec(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "EXEC %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s", buffer);
    }
    
    close(sock);
}

void cmd_createfolder(char* foldername) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "CREATEFOLDER %s %s", username, foldername);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(sock);
}

void cmd_move(char* filename, char* foldername) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "MOVE %s %s %s", username, filename, foldername);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(sock);
}

void cmd_viewfolder(char* foldername) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "VIEWFOLDER %s %s", username, foldername);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    printf("%s", buffer);
    
    close(sock);
}

void cmd_checkpoint(char* filename, char* tag) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "WRITE %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "CHECKPOINT %s %s", filename, tag);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(ss_sock);
}

void cmd_viewcheckpoint(char* filename, char* tag) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "READ %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "VIEWCHECKPOINT %s %s", filename, tag);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer);
    }
    
    close(ss_sock);
}

void cmd_revert(char* filename, char* tag) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "WRITE %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "REVERT %s %s", filename, tag);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(ss_sock);
}

void cmd_listcheckpoints(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "READ %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "LISTCHECKPOINTS %s", filename);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    printf("%s", buffer);
    
    close(ss_sock);
}

void cmd_undo(char* filename) {
    int sock = connect_to_nm();
    if (sock < 0) return;
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "UNDO %s %s", username, filename);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
        close(sock);
        return;
    }
    
    char ss_ip[32];
    int ss_port;
    sscanf(buffer, "SS_INFO %s %d", ss_ip, &ss_port);
    close(sock);
    
    // Connect to SS
    int ss_sock = connect_to_ss(ss_ip, ss_port);
    if (ss_sock < 0) return;
    
    sprintf(buffer, "UNDO %s", filename);
    send(ss_sock, buffer, strlen(buffer), 0);
    
    bytes = recv(ss_sock, buffer, MAX_BUFFER, 0);
    buffer[bytes] = '\0';
    
    if (strncmp(buffer, "ERROR", 5) == 0) {
        printf("%s\n", buffer + 6);
    } else {
        printf("%s\n", buffer + 4);
    }
    
    close(ss_sock);
}

void process_command(char* line) {
    char command[64], arg1[MAX_FILENAME], arg2[MAX_USERNAME], arg3[MAX_USERNAME];
    
    sscanf(line, "%s", command);
    
    if (strcmp(command, "VIEW") == 0) {
        char flags[16] = "";
        sscanf(line, "%s %s", command, flags);
        cmd_view(flags);
    } else if (strcmp(command, "CREATE") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_create(arg1);
    } else if (strcmp(command, "DELETE") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_delete(arg1);
    } else if (strcmp(command, "READ") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_read(arg1);
    } else if (strcmp(command, "WRITE") == 0) {
        int sent_idx;
        sscanf(line, "%s %s %d", command, arg1, &sent_idx);
        cmd_write(arg1, sent_idx);
    } else if (strcmp(command, "INFO") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_info(arg1);
    } else if (strcmp(command, "STREAM") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_stream(arg1);
    } else if (strcmp(command, "LIST") == 0) {
        cmd_list();
    } else if (strcmp(command, "ADDACCESS") == 0) {
        sscanf(line, "%s %s %s %s", command, arg1, arg2, arg3);
        cmd_addaccess(arg1, arg2, arg3);
    } else if (strcmp(command, "REMACCESS") == 0) {
        sscanf(line, "%s %s %s", command, arg1, arg2);
        cmd_remaccess(arg1, arg2);
    } else if (strcmp(command, "EXEC") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_exec(arg1);
    } else if (strcmp(command, "UNDO") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_undo(arg1);
    } else if (strcmp(command, "CREATEFOLDER") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_createfolder(arg1);
    } else if (strcmp(command, "MOVE") == 0) {
        sscanf(line, "%s %s %s", command, arg1, arg2);
        cmd_move(arg1, arg2);
    } else if (strcmp(command, "VIEWFOLDER") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_viewfolder(arg1);
    } else if (strcmp(command, "CHECKPOINT") == 0) {
        sscanf(line, "%s %s %s", command, arg1, arg2);
        cmd_checkpoint(arg1, arg2);
    } else if (strcmp(command, "VIEWCHECKPOINT") == 0) {
        sscanf(line, "%s %s %s", command, arg1, arg2);
        cmd_viewcheckpoint(arg1, arg2);
    } else if (strcmp(command, "REVERT") == 0) {
        sscanf(line, "%s %s %s", command, arg1, arg2);
        cmd_revert(arg1, arg2);
    } else if (strcmp(command, "LISTCHECKPOINTS") == 0) {
        sscanf(line, "%s %s", command, arg1);
        cmd_listcheckpoints(arg1);
    } else if (strcmp(command, "EXIT") == 0 || strcmp(command, "QUIT") == 0) {
        exit(0);
    } else {
        printf("Unknown command: %s\n", command);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Usage: %s <nm_ip> <nm_port>\n", argv[0]);
        return 1;
    }
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }
#endif
    
    strcpy(nm_ip, argv[1]);
    nm_port = atoi(argv[2]);
    
    printf("Enter username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;
    
    // Register with naming server
    printf("Connecting to naming server at %s:%d...\n", nm_ip, nm_port);
    int sock = connect_to_nm();
    if (sock < 0) {
        printf("Failed to connect to naming server\n");
        return 1;
    }
    printf("Connected successfully!\n");
    
    char buffer[MAX_BUFFER];
    sprintf(buffer, "REGISTER_CLIENT %s 127.0.0.1 0", username);
    printf("Sending registration: %s\n", buffer);
    send(sock, buffer, strlen(buffer), 0);
    
    int bytes = recv(sock, buffer, MAX_BUFFER - 1, 0);
    if (bytes <= 0) {
        printf("Failed to receive registration response\n");
        close(sock);
        return 1;
    }
    buffer[bytes] = '\0';
    printf("%s\n", buffer);
    close(sock);
    
    // Install signal handler for Ctrl+C
    running = 1;  // Ensure running flag is set
    signal(SIGINT, handle_sigint);
    
    printf("Connected to LangOS Distributed File System\n");
    printf("Type commands (EXIT to quit, Ctrl+C to disconnect)\n\n");
    
    char line[MAX_BUFFER];
    while (running) {
        printf("Client> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            if (running) {
                // EOF encountered, break gracefully
                break;
            }
        }
        
        if (!running) break; // Check if signal was received
        
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        
        process_command(line);
    }
    
    printf("\nGoodbye!\n");
    
    return 0;
}