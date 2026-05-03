CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -fopenmp
LDFLAGS = -lm -fopenmp
TARGET = furnishing
VIZ = visualizer
SRCS = main.c config.c utils.c simulation.c
OBJS = $(SRCS:.c=.o)
VIZ_LIBS = -lGL -lGLU -lglut -lpthread -lm

.PHONY: all clean run viz

all: $(TARGET) $(VIZ)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(VIZ): visualizer.c viz_protocol.h
	$(CC) $(CFLAGS) -o $@ visualizer.c $(VIZ_LIBS)

main.o: main.c furnishing.h
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c furnishing.h
	$(CC) $(CFLAGS) -c -o $@ $<

utils.o: utils.c furnishing.h
	$(CC) $(CFLAGS) -c -o $@ $<

simulation.o: simulation.c furnishing.h viz_protocol.h
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET) config.txt

viz: $(VIZ)
	./$(VIZ)

clean:
	rm -f $(OBJS) $(TARGET) $(VIZ) /tmp/furnish_team*_back /tmp/furnish_viz

