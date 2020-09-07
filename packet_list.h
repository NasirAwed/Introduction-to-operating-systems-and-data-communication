/**
 * Singly-linked list of outstanding buffered packets.
 * For the Client. */

#ifndef PACKET_LIST_H
#define PACKET_LIST_H

#include <time.h>

#include "common.h"

/**
 * element of `packet_list` */
typedef struct
{
    struct timespec send_time;
    sized_data packet;
} packet_list_el;

typedef struct packet_list_node
{
    packet_list_el el;
    struct packet_list_node *next;
} packet_list_node;

typedef struct
{
    unsigned long size;
    packet_list_node *head;
    packet_list_node **tail;
} packet_list;

/**
 * Return a new empty list. */
packet_list *packet_list_new( void );

void packet_list_free( packet_list *list );

void packet_list_insert_last( packet_list *list, packet_list_el el );

void packet_list_delete_first( packet_list *list );

#endif
