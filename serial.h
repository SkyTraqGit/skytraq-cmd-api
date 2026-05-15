/*
 * serial.h - Cross-platform serial port abstraction
 *
 * Supports Windows (MSYS2 UCRT64 / native Win32) and Linux (POSIX termios).
 * Allows arbitrary baud rates where supported by the OS.
 */
#ifndef GNSS_SERIAL_H
#define GNSS_SERIAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
  typedef HANDLE serial_handle_t;
  #define SERIAL_INVALID_HANDLE INVALID_HANDLE_VALUE
#else
  typedef int serial_handle_t;
  #define SERIAL_INVALID_HANDLE (-1)
#endif

/* Open a serial port at the given baud rate (8N1, no flow control).
 * Returns SERIAL_INVALID_HANDLE on failure and prints an error to stderr.
 *
 * port: on Linux something like "/dev/ttyUSB0", on Windows "COM3" or "\\\\.\\COM10".
 * baud: any integer baud rate. On Linux non-standard rates are configured via
 *       BOTHER+termios2 (Linux only) or fall back to the closest standard rate.
 */
serial_handle_t serial_open(const char *port, int baud);

/* Close the serial port. */
void serial_close(serial_handle_t h);

/* Write exactly len bytes. Returns number of bytes written, or -1 on error. */
int serial_write(serial_handle_t h, const uint8_t *buf, size_t len);

/* Read up to maxlen bytes with a deadline (absolute timeout in ms from now).
 * Returns the number of bytes actually read (>=0), or -1 on error.
 * Returns 0 if no bytes arrived before the timeout. */
int serial_read_timeout(serial_handle_t h, uint8_t *buf, size_t maxlen, int timeout_ms);

/* Discard any buffered input. */
void serial_flush_input(serial_handle_t h);

#endif
