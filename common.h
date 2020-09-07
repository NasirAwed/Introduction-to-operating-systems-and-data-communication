#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>

#if defined(WIN32)
#  define DIR_SEPARATOR '\\'
#else
#  define DIR_SEPARATOR '/'
#endif

#define FILE_NAME_SIZE 0x1000

typedef struct
{
    size_t size; /* size of the memory block referred by `data`, in bytes */
    void *data;
} sized_data;

/** Trim the end-of-line sequence (if there is one) in `s` in-place. */
void string_trim_eol( char *s );

/**
 * Exit with an error status. */
void error_exit( void );

/**
 * Print a message that the program accessed `path`. For error reporting. */
void print_accessed_path( char *path );

/**
 * Do the same as `malloc`,
 * except that if there is an error during a `malloc` call,
 * this function will print an error message
 * and terminate the program with an error status.
 * Thus this function never returns `NULL`. */
void *malloc_check( size_t size );

/**
 * Similar to `malloc_check`. */
sized_data malloc_sized_check( size_t size );

/**
 * Transfer data from `stream` to `buffer`, but no more than the buffer size.
 * Return the number of transferred bytes
 * or a negative number if an error happened. */
ssize_t read_stream_all( sized_data buffer, FILE *stream );

/**
 * Transfer data from the file named `file_name` to `buffer`,
 * but no more than the buffer size.
 * Return a non-zero number if the file size is less than the buffer size
 * or an error happened. */
int read_file_all( sized_data buffer, char *file_name );

off_t get_file_size( char *file_name );

/**
 * Print `data` as a list of bytes in base 16. For debugging. */
void debug_dump( sized_data data );

/**
 * Initialize a PRNG corresponding to `srand48` from system time.
 * Return a non-zero number if an error happened. */
int srand48_from_time( void );

#endif
