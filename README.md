[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

# LangOS Distributed File System

A distributed file system implementation supporting concurrent file operations, access control, and multi-client interactions.

## Features

- **File Operations**: CREATE, READ, WRITE, DELETE, STREAM
- **Access Control**: Owner-based permissions with read/write access management
- **Concurrent Access**: Multiple clients can read/write files simultaneously with sentence-level locking
- **Efficient Search**: Trie-based file search for O(m) lookup where m = filename length
- **Logging**: Comprehensive system logging for debugging and monitoring
- **UNDO Support**: Revert last changes to files
- **EXEC Support**: Execute file contents as shell commands

## Building
```bash
make
```

This creates three executables:
- `naming_server` - Central coordination server
- `storage_server` - Storage server for file persistence
- `client` - User client interface

## Running the System

### 1. Start the Naming Server
```bash
./naming_server 8080
```

### 2. Start Storage Servers
```bash
# Terminal 2
mkdir -p storage1
./storage_server 127.0.0.1 8080 9001 storage1

# Terminal 3
mkdir -p storage2
./storage_server 127.0.0.1 8080 9002 storage2
```

### 3. Start Clients
```bash
# Terminal 4
./client 127.0.0.1 8080
# Enter username when prompted (e.g., user1)

# Terminal 5
./client 127.0.0.1 8080
# Enter username when prompted (e.g., user2)
```

## Using Makefile Targets
```bash
# Terminal 1
make run_nm

# Terminal 2
make run_ss1

# Terminal 3
make run_ss2

# Terminal 4
make run_client
```

## Commands

### View Files
```
VIEW           # List files you have access to
VIEW -a        # List all files in system
VIEW -l        # List with details (words, chars, timestamps)
VIEW -al       # List all files with details
```

### File Operations
```
CREATE <filename>              # Create new file
READ <filename>                # Read file contents
DELETE <filename>              # Delete file (owner only)
INFO <filename>                # Get file metadata
STREAM <filename>              # Stream file word-by-word
```

### Writing to Files
```
WRITE <filename> <sentence_idx>
<word_idx> <content>           # Insert/update words
ETIRW                          # End write session
```

Example:
```
WRITE test.txt 0
1 Hello
3 world!
ETIRW
```

### Access Control
```
ADDACCESS -R <filename> <username>   # Grant read access
ADDACCESS -W <filename> <username>   # Grant write access
REMACCESS <filename> <username>      # Remove access
```

### Other Commands
```
LIST                           # List all users
UNDO <filename>                # Undo last change
EXEC <filename>                # Execute file as shell commands
EXIT                           # Quit client
```

## Example Usage Session
```bash
# User1 creates a file
Client> CREATE hello.txt
File Created Successfully!

# User1 writes to it
Client> WRITE hello.txt 0
Client: 1 Hello World.
Client: ETIRW
Write Successful!

# User1 reads it
Client> READ hello.txt
Hello World.

# User1 grants access to user2
Client> ADDACCESS -R hello.txt user2
Access granted successfully!

# User2 can now read
Client> READ hello.txt
Hello World.

# User1 executes a script file
Client> CREATE script.txt
Client> WRITE script.txt 0
Client: 1 echo "Hello from script"
Client: 2 ls
Client: ETIRW
Client> EXEC script.txt
Hello from script
hello.txt
script.txt
```

## Architecture

### Naming Server
- Central coordinator managing file metadata
- Routes requests between clients and storage servers
- Implements efficient file search using Trie data structure
- Manages access control lists
- Handles user registration and tracking

### Storage Server
- Stores actual file data on disk
- Handles concurrent read/write operations
- Implements sentence-level locking for writes
- Maintains backup for UNDO functionality
- Supports streaming operations

### Client
- User interface for file operations
- Connects to naming server for coordination
- Establishes direct connections to storage servers for data operations
- Supports interactive write sessions

## Error Handling

The system handles various error conditions:
- File not found
- Permission denied
- File already exists
- Sentence/word index out of range
- Storage server unavailable
- Sentence locked by another user

## Logging

All operations are logged to `system.log` with timestamps and component identification:
```
[2025-10-21 14:30:15] [NM] Client registered: user1
[2025-10-21 14:30:20] [SS] File created
[2025-10-21 14:30:25] [NM] Access granted
```

## Data Persistence

- Files are stored persistently in storage server directories
- File metadata maintained by naming server in memory
- Backup copies created before write operations for UNDO support

## Concurrent Access

- Multiple clients can read the same file simultaneously
- Write operations lock specific sentences
- Other sentences in the same file remain accessible
- UNDO maintains per-file history (not per-user)

## Technical Details

- **Protocol**: TCP sockets
- **Threading**: POSIX threads for concurrent client handling
- **Search Complexity**: O(m) where m = filename length (using Trie)
- **Language**: C with POSIX libraries
- **Concurrency Control**: Mutex locks for shared data structures

## Limitations

- Single naming server (no fault tolerance for NM)
- One level of UNDO (last operation only)
- Text files only
- Sentence delimiters: . ! ?
- No authentication/encryption (usernames are trusted)

## Clean Up
```bash
make clean
```

This removes:
- All compiled binaries
- Log files
- Storage directories

## Notes

- Naming server must be started before storage servers
- Storage servers register automatically on startup
- Each file is stored on exactly one storage server
- Load balancing distributes files across available storage servers
- System uses simple round-robin for load distribution

## Troubleshooting

**Connection Refused**: Ensure naming server is running before starting storage servers or clients

**Permission Denied**: Check file ownership and access rights using INFO command

**File Not Found**: Use VIEW -a to see all files in the system

**Port Already in Use**: Change port numbers or kill existing processes
