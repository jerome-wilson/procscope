# ProcScope - Real-Time Process & Memory Monitor
# Makefile

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -O2
CFLAGS += -Wno-newline-eof -Wno-unused-parameter -Wno-format
CFLAGS += -Wno-tautological-compare -Wno-unused-function
CFLAGS += -Iinclude

LDFLAGS =
LDLIBS  = -lm

SRC_DIR  = src
OBJ_DIR  = obj
INC_DIR  = include
LOGS_DIR = logs
DATA_DIR = data

TARGET = ./procscope

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/cli.c \
          $(SRC_DIR)/proc.c \
          $(SRC_DIR)/memmap.c \
          $(SRC_DIR)/monitor.c \
          $(SRC_DIR)/alert.c \
          $(SRC_DIR)/stream.c \
          $(SRC_DIR)/system.c \
          $(SRC_DIR)/ai.c \
          $(SRC_DIR)/ai_insights.c \
          $(SRC_DIR)/utils.c \
          $(SRC_DIR)/daemon.c \
          $(SRC_DIR)/logger.c \
          $(SRC_DIR)/config.c \
          $(SRC_DIR)/ipc.c \
          $(SRC_DIR)/history.c

OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

HEADERS = $(INC_DIR)/procscope.h \
          $(INC_DIR)/daemon.h \
          $(INC_DIR)/logger.h \
          $(INC_DIR)/config.h \
          $(INC_DIR)/ipc.h \
          $(INC_DIR)/history.h

all: directories $(TARGET)

directories:
	@mkdir -p $(OBJ_DIR) $(LOGS_DIR) $(DATA_DIR)

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LDLIBS)
	@echo "Build complete: $(TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning..."
	rm -rf $(OBJ_DIR)
	rm -f $(TARGET)

distclean: clean
	rm -rf $(DATA_DIR)/*.db
	rm -rf $(LOGS_DIR)/*.log
	rm -f /tmp/procscope.pid
	rm -f /tmp/procscope.fifo

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/procscope

uninstall:
	rm -f /usr/local/bin/procscope

debug: CFLAGS += -DDEBUG -g3 -O0
debug: clean all

release: CFLAGS += -DNDEBUG -O3
release: clean all

run: $(TARGET)
	$(TARGET) top

list: $(TARGET)
	$(TARGET) list

status: $(TARGET)
	$(TARGET) status

help:
	@echo "ProcScope Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       Build (default)"
	@echo "  clean     Remove build artifacts"
	@echo "  debug     Debug build"
	@echo "  release   Optimised build"
	@echo "  run       Run: procscope top"
	@echo "  list      Run: procscope list"
	@echo "  status    Run: procscope status"

.PHONY: all clean distclean install uninstall debug release run list status help directories
