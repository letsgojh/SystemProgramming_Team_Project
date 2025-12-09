##########################################################
# ChatFileSystem - Makefile
##########################################################

CC = gcc
CFLAGS = -Wall -O2 -pthread -Icommon

SERVER_DIR = server
CLIENT_DIR = client
COMMON_DIR = common

SERVER_TARGET = server_app
CLIENT_TARGET = client_app

# Source files (.c only!)
SERVER_SRCS = $(wildcard $(SERVER_DIR)/*.c)
CLIENT_SRCS = $(wildcard $(CLIENT_DIR)/*.c)
COMMON_SRCS = $(wildcard $(COMMON_DIR)/*.c)

SERVER_OBJS = $(SERVER_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
COMMON_OBJS = $(COMMON_SRCS:.c=.o)

# ncurses needed ONLY for client
CLIENT_LDFLAGS = -lncurses

all: $(SERVER_TARGET) $(CLIENT_TARGET)

##########################################################
# Server Build
##########################################################
$(SERVER_TARGET): $(SERVER_OBJS) $(COMMON_OBJS)
	@echo "🔧 Building server..."
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS) $(COMMON_OBJS)
	@echo "✅ Server build complete!"

##########################################################
# Client Build
##########################################################
$(CLIENT_TARGET): $(CLIENT_OBJS) $(COMMON_OBJS)
	@echo "🔧 Building client..."
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS) $(COMMON_OBJS) $(CLIENT_LDFLAGS) -lncursesw 
	@echo "✅ Client build complete!"

##########################################################
# Compilation Rules
##########################################################
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

##########################################################
# Utility Commands
##########################################################
clean:/
	@echo "🧹 Cleaning build files..."
	rm -f $(SERVER_DIR)/*.o $(CLIENT_DIR)/*.o $(COMMON_DIR)/*.o \
	      $(SERVER_TARGET) $(CLIENT_TARGET) \
	      $(SERVER_DIR)/*.txt $(CLIENT_DIR)/*.txt dummy.txt
	@echo "✅ Clean complete!"

run_server:
	./$(SERVER_TARGET)

run_client:
	./$(CLIENT_TARGET)

rebuild: clean all

dummy : 
	bash -c "head -c 300M /dev/urandom | base64 > dummy.txt"


##########################################################
# End of Makefile
##########################################################
