#include "packet_list.h"

packet_list *packet_list_new( void )
{
    packet_list *r = malloc_check(sizeof(packet_list));
    r->size = 0;
    r->head = NULL;
    r->tail = &r->head;
    return r;
}

void packet_list_free( packet_list *list )
{
    packet_list_node *node = list->head;
    while( node != NULL )
    {
        packet_list_node *node1 = node->next;
        free(node->el.packet.data);
        free(node);
        node = node1;
    }
    free(list);
}

void packet_list_insert_last( packet_list *list, packet_list_el el )
{
    packet_list_node *node = malloc_check(sizeof(packet_list_node));
    node->el = el;
    node->next = NULL;
    *(list->tail) = node;
    list->tail = &(node->next);
    list->size++;
}

void packet_list_delete_first( packet_list *list )
{
    packet_list_node *head;
    if( list->size == 0 )
    {
        fputs("packet_list_remove_first: The list must be non-empty.\n", stderr);
        error_exit();
    }
    head = list->head;
    list->head = head->next;
    free(head->el.packet.data);
    free(head);
    list->size--;
}
