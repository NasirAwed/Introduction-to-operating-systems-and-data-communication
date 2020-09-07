#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <libgen.h>
#include <time.h>
#include <sys/select.h>

#include "send_packet.h"
#include "common.h"
#include "protocol.h"
#include "packet_list.h"

#define ACK_TIMEOUT 5 /* seconds */

/* CLOCK_MONOTONIC does not require a system call */
#define CLOCK CLOCK_MONOTONIC

/**
 * Iterator over files in a textual list. */
typedef struct
{
    char has_next;
    FILE *stream;
} file_iter;



/**
 * Time arithmetic. Time can be negative.
 */

struct timespec time_add( struct timespec x, struct timespec y )
{
    long nsec = x.tv_nsec + y.tv_nsec;
    int wrap = nsec >= 1000000000;
    struct timespec r;
    r.tv_sec = x.tv_sec + y.tv_sec + (wrap ? 1 : 0);
    r.tv_nsec = wrap ? nsec - 1000000000 : nsec;
    return r;
}

/**
 * Return `x - y`. */
struct timespec time_subtract( struct timespec x, struct timespec y )
{
    int wrap = x.tv_nsec < y.tv_nsec;
    struct timespec r;
    r.tv_sec = x.tv_sec - (wrap ? 1 : 0) - y.tv_sec;
    r.tv_nsec = x.tv_nsec + (wrap ? 1000000000 : 0) - y.tv_nsec;
    return r;
}

struct timeval timespec_to_timeval( struct timespec x )
{
    struct timeval r;
    r.tv_sec = x.tv_sec;
    r.tv_usec = x.tv_nsec / 1000;
    return r;
}



/**
 * Iterator over files
 */

/**
 * Return a new iterator that will iterate over files.
 * The names of the files are in the textual file named `list_file_name`.
 * Each file name on a separate line. */
file_iter *file_iter_new( char *list_file_name )
{
    FILE *stream = fopen(list_file_name, "r");
    file_iter *r;
    if( stream == NULL )
    {
        perror("file_iter_new");
        print_accessed_path(list_file_name);
        return NULL;
    }
    r = malloc_check(sizeof(file_iter));
    r->stream = stream;
    r->has_next = 1;
    return r;
}

void file_iter_free( file_iter *iter )
{
    fclose(iter->stream);
    free(iter);
}

/**
 * If there is a next line, write it to `file_name` and return `0`.
 * If there are no lines, return `1`.
 * If an error happened, return another number.
 * Move forward by one line in the list. */
int file_iter_next( file_iter *iter, sized_data file_name )
{
    if( !iter->has_next ) return 3;
    if( fgets(file_name.data, file_name.size, iter->stream) == NULL )
    {
        iter->has_next = 0;
        if( ferror(iter->stream) )
        {
            perror("file_iter_next");
            return 2;
        } else return 1;
    }
    else
    {
        string_trim_eol(file_name.data);
        return 0;
    }
}



/**
 * Return a new data packet containing the contents of the file
 * named `file_name` and its base name.
 * `req_n` and `seq_n` are the values of the corresponding packet fields.
 * If an error happened, the `data` field of the result will be `NULL`. */
sized_data new_packet_from_file( char *file_name, int req_n, seq_n_t seq_n )
{
    char *base_file_name = basename(file_name);

    /* including `'\0'`*/
    size_t base_file_name_size = strlen(base_file_name) + 1;

    size_t file_size = get_file_size(file_name);
    size_t packet_size = get_data_packet_size(base_file_name_size, file_size);
    sized_data r;
    if( packet_size > UDP_SIZE )
    {
        printf("File size and file name are too big: %s\n", file_name);
        r.size = 0;
        r.data = NULL;
        return r;
    }
    else
    {
        sized_data packet = malloc_sized_check(packet_size);
        sized_data packet_data;
        init_data_packet(packet, req_n, seq_n,
                         base_file_name_size, file_size);
        memcpy(get_packet_file_name_p(packet.data),
               base_file_name, base_file_name_size);
        packet_data.size = file_size;
        packet_data.data = get_packet_data_p(packet.data, base_file_name_size);
        if( read_file_all(packet_data, file_name) != 0 )
        {
            free(packet.data);
            r.size = 0;
            r.data = NULL;
            return r;
        }
        return packet;
    }
}

/**
 * If an error happened, return a non-zero number. */
int send_eot_packet( int udp_socket,
                     struct sockaddr *remote_address,
                     socklen_t remote_address_length )
{
    int error = 0;
    sized_data packet = malloc_sized_check(get_eot_packet_size());
    init_eot_packet(packet);
    printf("Sending a EOT packet.\n");
    if ( send_packet(udp_socket, packet.data, packet.size, 0,
                     remote_address, remote_address_length) < 0 )
    {
        perror("send EOT packet");
        error = 1;
    }
    free(packet.data);
    return error;
}

/**
 * If an error happened, return a non-zero number. */
int send_packet_list( int udp_socket,
                      struct sockaddr *remote_address,
                      socklen_t remote_address_length,
                      packet_list *packet_list0,
                      struct timespec *current_time)
{
    packet_list_node *node = packet_list0->head;
    while( node != NULL )
    {
        sized_data packet = node->el.packet;
        node->el.send_time = *current_time;
        printf("Resending a data packet with seq_n = %u, req_n = %d.\n",
               (unsigned int)(get_packet_seq_n(packet.data)),
               get_packet_req_n(packet.data));
        if ( send_packet(udp_socket, packet.data, packet.size, 0,
                         remote_address, remote_address_length) < 0 )
        {
            perror("resend data packet");
            return 1;
        }
        node = node->next;
    }
    return 0;
}

/**
 * Wait for an incoming packet or error in `udp_socket`,
 * but no more than `timeout`.
 * Return `1` if a packet is ready, `0` if no packet is ready,
 * or a negative number if an error happened.
 * May modify the memory referred by `timeout`. */
int wait_session( int udp_socket, struct timeval *timeout )
{
    fd_set read_set;
    fd_set error_set;
    int n_set;
    FD_ZERO(&read_set);
    FD_SET(udp_socket, &read_set);
    FD_ZERO(&error_set);
    FD_SET(udp_socket, &error_set);

    printf("Waiting for a packet with timeout"
           " (%ld seconds, %ld microseconds).\n",
           (long)timeout->tv_sec, (long)timeout->tv_usec);
    n_set = select(udp_socket + 1, &read_set, NULL, &error_set, timeout);
    if( n_set < 0 )
    {
        perror("receive packet, select");
        return -1;
    }
    return n_set != 0 ? 1 : 0;
}

char handle_session( int udp_socket, file_iter *iter,
                     struct sockaddr *remote_address,
                     socklen_t remote_address_length )
{
    int error = 0;
    int req_n = 0;
    struct timespec current_time;
    sized_data file_name;
    sized_data packet_buffer;
    packet_list *packet_list0;
    seq_n_t list_seq_n = 0; /* `seq_n` of the head of `packet_list0` */
    if( clock_gettime(CLOCK, &current_time) != 0 )
    {
        perror("read clock");
        return 2;
    }

    file_name = malloc_sized_check(FILE_NAME_SIZE);
    packet_buffer = malloc_sized_check(UDP_SIZE);
    packet_list0 = packet_list_new();
    /* Loop invariants:
     * - `packet_list0->size < WINDOW_SIZE`;
     * - `packet_list0` is in the order of increasing `send_time`;
     * - the head of `packet_list0` is the oldest packet;
     * - the `seq_n` field of the packet in the `i`th (0-based) element
     *   of `packet_list0` is `seq_n_add(list_seq_n, i)`. */
    while( 1 )
    {
        int wait_result;
        /* If there is a room in the packet list,
         * attach a new data packet to its tail and send that packet. */
        while( packet_list0->size < WINDOW_SIZE
               && file_iter_next(iter, file_name) == 0)
        {
            /* send a data packet */
            seq_n_t seq_n = seq_n_add(list_seq_n, packet_list0->size);
            sized_data packet = new_packet_from_file(
                file_name.data, req_n, seq_n);
            if( packet.data != NULL )
            {
                packet_list_el el;
                printf("Sending a data packet with seq_n = %u, req_n = %d.\n",
                       (unsigned int)seq_n, req_n);
                if ( send_packet(udp_socket, packet.data, packet.size, 0,
                                 remote_address, remote_address_length) < 0 )
                {
                    perror("send data packet");
                    error = 1;
                }

                if( error != 0 ) {
                    free(packet.data);
                    break;
                }
                el.send_time = current_time;
                el.packet = packet;
                packet_list_insert_last(packet_list0, el);
                req_n++;
            }

        }
        if( error != 0 ) break;

        /* The packet list may be empty here
         * only if there are no files to send. */
        if( packet_list0->size == 0 ) break;

        /* wait for an incoming packet or an ACK timeout */
        {
            struct timespec ack_timeout = {ACK_TIMEOUT, 0};
            struct timeval timeout =
                timespec_to_timeval(
                    time_subtract(
                        time_add(packet_list0->head->el.send_time, ack_timeout),
                        current_time));
            if( timeout.tv_sec < 0 )
            {
                timeout.tv_sec = 0;
                timeout.tv_usec = 0;
            }

            /* `timeout` is the time left until the timeout
             * of the oldest outstanding packet happens */
            wait_result = wait_session(udp_socket, &timeout);
            if( wait_result < 0 )
            {
                error = 8;
                break;
            }
        }
        /* After the wait, the clock is likely moved forward a lot.
         * Read the new time. */
        if( clock_gettime(CLOCK, &current_time) != 0 )
        {
            perror("read clock");
            error = 3;
            break;
        }

        if( wait_result == 0 )
        {
            /* ACK timeout. */
            printf("ACK timeout.\n");
            if( send_packet_list(udp_socket,
                                 remote_address, remote_address_length,
                                 packet_list0, &current_time) != 0 )
            {
                error = 7;
                break;
            }
        }
        else
        {
            sized_data packet;
            int packet_type;
            ssize_t packet_size =
                recvfrom(udp_socket, packet_buffer.data, packet_buffer.size, 0,
                         NULL, NULL);
            if( packet_size < 0 )
            {
                perror("receive packet");
                error = 4;
                break;
            }

            /* received a packet */
            packet.size = packet_size;
            packet.data = packet_buffer.data;
            packet_type = get_packet_type(packet);
            if( packet_type < 0 )
            {
                printf("Received an invalid packet.\n");
            }
            else
            {
                if( packet_type == PACKET_TYPE_ACK )
                {
                    seq_n_t list_index;
                    seq_n_t ack_seq_n = get_packet_ack_seq_n(packet.data);
                    printf("Received an ACK packet with ack_seq_n = %u.\n",
                           (unsigned int)ack_seq_n);

                    /* delete outstanding packets with the `seq_n` field
                     * up to `ack_seq_n` inclusive */
                    list_index = seq_n_subtract(ack_seq_n, list_seq_n);
                    if( list_index < packet_list0->size )
                    {
                        unsigned long i;
                        for( i = list_index + 1; i-- != 0; )
                        {
                            packet_list_delete_first(packet_list0);
                            list_seq_n = seq_n_add(list_seq_n, 1);
                        }
                    }
                    else
                    {
                        printf("No outstanding buffered packet"
                               " with seq_n = %u.\n", (unsigned int)ack_seq_n);
                    }
                    printf("The seq_n of the beginning of the window is %u.\n",
                           (unsigned int)list_seq_n);
                    printf("The number of outstanding buffered packets"
                           " is %lu.\n", packet_list0->size);
                }
                else
                {
                    printf("Received an unexpected packet of type %d.\n",
                           packet_type);
                }
            }
        }
    }
    packet_list_free(packet_list0);
    free(packet_buffer.data);
    free(file_name.data);

    if( error == 0 )
    {
        error = send_eot_packet(
            udp_socket, remote_address, remote_address_length);
    }
    return error;
}

int main( int argc, char *argv[] )
{
    int error = 0;
    /* because we call `send_packet` */
    if( srand48_from_time() != 0 ) {
        printf("Error when initializing PRNG.\n");
        error = 6;
    }
    else
    {
        /* Assuming we have the command-line arguments as in the specification.
         * The first element of `argv` is the whole command line. */
        if( argc != 5 )
        {
            printf("Expected 4 command-line arguments.\n");
            error = 1;
        }
        else
        {
            char *remote_host_name = argv[1];
            in_port_t remote_port = htons(strtol(argv[2], NULL, 10));
            char *list_file_name = argv[3];
            float loss_probability = strtod(argv[4], NULL) / 100.0;
            int udp_socket;
            set_loss_probability(loss_probability);
            printf("Setting loss probability to %f.\n", loss_probability);

            udp_socket = new_udp_socket(0); /* use any available port */
            if( udp_socket >= 0 )
            {
                file_iter *iter = file_iter_new(list_file_name);
                if( iter != NULL )
                {
                    struct sockaddr_in remote_address;
                    if( get_host_address_by_name(
                            remote_host_name, &remote_address.sin_addr) == 0)
                    {
                        /* Internet Domain */
                        remote_address.sin_family = AF_INET;
                        remote_address.sin_port = remote_port;

                        if( handle_session(
                                udp_socket, iter,
                                (struct sockaddr *)&remote_address,
                                sizeof(remote_address)) != 0 )
                        {
                            error = 5;
                        }
                    }
                    else error = 2;
                    file_iter_free(iter);
                }
                else error = 3;
                close(udp_socket);
            }
            else error = 4;
        }
    }
    if( error != 0 ) error_exit();
    return 0;
}
