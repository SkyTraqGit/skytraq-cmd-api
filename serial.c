/*
 * serial.c - Cross-platform serial port implementation
 *
 * On Windows (MSYS2 UCRT64 or native MinGW), uses Win32 CreateFile / DCB / OVERLAPPED.
 * On Linux, uses POSIX termios; non-standard baud rates use the termios2 ioctl when available.
 */
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE      /* needed for cfmakeraw on glibc */
#endif

#include "serial.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ============================================================================
 *   Windows implementation
 * ============================================================================ */
#ifdef _WIN32

serial_handle_t serial_open(const char *port, int baud) {
    /* Allow plain "COM3" as well as "\\\\.\\COM10" - prepend the universal prefix
     * so COM10+ also work. */
    char path[64];
    if (port[0] == '\\' || strncmp(port, "//", 2) == 0) {
        snprintf(path, sizeof(path), "%s", port);
    } else {
        snprintf(path, sizeof(path), "\\\\.\\%s", port);
    }

    HANDLE h = CreateFileA(path,
                           GENERIC_READ | GENERIC_WRITE,
                           0,
                           NULL,
                           OPEN_EXISTING,
                           0, /* synchronous I/O is fine - we use timeouts */
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open '%s' (error %lu)\n", path, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        fprintf(stderr, "GetCommState failed (error %lu)\n", GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    dcb.BaudRate = (DWORD)baud;   /* Win32 accepts arbitrary integer baud rates */
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX  = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    if (!SetCommState(h, &dcb)) {
        fprintf(stderr, "SetCommState failed (error %lu) - baud %d may be unsupported\n",
                GetLastError(), baud);
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }

    /* Configure timeouts so ReadFile returns promptly. We supply our own
     * deadline in serial_read_timeout, so use ReadIntervalTimeout=MAXDWORD
     * with the constant/multiplier zeroed: ReadFile then returns immediately
     * with whatever is buffered. We then poll until our deadline. */
    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout         = MAXDWORD;
    to.ReadTotalTimeoutConstant    = 0;
    to.ReadTotalTimeoutMultiplier  = 0;
    to.WriteTotalTimeoutConstant   = 1000;
    to.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

void serial_close(serial_handle_t h) {
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

int serial_write(serial_handle_t h, const uint8_t *buf, size_t len) {
    DWORD written = 0;
    if (!WriteFile(h, buf, (DWORD)len, &written, NULL)) {
        fprintf(stderr, "WriteFile failed (error %lu)\n", GetLastError());
        return -1;
    }
    return (int)written;
}

int serial_read_timeout(serial_handle_t h, uint8_t *buf, size_t maxlen, int timeout_ms) {
    DWORD start = GetTickCount();
    size_t got = 0;
    while (got < maxlen) {
        DWORD elapsed = GetTickCount() - start;
        if ((int)elapsed >= timeout_ms) break;
        DWORD n = 0;
        if (!ReadFile(h, buf + got, (DWORD)(maxlen - got), &n, NULL)) {
            fprintf(stderr, "ReadFile failed (error %lu)\n", GetLastError());
            return -1;
        }
        if (n > 0) {
            got += n;
        } else {
            Sleep(5); /* nothing buffered yet */
        }
    }
    return (int)got;
}

void serial_flush_input(serial_handle_t h) {
    PurgeComm(h, PURGE_RXCLEAR);
}

#else /* ============================================================================
       *   POSIX / Linux implementation
       * ============================================================================ */

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

/* Linux supports arbitrary baud rates via the termios2 ioctl + BOTHER flag.
 * Including <linux/termios.h> alongside glibc's <termios.h> causes type
 * redefinition errors, so we declare just what we need ourselves. The ioctl
 * numbers and bit values match the kernel's UAPI for all supported arches.
 *
 * If your kernel/libc combo doesn't have termios2, set GNSS_NO_BOTHER=1
 * when compiling - non-standard baud rates will then be rejected. */
#if defined(__linux__) && !defined(GNSS_NO_BOTHER)
  #ifndef BOTHER
    #define BOTHER 0010000
  #endif
  #ifndef CBAUD
    #define CBAUD  0010017
  #endif
  /* termios2 is the same layout the kernel uses; we don't have to know all
   * the c_cflag bits, just preserve them and add BOTHER. */
  struct termios2_local {
      unsigned int c_iflag;
      unsigned int c_oflag;
      unsigned int c_cflag;
      unsigned int c_lflag;
      unsigned char c_line;
      unsigned char c_cc[19];
      unsigned int c_ispeed;
      unsigned int c_ospeed;
  };
  #ifndef TCGETS2
    #define TCGETS2 _IOR('T', 0x2A, struct termios2_local)
  #endif
  #ifndef TCSETS2
    #define TCSETS2 _IOW('T', 0x2B, struct termios2_local)
  #endif
  /* If <asm-generic/ioctls.h> already pulled in TCGETS2/TCSETS2 defined in
   * terms of `struct termios2`, override with our local-struct versions. */
  #undef TCGETS2
  #undef TCSETS2
  #define TCGETS2 _IOR('T', 0x2A, struct termios2_local)
  #define TCSETS2 _IOW('T', 0x2B, struct termios2_local)
#endif

static int set_standard_baud(int fd, int baud) {
    /* Map common rates to Bxxx symbols. */
    speed_t s;
    switch (baud) {
        case 4800:    s = B4800;    break;
        case 9600:    s = B9600;    break;
        case 19200:   s = B19200;   break;
        case 38400:   s = B38400;   break;
        case 57600:   s = B57600;   break;
        case 115200:  s = B115200;  break;
        case 230400:  s = B230400;  break;
#ifdef B460800
        case 460800:  s = B460800;  break;
#endif
#ifdef B921600
        case 921600:  s = B921600;  break;
#endif
        default: return -1; /* not a standard rate */
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return -1;
    cfmakeraw(&tio);
    cfsetispeed(&tio, s);
    cfsetospeed(&tio, s);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &tio);
}

#if defined(__linux__) && !defined(GNSS_NO_BOTHER)
static int set_custom_baud_linux(int fd, int baud) {
    /* Use TCSETS2 with BOTHER to set any integer rate. */
    struct termios2_local tio;
    if (ioctl(fd, TCGETS2, &tio) != 0) return -1;
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = (unsigned int)baud;
    tio.c_ospeed = (unsigned int)baud;
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tio.c_cflag |= CS8;
    /* Make raw */
    tio.c_iflag = 0;
    tio.c_oflag = 0;
    tio.c_lflag = 0;
    tio.c_cc[16 /*VMIN  on Linux*/] = 0;
    tio.c_cc[ 5 /*VTIME on Linux*/] = 0;
    return ioctl(fd, TCSETS2, &tio);
}
#endif

serial_handle_t serial_open(const char *port, int baud) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open('%s'): %s\n", port, strerror(errno));
        return -1;
    }
    /* Switch to blocking mode (we use select() for timeouts). */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    if (set_standard_baud(fd, baud) != 0) {
#if defined(__linux__) && !defined(GNSS_NO_BOTHER)
        if (set_custom_baud_linux(fd, baud) != 0) {
            fprintf(stderr, "Cannot set baud rate %d: %s\n", baud, strerror(errno));
            close(fd);
            return -1;
        }
#else
        fprintf(stderr, "Cannot set non-standard baud rate %d on this platform.\n", baud);
        close(fd);
        return -1;
#endif
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

void serial_close(serial_handle_t h) {
    if (h >= 0) close(h);
}

int serial_write(serial_handle_t h, const uint8_t *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(h, buf + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t)n;
    }
    return (int)written;
}

int serial_read_timeout(serial_handle_t h, uint8_t *buf, size_t maxlen, int timeout_ms) {
    struct timeval start;
    gettimeofday(&start, NULL);
    size_t got = 0;
    while (got < maxlen) {
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000L
                        + (now.tv_usec - start.tv_usec) / 1000L;
        long remain = timeout_ms - elapsed_ms;
        if (remain <= 0) break;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(h, &rfds);
        struct timeval tv;
        tv.tv_sec  = remain / 1000;
        tv.tv_usec = (remain % 1000) * 1000;

        int r = select(h + 1, &rfds, NULL, NULL, &tv);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break; /* timeout */
        ssize_t n = read(h, buf + got, maxlen - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        got += (size_t)n;
    }
    return (int)got;
}

void serial_flush_input(serial_handle_t h) {
    tcflush(h, TCIFLUSH);
}

#endif
