CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -Isrc
LDFLAGS = -lasound -lltc

TARGET  = pitcgen
SRCS    = src/tc.c src/pitcgen.c
HELPER  = pitcgenctl

.PHONY: all clean install deps install-service enable-service

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Install build dependencies (run once on the Pi)
deps:
	sudo apt-get install -y libltc-dev libasound2-dev gcc make

install: $(TARGET)
	sudo install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	sudo install -m 755 $(HELPER) /usr/local/bin/$(HELPER)

install-service: $(TARGET)
	sudo install -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	sudo install -m 755 $(HELPER) /usr/local/bin/$(HELPER)
	sudo install -m 644 pitcgen.service /etc/systemd/system/pitcgen.service
	sudo sh -c 'test -f /etc/default/pitcgen || install -m 644 pitcgen.env.example /etc/default/pitcgen'
	sudo systemctl daemon-reload

enable-service:
	sudo systemctl enable --now pitcgen.service

clean:
	rm -f $(TARGET)
