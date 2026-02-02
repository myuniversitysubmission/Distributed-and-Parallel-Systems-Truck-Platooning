# Distributed-and-Parallel-Systems-Truck-Platooning

22-01-2026

- 'n' clients can connect & interact with the server.
- Server has pthreads implemented, each client joining will spawn into a new thread.
- While master thread has a non blocking accept function, it can accept as well as perfrom its other funcctions.

## How to run
**To compile:**
```
    gcc -Iinclude -DBUILD_SERVER src/server.c src/frame.c src/queue.c src/logical_clock.c -o server.exe -lws2_32

    gcc -Iinclude -DBUILD_CLIENT src/client.c src/frame.c src/logical_clock.c src/queue.c -o client.exe -lws2_32                                ```
**To run:**
```
    ./server.exe
    ./client.exe 21
```
- This 21 is the message we want to send, it can be of any value 'or' text. *[Ex: 21, Hello, etc.. ]

**To run test suit:**
```
    gcc -Iinclude src/frame.c src/queue.c src/client.c src/server.c tests/tests.c src/logical_clock.c -o tests.exe -lcunit -lpthread -lws2_32
 
```
# Changes
28-01-2026
- Including maximum no of clients=10
- Client ID is not randomised, as each client joins - indivitual client id is assigned.
- If a client leaves, its space will be empty. 
- A new truck will reuse this ID now. New truck's position would be the max no of clients. 
- ID of following trucks remains same. Position of its following trucks moves up. 
  
- Any number of cliets can be created (Max 40 clients)
