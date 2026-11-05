# LangOS Distributed File System

A concurrent distributed document store implemented in C with a central naming server, multiple storage servers, and interactive clients.

## System design

```text
                 ┌─────────────────┐
                 │  Naming Server  │
                 │ metadata + ACLs │
                 └───────┬─────────┘
                         │ TCP
          ┌──────────────┴──────────────┐
          ▼                             ▼
   ┌──────────────┐              ┌──────────────┐
   │ Storage 9001 │              │ Storage 9002 │
   │ files + locks│              │ files + locks│
   └──────┬───────┘              └──────┬───────┘
          ▲                             ▲
          └──────────────┬──────────────┘
                         │
                    ┌────┴────┐
                    │ Clients │
                    └─────────┘
```

The naming server owns placement and metadata. Clients first resolve a file through the naming server, then connect directly to the selected storage server for data operations. This keeps coordination traffic separate from file-content traffic.

## Core capabilities

- TCP socket communication between clients, the naming server, and storage servers.
- File lifecycle operations: `CREATE`, `READ`, `WRITE`, `DELETE`, `STREAM`, and `INFO`.
- Owner-based permissions with separate read and write access grants.
- Sentence-level write locks so independent sentences can be edited concurrently.
- Trie-backed filename lookup with a 50-entry timestamp-based LRU cache.
- Persistent storage-server files and naming-server metadata snapshots.
- Checkpoints and `UNDO` support for reverting the latest file change.
- `EXEC` support for running command contents stored in a file.
- Mutex-protected shared state and one worker thread per accepted connection.
- Timestamped component logs for naming-server and storage-server diagnostics.

## Build

```bash
make
```

This produces `naming_server`, `storage_server`, and `client`. The Makefile uses GCC, `-Wall`, debug symbols, and POSIX threads.

## Run a two-storage-server deployment

```bash
# Terminal 1
./naming_server 8080

# Terminal 2
mkdir -p storage1
./storage_server 127.0.0.1 8080 9001 storage1

# Terminal 3
mkdir -p storage2
./storage_server 127.0.0.1 8080 9002 storage2

# Terminal 4
./client 127.0.0.1 8080
```

Start a second client in another terminal to exercise concurrent access and sentence locking.

## Client command reference

```text
VIEW [-a] [-l]                 List accessible files and metadata
CREATE <filename>              Create a file
READ <filename>                Read file contents
WRITE <filename>               Edit words within locked sentences
STREAM <filename>              Stream file content
INFO <filename>                Inspect metadata
ADDACCESS -R|-W <file> <user>  Grant read or write access
REMACCESS <file> <user>        Revoke access
UNDO <filename>                Restore the latest checkpoint
EXEC <filename>                Execute stored shell commands
LIST                           List registered users
EXIT                           Close the client
```

During `WRITE`, a client locks only the selected sentence and ends the session with `ETIRW`. Other sentences remain available to other users.

## Correctness and trade-offs

The implementation makes concurrency explicit with mutexes around server registries, metadata, cache entries, and storage-server file state. File placement uses simple round-robin selection. The current design intentionally has a single naming-server coordinator, trusted usernames, text-file storage, and in-memory active metadata; these are clear extension points for replicated coordination, authentication, encryption, and durable metadata indexing.

## Cleanup

```bash
make clean
```

## Technologies

`C` · `POSIX sockets` · `pthreads` · `TCP` · `Trie` · `LRU cache` · `ACLs` · `concurrency control` · `persistent storage`
