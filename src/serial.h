#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/ioctl.h>

int *serial_open(char *dev, int baudrate);
int serial_write(int fd, char *msg, int bytes);
int serial_read(int fd, char *buf, int count);

