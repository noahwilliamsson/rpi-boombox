CFLAGS = -Wall -ggdb -pthread
LDFLAGS = -lpthread
OBJS = main.o app.o audio.o openal-audio.o net.o player.o playlist.o rpi-gpio.o

ifeq ($(shell uname),Darwin)
	LDFLAGS += -framework Libspotify
	LDFLAGS += -framework OpenAL
else
	LDFLAGS += -L/usr/local/lib -lspotify -lopenal
endif
EXE = pi-boombox 
all: $(OBJS)
	$(CC) -o $(EXE) $(OBJS) $(LDFLAGS)
clean:
	rm -f $(EXE) $(OBJS)
