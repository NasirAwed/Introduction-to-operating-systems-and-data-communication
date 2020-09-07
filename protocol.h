/**
 * prot = protocol
 * seq = sequence,
 * EOT = end of transmission, packet terminating a session.
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <netinet/in.h>

#include "common.h"



/**
 * Sequence numbers range from `0` inclusive to this constant exclusive. */
#define SEQ_N_LIMIT (WINDOW_SIZE + 1)

/**
 * sequence numbers,  numbers modulo `SEQ_N_LIMIT` */
typedef unsigned char seq_n_t;

/**
 * the greatest size of UDP payload, in bytes */
#define UDP_SIZE 65507

/**
 * must be positive */
#define WINDOW_SIZE 7

#define PACKET_TYPE_DATA 0
#define PACKET_TYPE_ACK 1
#define PACKET_TYPE_EOT 2

/**
 * header for all types of packets */
typedef struct
{
    int size;
    seq_n_t seq_n;
    seq_n_t ack_seq_n;
    unsigned char flags;
    unsigned char const0; /* always `PROT_HEADER_CONST0` */
} prot_header;

/**
 * header for data packets */
typedef struct
{
    int req_n;
    int file_name_size;
} payload_header;

/**
 * pointers to file name and file data in a UDP packet */
typedef struct
{
    int error;
    sized_data file_name;
    sized_data data;
} packet_payload_p;



/**
 * sequence number arithmetic
 */

seq_n_t seq_n_add(seq_n_t x, seq_n_t y);

/**
 * Return `x` negated. */
seq_n_t seq_n_neg(seq_n_t x);

/**
 * Return `x - y`. */
seq_n_t seq_n_subtract(seq_n_t x, seq_n_t y);

/**
 *  cyclic order on sequence numbers,
 * x inclusive to z exclusive goes through y*/
int seq_n_between(seq_n_t x, seq_n_t  y, seq_n_t z);



/**
 * Return a new UDP socket on the local host
 * or a negative number if an error happened. */
int new_udp_socket( in_port_t port );

/**
 * Write the address of the host `host_name` to `address`.
 * Return a non-zero number if an error happened. */
int get_host_address_by_name( char *host_name, struct in_addr *address );

/**
 * Return the type of `packet` or a negative number if the packet is invalid.
 * Packet types are `PACKET_TYPE_*`. */
int get_packet_type( sized_data packet );

/**
 * Return the `seq_n` field of `packet`. The packet must be valid. */
seq_n_t get_packet_seq_n( void *packet_data );

/**
 * Return the `ack_seq_n` field of `packet`. The packet must be valid. */
seq_n_t get_packet_ack_seq_n( void *packet_data );

/**
 * Return the size of an EOT packet. */
size_t get_eot_packet_size( void );

/**
 * Write a EOT packet into `packet`. The size of `packet` must be sufficient. */
size_t init_eot_packet( sized_data packet );

/**
 * Return the size of an ACK packet. */
size_t get_ack_packet_size( void );

/**
 * Write an ACK packet into `packet`. The size of `packet` must be sufficient.
 * `seq_n` is written into the `ack_seq_n` packet field. */
size_t init_ack_packet( sized_data packet, seq_n_t seq_n );

/**
 * Return the size of an ACK packet.
 * `file_name_size` is the size of a file name including `'\0'`.
 * `data_size` is the size of file data. */
size_t get_data_packet_size( size_t file_name_size, size_t data_size );

/**
 * Return the pointer to the file name in the packet `packet_data`.
 * `packet_data` must by of type `PACKET_TYPE_DATA` */
void *get_packet_file_name_p( void *packet_data );

/**
 * Return the pointer to the file data in the packet `packet_data`.
 * `file_name_size` is the size of a file name including `'\0'`.
 * `packet_data` must by of type `PACKET_TYPE_DATA` */
void *get_packet_data_p( void *packet_data, size_t file_name_size );

/**
 * Return the `req_n` field of the packet `packet_data`.
 * `packet_data` must by of type `PACKET_TYPE_DATA` */
int get_packet_req_n( void *packet_data );

/**
 * `packet` must by of type `PACKET_TYPE_DATA` */
packet_payload_p get_packet_payload_p( sized_data packet );

/**
 * Write a data packet into `packet`. The size of `packet` must be sufficient.
 * `req_n` is written into the `req_n` packet field.
 * `seq_n` is written into the `seq_n` packet field.
 * `file_name_size` is the size of a file name including `'\0'`.
 * `data_size` is the size of file data. */
size_t init_data_packet( sized_data packet, int req_n, seq_n_t seq_n,
                         size_t file_name_size, size_t data_size );

#endif
