#include <sys/stat.h>
#include <time.h>

#include "common.h"

#define ERROR_STATUS 72

void string_trim_eol( char *s )
{
    while( 1 )
    {
        char c = *s;
        if( c == '\r' || c == '\n' || c == '\0' )
        {
            *s = '\0';
            return;
        }
        s++;
    }
}

void error_exit( void )
{
    exit(ERROR_STATUS);
}

void print_accessed_path( char *path )
{
    printf("While accessing file or directory %s\n", path);
}

void *malloc_check( size_t size )
{
    void *p = malloc(size);
    if( p == NULL )
    {
        fputs("malloc: Memory allocation error.\n", stderr);
        error_exit();
    }
    return p;
}

sized_data malloc_sized_check( size_t size )
{
    sized_data r;
    r.size = size;
    r.data = malloc_check(size);
    return r;
}

ssize_t read_stream_all( sized_data buffer, FILE *stream )
{
    size_t received_size = 0;
    while( 1 )
    {
        size_t available_size = buffer.size - received_size;
        size_t moved_size = 0;
        if( available_size == 0 ) break;
        moved_size = fread((char *)buffer.data + received_size,
                                  1, available_size, stream);
        if( moved_size == 0 )
        {
            if( ferror(stream) )
            {
                perror("read_stream_all");
                return -1;
            }
            break;
        }
        received_size += moved_size;
    }
    return received_size;
}

int read_file_all( sized_data buffer, char *file_name )
{
    FILE *stream = fopen(file_name, "rb");
    if( stream == NULL )
    {
        perror("read_file_all");
        print_accessed_path(file_name);
        return 1;
    }
    else
    {
        ssize_t real_size = read_stream_all(buffer, stream);
        fclose(stream);
        if( real_size < 0 ) return 2;
        if( ((size_t)real_size) != buffer.size)
        {
            printf("Read less data (%lu) than expected (%lu) from file %s\n",
                   (unsigned long)real_size, (unsigned long)buffer.size,
                   file_name);
            return 3;
        }
        return 0;
    }
}

off_t get_file_size( char *file_name )
{
    struct stat stat0;
    if( stat(file_name, &stat0) != 0 )
    {
        perror("get_file_size");
        print_accessed_path(file_name);
        return -1;
    }
    return stat0.st_size;
}

void debug_dump( sized_data data )
{
    unsigned char *p = data.data;
    size_t n = data.size;
    for( ; n-- != 0; )
    {
        printf(" %02x", *p);
        p++;
    }
    printf("\n");
}

int srand48_from_time( void )
{
    struct timespec t;
    if( clock_gettime(CLOCK_MONOTONIC, &t) != 0 )
    {
        perror("srand48_from_time");
        return 1;
    }
    srand48(t.tv_sec * 1000000000 + t.tv_nsec);
    return 0;
}
