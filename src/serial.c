#include "serial.h"

struct termios toptions;

int *serial_open(char *dev, int baudrate)
{
    static int fd;

    fd = open(dev, O_RDWR | O_NOCTTY);
    if (-1 == fd)
    {
        perror("Fail to open serial port");
        return NULL;
    }
    printf("fd opened as %i\n", fd);

    /* get current serial port settings */
    tcgetattr(fd, &toptions);
    /* set 9600 baud both ways */
    cfsetispeed(&toptions, B9600);
    cfsetospeed(&toptions, B9600);
    /* 8 bits, no parity, no stop bits */
    toptions.c_cflag &= ~PARENB;
    toptions.c_cflag &= ~CSTOPB;
    toptions.c_cflag &= ~CSIZE;
    toptions.c_cflag |= CS8;
    /* Canonical mode */
    toptions.c_lflag |= ICANON;
    /* commit the serial port settings */
    tcsetattr(fd, TCSANOW, &toptions);

    return &fd;
}

int serial_write(int fd, char *msg, int bytes)
{
    int status;

    status = write(fd, msg, bytes);
    if (-1 == status)
    {
        perror("Fail to write");
        return -1;
    }

    return 1;
}

int serial_read(int fd, char *buf, int count)
{
    int n_bytes;

    n_bytes = read(fd, buf, count);
    if (-1 == n_bytes)
    {
        perror("Fail to read");
        return -1;
    }
    else 
    {
        /* insert terminating zero in the string */
        *(buf + n_bytes) = 0;

        printf("%i bytes read, buffer contains: %s\n", n_bytes, buf);
    }

    return 1;
}
