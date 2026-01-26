# Distributed-and-Parallel-Systems-Truck-Platooning

22-01-2026

- 'n' clients can connect & interact with the server.
- Server has pthreads implemented, each client joining will spawn into a new thread.
- While master thread has a non blocking accept function, it can accept as well as perfrom its other funcctions.

## How to run
**To compile:**
```
    gcc server.c frame.c -o server.exe -lws2_32
    gcc client.c frame.c -o client.exe -lws2_32
```
**To run:**
```
    ./server.exe
    ./client.exe 21
 ```

- This 21 is the message we want to send, it can be of any value 'or' text. *[Ex: 21, Hello, etc.. ]*
- Any number of cliets can be created (Max 40 clients)