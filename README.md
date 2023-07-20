# remote-procedure-call


https://github.com/lohchness/remote-procedure-call/assets/50405970/0a9fbdd7-6933-4a6c-a1c6-4ad6ff6252d4


# Overview and Architecture

Remote Procedure Call (RPC) is a crucial technology in distributed computing that enables software applications to communicate with each other seamlessly over a network. It provides a way for a client to call a function on a remote server as if it were a local function call. This abstraction allows developers to build distributed systems and applications that span multiple machines and platforms.

This is a custom RPC system that allows computations to be split seamlessly between multiple computers with different architectures (i.e.: server runs on Linux, client runs on Windows). There may be many clients to one server.

The underlying principles of RPC still applies even though the system may differ from other standard RPC systems.

# Details

## API

The API is implemented by `rpc.c`.

### Data Structures

The API sends and receives data structures of the form:

```c
typedef struct {
	int data1;
	size_t data2_len;
	void *data2;
} rpc_data;
```
 - `data1` is an integer. The purpose of this is to allow simple functions that only passes an integer to avoid memory management issues, by setting `data2_len = 0` and `data2 = NULL`. 
- `data2` is a block of bytes of `data2_len` bytes. `data2_len` may be 64 bits long.


The handler that implements the actual remote procedure will have functions of the signature:

`rpc_data *procedure_name(const rpc_data *d);`

This takes a pointer to an `rpc_data` object and returns a pointer to another `rpc_data` object. This function will allocate memory for the `rpc_data` structure and its `data2` field. The server can have any kind of implementation in its body as long as it follows the format above. 

## Server-side API

- `rpc_init_server` initializes the server, creating and binding the sockets, and listens to any incoming connections.

- `rpc_register` lets the subsystem know which function to call when an incoming request is receives. There is no limit to the number of functions to be registered.

- `rpc_serve_all` waits for incoming connections on the specified port for any of the registered functions, or `rpc_find`. This will not usually return and will sit and wait for new requests, only returning on a SIGINT.

## Client-side API

- `rpc_init_client` initializes the client and connects to the server. The server details are known beforehand.

- `rpc_close_client` closes the connection between itself and the server and frees up any allocated memory.

- `rpc_find` tells the subsystem what details are required to place a call. The return value is a handle for the remote procedure used for `rpc_call`.

- `rpc_call` uses the subsystem to run the remote procedure and returns the value.

# Portability and safety

To be usable on machines with different architectures, I had to convert 64-bit ints to network byte order and vice versa when sending and receiving data from one machine to another. This is because endian orders may be defined differently. The functions `uint64_t htonll(uint64_t value)` and `uint64_t ntohll(uint64_t value)` in `rpc.c` fixes this issue.

I have also provided safety features and failure handling in case any data has been corrupted mid-transmission. In most cases, the host will send a failure bit to the other side and/or close the connection.

There are also safeguards against request flooding, client/server disconnects, port conflicts, random data, binary characters, and switching threads.

# Usage

- A makefile is provided to compile the client and server.

To run:

`./rpc-server -p <port>`

`./rpc-client -i <ip-address> -p <port>`

where:

- `ip_address` is the IPv6 address of the machine on which the server is running
- `port` is the TCP port number of the server.

This can be run on any two machines as long as the IPv6 and port number is known.

Example output:

Client:

```
rpc_init_client: instance 0, addr ::1, port 6000
rpc_find: instance 0, add2
rpc_find: instance 0, returned handle for function add2
rpc_find: instance 0, echo2
rpc_find: instance 0, returned handle for function echo2
rpc_call: instance 0, calling echo2, data1 = 0, data2 sha256 = 55a10ed...
rpc_call: instance 0, call of echo2 received data1 = 0, data2 sha256 = 55a10ed
rpc_call: instance 0, calling add2, with arguments 80 -40...
rpc_call: instance 0, call of add2 received result 40
rpc_close_client: instance 0
```

Server:

```
rpc_init_server: instance 0, port 6000
rpc_register: instance 0, echo2 (handler) as echo2
rpc_register: instance 0, add2 (handler) as add2
rpc_serve_all: instance 0
handler echo2: data1 0, data2 sha256 55a10ed
handler add2_i8: arguments 80 and -40
```


# Extensions

To extend this project even further, I could build upon the foundations of my code and pivot towards a file sharing service between peers or between 2 clients through a central server. The `data2` field could contain the bytes of a file, and then transfer this file to the other side.

There is also the possibility of a simple chat service (private messages and group messages) that could also be centralized or decentralized. Of course, I would also have to implement authorization and encryption in my code for both extensions to provide security.
