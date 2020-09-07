#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common.h"
#include "protocol.h"

#define PROT_HEADER_CONST0 0x7f



seq_n_t seq_n_add(seq_n_t x, seq_n_t y)
{
    return (x + y) % SEQ_N_LIMIT;
}

seq_n_t seq_n_neg(seq_n_t x)
{
    return (SEQ_N_LIMIT - x) % SEQ_N_LIMIT;
}

seq_n_t seq_n_subtract(seq_n_t x, seq_n_t y)
{
    return ((SEQ_N_LIMIT + x) - y) % SEQ_N_LIMIT;
}

int seq_n_between(seq_n_t x, seq_n_t  y, seq_n_t z)
{
    return seq_n_subtract(y, x) < seq_n_subtract(z, x);
}



int new_udp_socket( in_port_t port )
{
    int udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if( udp_socket < 0 )
    {
        perror("create socket");
        return -1;
    }
    else
    {
        struct sockaddr_in local_address;
        local_address.sin_family = AF_INET; /* Internet Domain */
        local_address.sin_port = port;
        
        /* Server's Internet Address */
        local_address.sin_addr.s_addr = INADDR_ANY;
        
        if( bind(udp_socket, (struct sockaddr*)&local_address,
                 sizeof(local_address)) < 0 )
        {
            perror("bind socket");
            close(udp_socket);
            return -1;
        }
        return udp_socket;
    }
}

int get_host_address_by_name( char *host_name, struct in_addr *address )
{
    struct hostent *host_info = gethostbyname(host_name);
    if (host_info == NULL)
    {
        perror("unknown host");
        printf("Host name: %s\n", host_name);
        return 1;
    }
    *address = *(struct in_addr*)((host_info->h_addr_list)[0]);
    return 0;
}

int get_packet_type( sized_data packet )
{
    prot_header *prot_h;
    unsigned char flags;
    if( packet.size < sizeof(prot_header) ) return -1;
    
    prot_h = (prot_header *)packet.data;
    if( prot_h->size != packet.size ) return -1;
    if( prot_h->const0 != PROT_HEADER_CONST0 ) return -1;

    flags = prot_h->flags;
    return (flags & 0x1) != 0 ? PACKET_TYPE_DATA
        : ((flags & 0x2) != 0 ? PACKET_TYPE_ACK
           : ((flags & 0x4) != 0 ? PACKET_TYPE_EOT : -1));
}

seq_n_t get_packet_seq_n( void *packet_data )
{
    return ((prot_header *)packet_data)->seq_n;
}

seq_n_t get_packet_ack_seq_n( void *packet_data )
{
    return ((prot_header *)packet_data)->ack_seq_n;
}

size_t get_eot_packet_size( void )
{
    return sizeof(prot_header);
}

size_t init_eot_packet( sized_data packet )
{
    size_t packet_size = get_eot_packet_size();
    prot_header *prot_h;
    if( packet_size > packet.size )
    {
        fputs("init_eot_packet: Buffer is too small.\n", stderr);
        error_exit();
    }
    
    prot_h = packet.data;
    prot_h->size = packet_size;
    prot_h->seq_n = 0;
    prot_h->ack_seq_n = 0;
    prot_h->flags = 0x4;
    prot_h->const0 = PROT_HEADER_CONST0;

    return packet_size;
}

size_t get_ack_packet_size( void )
{
    return sizeof(prot_header);
}

size_t init_ack_packet( sized_data packet, seq_n_t seq_n )
{
    size_t packet_size = get_ack_packet_size();
    prot_header *prot_h;
    if( packet_size > packet.size )
    {
        fputs("init_ack_packet: Buffer is too small.\n", stderr);
        error_exit();
    }
    
    prot_h = packet.data;
    prot_h->size = packet_size;
    prot_h->seq_n = 0;
    prot_h->ack_seq_n = seq_n;
    prot_h->flags = 0x2;
    prot_h->const0 = PROT_HEADER_CONST0;

    return packet_size;
}

size_t get_data_packet_size( size_t file_name_size, size_t data_size )
{
    return sizeof(prot_header) + sizeof(payload_header)
        + file_name_size + data_size;
}

void *get_packet_file_name_p( void *packet_data )
{
    return (char *)packet_data + sizeof(prot_header) + sizeof(payload_header);
}

void *get_packet_data_p( void *packet_data, size_t file_name_size )
{
    return (char *)packet_data + sizeof(prot_header) + sizeof(payload_header)
        + file_name_size;
}

static payload_header *get_packet_payload_header_p( void *packet_data )
{
    return (payload_header *)((char *)packet_data + sizeof(prot_header));
}

int get_packet_req_n( void *packet_data )
{
    return get_packet_payload_header_p(packet_data)->req_n;
}

packet_payload_p get_packet_payload_p( sized_data packet )
{
    packet_payload_p r;
    if( packet.size < get_data_packet_size(0, 0) ) r.error = 1;
    else
    {
        payload_header *payload_h = get_packet_payload_header_p(packet.data);
        int file_name_size = payload_h->file_name_size;
        if( file_name_size <= 0 ) r.error = 2;
        else
        {
            if( packet.size < get_data_packet_size(file_name_size, 0) )
            {
                r.error = 3;
            }
            else
            {
                char *file_name = get_packet_file_name_p(packet.data);
                if( file_name[file_name_size - 1] != '\0' ) r.error = 4;
                else
                {
                    r.error = 0;
                    r.file_name.size = file_name_size;
                    r.file_name.data = file_name;
                    r.data.size = packet.size
                        - get_data_packet_size(file_name_size, 0);
                    r.data.data = get_packet_data_p(packet.data, file_name_size);
                }
            }
        }
    }
    return r;
}

size_t
init_data_packet( sized_data packet, int req_n, seq_n_t seq_n,
                  size_t file_name_size, size_t data_size )
{
    size_t packet_size = get_data_packet_size(file_name_size, data_size);
    prot_header *prot_h;
    payload_header *payload_h;
    if( packet_size > packet.size )
    {
        fputs("init_data_packet: Buffer is too small.\n", stderr);
        error_exit();
    }
    
    prot_h = packet.data;
    prot_h->size = packet_size;
    prot_h->seq_n = seq_n;
    prot_h->ack_seq_n = 0;
    prot_h->flags = 0x1;
    prot_h->const0 = PROT_HEADER_CONST0;

    payload_h = get_packet_payload_header_p(packet.data);
    payload_h->req_n = req_n;
    payload_h->file_name_size = file_name_size;

    return packet_size;
}
