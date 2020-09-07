#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>

#include "send_packet.h"
#include "common.h"
#include "protocol.h"

/**
 * File search handler, an object that performs file search in a directory. */
typedef struct
{
    char *dir_name;
    DIR *dir_stream;
    FILE *match_stream;
} search_handler;




/**
 * Return whether `data` and the contents of the file named `file_name`
 * are equal. `data` and the file must be of equal size. */
int memory_file_equal( sized_data data, char *file_name )
{
    int r;
    sized_data data1 = malloc_sized_check(data.size);
    if(read_file_all(data1, file_name) != 0) r = 0;
    else r = 0 == memcmp(data.data, data1.data, data.size);
    free(data1.data);
    return r;
}



/**
 * File search handler
 */

/**
 * Return a new file search handler that will search in the directory `dir_name`
 * and write search results into the textual file `match_file_name`.
 * Return `NULL` if an error happened. */
search_handler *search_handler_new( char *dir_name, char *match_file_name )
{
    FILE *match_stream;
    char *dir_name1;
    DIR *dir_stream = opendir(dir_name);
    if( dir_stream == NULL )
    {
        perror("search_handler_new, open directory");
        print_accessed_path(dir_name);
    }
    
    match_stream = fopen(match_file_name, "a");
    if( match_stream == NULL )
    {
        perror("search_handler_new, open match list file");
        print_accessed_path(match_file_name);
    }

    dir_name1 = strdup(dir_name);
    if( dir_name1 == NULL )
    {
        fputs("search_handler_new: Memory allocation error in \"strdup\".\n",
              stderr);
        error_exit();
    }

    if( dir_stream != NULL && match_stream != NULL )
    {
        search_handler *r = malloc_check(sizeof(search_handler));
        r->dir_name = dir_name1;
        r->dir_stream = dir_stream;
        r->match_stream = match_stream;
        return r;
    }
    else
    {
        if( dir_stream != NULL ) closedir(dir_stream);
        if( match_stream != NULL ) fclose(match_stream);
        return NULL;
    }
}

void search_handler_free(search_handler *search_handler0)
{
    closedir(search_handler0->dir_stream);
    fclose(search_handler0->match_stream);
    free(search_handler0->dir_name);
    free(search_handler0);
}

/**
 * Search for a file which content is equal to `remote_data`.
 * Return a non-zero number if an error happened. */
int search_handler_search( search_handler *search_handler0,
                           char *remote_file_name, sized_data remote_data )
{
    int error = 0;
    char *matching_file_name;
    char list_line[FILE_NAME_SIZE] = {0};
    while( 1 )
    {
        char local_file_name[FILE_NAME_SIZE] = {0};
        struct stat stat0;
        struct dirent *dir_entry = readdir(search_handler0->dir_stream);
        if( dir_entry == NULL )
        {
            matching_file_name = NULL;
            break;
        }
        snprintf(local_file_name, sizeof(local_file_name), "%s%c%s",
                 search_handler0->dir_name, DIR_SEPARATOR, dir_entry->d_name);

        if( stat(local_file_name, &stat0) != 0 )
        {
            perror("search_handler_search");
            print_accessed_path(local_file_name);
        }
        else
        {
            if( S_ISREG(stat0.st_mode) )
            {
                size_t file_size = stat0.st_size;
                if( remote_data.size == file_size
                    && memory_file_equal(remote_data, local_file_name) )
                {
                    matching_file_name = dir_entry->d_name;
                    break;
                }
            }
        }
    }
    rewinddir(search_handler0->dir_stream);

    /* write the search result to the file */
    snprintf(list_line, sizeof(list_line), "%s %s\n",
             remote_file_name,
             matching_file_name == NULL ? "UNKNOWN" : matching_file_name);
    if( fputs(list_line, search_handler0->match_stream) == EOF )
    {
        perror("search_handler_search");
        error = 1;
    }
    return error;
}

int handle_session( int udp_socket, search_handler *search_handler0 )
{
    int error = 0;

    /* `seq_n` of the last data packet received */
    seq_n_t last_seq_n = seq_n_neg(1);
    
    struct sockaddr_storage remote_address;
    socklen_t remote_address_length;

    sized_data packet_buffer = malloc_sized_check(UDP_SIZE);
    while( 1 )
    {
        ssize_t packet_size;
        sized_data packet;
        int packet_type;
        remote_address_length = sizeof(remote_address);
        packet_size =
            recvfrom(udp_socket, packet_buffer.data, packet_buffer.size, 0,
                     (struct sockaddr *)&remote_address,
                     &remote_address_length);
        
        if( packet_size < 0 )
        {
            perror("receive packet");
            error = 1;
            break;
        }

        packet.size = packet_size;
        packet.data = packet_buffer.data;
        packet_type = get_packet_type(packet);
        if( packet_type < 0 )
        {
            printf("Received an invalid packet.\n");
        }
        else
        {
            if( packet_type == PACKET_TYPE_EOT )
            {
                printf("Received a EOT packet.\n");
                break;
            }
            else if( packet_type == PACKET_TYPE_DATA )
            {
                int send_ack;
                seq_n_t seq_n = ((prot_header *)packet.data)->seq_n;
                printf("Received a data packet with seq_n = %u, req_n = %d.\n",
                       (unsigned int)seq_n, get_packet_req_n(packet.data));
                
                if( seq_n == last_seq_n ) send_ack = 1;
                else if ( seq_n == seq_n_add(last_seq_n, 1) )
                {
                    packet_payload_p payload_p;
                    /* If `seq_n` of the received packet is next
                     * to the last received, perform a file search. */
                    last_seq_n = seq_n;
                    send_ack = 1;

                    payload_p = get_packet_payload_p(packet);
                    if( payload_p.error != 0 )
                    {
                        printf("The data packet is invalid, error: %d\n",
                               payload_p.error);
                    }
                    else
                    {
                        printf("Searching for the file, remote file name: %s\n",
                               (char *)payload_p.file_name.data);
                        if( search_handler_search(search_handler0,
                                                  payload_p.file_name.data,
                                                  payload_p.data) != 0 )
                        {
                            error = 3;
                            break;
                        }
                    }
                }
                else send_ack = 0;
                
                if( send_ack )
                {
                    printf("Sending an ACK packet with seq_n = %u.\n",
                           (unsigned int)last_seq_n);
                    init_ack_packet(packet_buffer, last_seq_n);
                    if ( send_packet(udp_socket, packet_buffer.data,
                                     get_ack_packet_size(), 0,
                                     (struct sockaddr *)&remote_address,
                                     remote_address_length) == -1 )
                    {
                        perror("send ACK packet");
                        error = 2;
                        break;
                    }
                    
                }
                printf("The last received seq_n is %u.\n",
                       (unsigned int)last_seq_n);
            }
            else
            {
                printf("Received an unexpected packet of type %d.\n",
                       packet_type);
            }
        }
    }
    free(packet_buffer.data);
    return error;
}

int main( int argc, char *argv[] )
{
    int error = 0;
    /* because we call `send_packet` */
    if( srand48_from_time() != 0 ) {
        printf("Error when initializing PRNG.\n");
        error = 2;
    }
    else
    {
        /* Assuming we have the command-line arguments as in the specification.
         * The first element of `argv` is the whole command line. */
        if( argc != 4 )
        {
            printf("Expected 3 command-line arguments.\n");
            error = 1;
        }
        else
        {
            in_port_t local_port = htons(strtol(argv[1], NULL, 10));
            char *compare_dir_name = argv[2];
            char *match_file_name = argv[3];

            int udp_socket = new_udp_socket(local_port);
            if( udp_socket >= 0 )
            {
                search_handler *search_handler0 =
                    search_handler_new(compare_dir_name, match_file_name);
                if( search_handler0 != NULL )
                {
                    if( handle_session(udp_socket, search_handler0) != 0 )
                    {
                        error = 3;
                    }
                    search_handler_free(search_handler0);
                }
                close(udp_socket);
            }
        }
    }
    if( error != 0 ) error_exit();
    return 0;
}
