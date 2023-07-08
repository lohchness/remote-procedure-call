CC=cc
RPC_SYSTEM=rpc.o
LDFLAGS = -lm

.PHONY: format all

all: $(RPC_SYSTEM) rpc-client rpc-server

$(RPC_SYSTEM): rpc.c rpc.h
	$(CC) -Wall -c -o $@ $< $(LDFLAGS)

rpc-client:
	gcc -Wall -o rpc-client client.c rpc.o -lm

rpc-server:
	gcc -Wall -o rpc-server server.c rpc.o -lm


testing:
	cc -Wall -c rpc.c
	cc -g -Wall -o test_server artifacts/server.a rpc.o -lm
	cc -g -Wall -o test_client artifacts/client.a rpc.o -lm


# rpc.o:
# 	gcc -Wall -c rpc.c

# RPC_SYSTEM_A=rpc.a
# $(RPC_SYSTEM_A): rpc.o
#	ar rcs $(RPC_SYSTEM_A) $(RPC_SYSTEM)

clean:
	rm -rf rpc-client rpc-client.o rpc-server rpc-server.o rpc rpc.o test_client test_server

format:
	clang-format -style=file -i *.c *.h
