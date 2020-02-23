#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include "sr_arpcache.h"
#include "sr_router.h"
#include "sr_if.h"
#include "sr_protocol.h"

/* 
  This function gets called every second. For each request sent out, we keep
  checking whether we should resend an request or destroy the arp request.
  See the comments in the header file for an idea of what it should look like.
*/

extern uint16_t cksum(const void *_data, int len);

extern struct sr_if* target_ip_2_iface(struct sr_instance* sr, uint32_t t_ip);
extern void printIP(uint32_t ip);
extern void printMAC(uint8_t *mac);

void sr_arpcache_sweepreqs(struct sr_instance *sr) { 
    /* Fill this in */
    struct sr_arpcache *cache = &(sr->cache);

    struct sr_arpreq *req;
    for (req = cache->requests; req != NULL; req = req->next) {
        
        /* get forwarding INTERFACE */
        struct sr_if* via_iface = target_ip_2_iface(sr,req->ip);
        if(via_iface==NULL) return;

        uint8_t* arp_packet = calloc(42,1); /* init all 42 bytes to zero */
        sr_ethernet_hdr_t *arp_req_packet_ETH_FRAME = (sr_ethernet_hdr_t*)(arp_packet);
        
        /* set source MAC */
        memcpy(arp_req_packet_ETH_FRAME->ether_shost,via_iface->addr,6);
        printMAC(arp_packet+6);

        /*set destination MAC - broadcast for ARP - because we dont know MAC yet*/
        memcpy(arp_req_packet_ETH_FRAME->ether_dhost,"\xff\xff\xff\xff\xff\xff",6);

        /* set etherType to 0x0806 for ARP */
        arp_req_packet_ETH_FRAME->ether_type = htons(0x0806);

        
        sr_arp_hdr_t* apr_req_packet_ARP_FRAME = (sr_arp_hdr_t*)(arp_packet+14);

        apr_req_packet_ARP_FRAME->ar_hrd = htons(1);             /* format of hardware address   */
        apr_req_packet_ARP_FRAME->ar_pro=htons(0x0800);             /* format of protocol address   */
        apr_req_packet_ARP_FRAME->ar_hln=6;             /* length of hardware address   */
        apr_req_packet_ARP_FRAME->ar_pln=4;             /* length of protocol address   */
        apr_req_packet_ARP_FRAME->ar_op=htons(1);              /* ARP opcode (command)         */
        memcpy(apr_req_packet_ARP_FRAME->ar_sha,via_iface->addr,6);   /* sender hardware address      */
        apr_req_packet_ARP_FRAME->ar_sip=via_iface->ip;             /* sender IP address            */
        memcpy(apr_req_packet_ARP_FRAME->ar_tha,"\x00\x00\x00\x00\x00\x00",6);   /* target hardware address      */
        apr_req_packet_ARP_FRAME->ar_tip=req->ip;             /* target IP address */



        printf("<------------- ARP SWEEP -------------->\n");

        if(req->times_sent>=5)
        {
            printf("-----> No ARP Reply for n:%d ->", req->times_sent++);
            printIP(req->ip);
            

            /* send ARP not reachable to all packets(IF AVAILABLE) waiting on this request*/
            
            /* 14 eth-header , 20 ip-header, icmp_type3, eth-crc-fcs */
            uint32_t icmp_type3_pkt_len = 14+20+sizeof(sr_icmp_t3_hdr_t)+4;
            uint8_t* icmp_type3_pkt = malloc(icmp_type3_pkt_len);

            struct sr_packet* pending_pkt;
            for(pending_pkt=req->packets;pending_pkt!=NULL;pending_pkt=pending_pkt->next)
            {
                printf("YYY\n");
                
                

                /* get a pointer to the IP header of icmp_type3_pkt */
                sr_ip_hdr_t* icmp_type3_pkt_IP_HEADER = (sr_ip_hdr_t*)(icmp_type3_pkt+14);

                /* pointet to pending_pkt_buf */
                uint8_t* pending_pkt_buf = pending_pkt->buf;

                /* pointer to the IP_header of pending_pkt->buf */
                sr_ip_hdr_t* pending_pkt_buf_IP_HEADER = (sr_ip_hdr_t*)(pending_pkt_buf+14);

                /* COPY THE TRIVIAL IP HEADERS */
                memcpy(icmp_type3_pkt_IP_HEADER,pending_pkt_buf_IP_HEADER,20);
                
                /* set destination IP of ICMP as IP of sender of the original packet*/
                icmp_type3_pkt_IP_HEADER->ip_dst=pending_pkt_buf_IP_HEADER->ip_src;

                /* get the interface for the destination IP */
                struct sr_if* via_iface = target_ip_2_iface(sr,icmp_type3_pkt_IP_HEADER->ip_dst);


                /* set source IP as IP of via_iface */
                icmp_type3_pkt_IP_HEADER->ip_src = via_iface->ip;
                

                /* set IP PROTOCOL to 1 for ICMP*/
                icmp_type3_pkt_IP_HEADER->ip_p=1;

                /* set IP TTL */
                icmp_type3_pkt_IP_HEADER->ip_ttl=64;

                /* set IP packet length */
                icmp_type3_pkt_IP_HEADER->ip_len = htons(20+sizeof(sr_icmp_t3_hdr_t)); 
                /* 20 for IP header and 36 for sr_icmp_t3_hdr_t */

                /* set IP Header Checksum */
                icmp_type3_pkt_IP_HEADER->ip_sum = 0;
                icmp_type3_pkt_IP_HEADER->ip_sum = cksum(icmp_type3_pkt_IP_HEADER,20);


                /* set ICMP Headers */
                ((sr_icmp_t3_hdr_t*)(icmp_type3_pkt+14+20))->icmp_type = 3;
                ((sr_icmp_t3_hdr_t*)(icmp_type3_pkt+14+20))->icmp_code = 1;
                ((sr_icmp_t3_hdr_t*)(icmp_type3_pkt+14+20))->icmp_sum = 0;

                ((sr_icmp_t3_hdr_t*)(icmp_type3_pkt+14+20))->unused = 0;
                ((sr_icmp_t3_hdr_t*)(icmp_type3_pkt+14+20))->next_mtu = 0;

                

                /* COPY IP packet from original packet into the ICMP packet - upto 28 bytes - check sr_icmp_t3_hdr_t */
                memcpy(icmp_type3_pkt-4-28,pending_pkt_buf_IP_HEADER,28); /* -4 for eth-crc, -28 for data */

                ((sr_icmp_t3_hdr_t*)(icmp_type3_pkt+14+20))->icmp_sum = cksum(icmp_type3_pkt+14+20,sizeof(sr_icmp_t3_hdr_t));
                
                
                /* set source MAC to via_iface MAC */
                memcpy(icmp_type3_pkt+6,via_iface->addr,6);

                /* set destination MAC as source MAC of the original packet*/
                memcpy(icmp_type3_pkt,pending_pkt_buf+6,6);

                /* set EthType field to 0x0800 for IP packets */
                ((sr_ethernet_hdr_t*)icmp_type3_pkt)->ether_type = htons(0x0800);

                sr_send_packet(sr /* borrowed */,
                            icmp_type3_pkt /* borrowed */ ,
                            icmp_type3_pkt_len,
                            via_iface->name /* borrowed */);

                

                printf("%s\n",via_iface->name);

                /*printf("XXX\n");*/
                /*return;*/

            }

            /* free(icmp_type3_pkt); */



            printf("-----> DROPPED ALL PACKETS -> HOST NOT REACHABLE (no ARP REPLY) -> \n");
            printIP(req->ip);


            /* delete the request - all packets related to it will be deleted/dropped */
            sr_arpreq_destroy(cache,req);


            return;
        }
        printf("-----> Sending ARP for n:%d -> ",req->times_sent++);
        printIP(req->ip);
        sr_send_packet(sr /* borrowed */,
                            arp_packet /* borrowed */ ,
                            42,
                            via_iface->name /* borrowed */);
        printf(">------------- ARP SWEEP --------------<\n");

        
    }
   
}

/* You should not need to touch the rest of this code. */

/* Checks if an IP->MAC mapping is in the cache. IP is in network byte order.
   You must free the returned structure if it is not NULL. */
struct sr_arpentry *sr_arpcache_lookup(struct sr_arpcache *cache, uint32_t ip) {
    pthread_mutex_lock(&(cache->lock));
    
    struct sr_arpentry *entry = NULL, *copy = NULL;
    
    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++) {
        if ((cache->entries[i].valid) && (cache->entries[i].ip == ip)) {
            entry = &(cache->entries[i]);
        }
    }
    
    /* Must return a copy b/c another thread could jump in and modify
       table after we return. */
    if (entry) {
        copy = (struct sr_arpentry *) malloc(sizeof(struct sr_arpentry));
        memcpy(copy, entry, sizeof(struct sr_arpentry));
    }
        
    pthread_mutex_unlock(&(cache->lock));
    
    return copy;
}

/* Adds an ARP request to the ARP request queue. If the request is already on
   the queue, adds the packet to the linked list of packets for this sr_arpreq
   that corresponds to this ARP request. You should free the passed *packet.
   
   A pointer to the ARP request is returned; it should not be freed. The caller
   can remove the ARP request from the queue by calling sr_arpreq_destroy. */
struct sr_arpreq *sr_arpcache_queuereq(struct sr_arpcache *cache,
                                       uint32_t ip,
                                       uint8_t *packet,           /* borrowed */
                                       unsigned int packet_len,
                                       char *iface)
{
    pthread_mutex_lock(&(cache->lock));
    
    struct sr_arpreq *req;
    for (req = cache->requests; req != NULL; req = req->next) {
        if (req->ip == ip) {
            break;
        }
    }
    
    /* If the IP wasn't found, add it */
    if (!req) {
        req = (struct sr_arpreq *) calloc(1, sizeof(struct sr_arpreq));
        req->ip = ip;
        req->next = cache->requests;
        cache->requests = req;
    }
    
    /* Add the packet to the list of packets for this request */
    if (packet && packet_len && iface) {
        struct sr_packet *new_pkt = (struct sr_packet *)malloc(sizeof(struct sr_packet));
        
        new_pkt->buf = (uint8_t *)malloc(packet_len);
        memcpy(new_pkt->buf, packet, packet_len);
        new_pkt->len = packet_len;
		new_pkt->iface = (char *)malloc(sr_IFACE_NAMELEN);
        strncpy(new_pkt->iface, iface, sr_IFACE_NAMELEN);
        new_pkt->next = req->packets;
        req->packets = new_pkt;
    }
    
    pthread_mutex_unlock(&(cache->lock));
    
    return req;
}

/* This method performs two functions:
   1) Looks up this IP in the request queue. If it is found, returns a pointer
      to the sr_arpreq with this IP. Otherwise, returns NULL.
   2) Inserts this IP to MAC mapping in the cache, and marks it valid. */
struct sr_arpreq *sr_arpcache_insert(struct sr_arpcache *cache,
                                     unsigned char *mac,
                                     uint32_t ip)
{
    pthread_mutex_lock(&(cache->lock));
    
    struct sr_arpreq *req, *prev = NULL, *next = NULL; 
    for (req = cache->requests; req != NULL; req = req->next) {
        if (req->ip == ip) {            
            if (prev) {
                next = req->next;
                prev->next = next;
            } 
            else {
                next = req->next;
                cache->requests = next;
            }
            
            break;
        }
        prev = req;
    }
    
    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++) {
        if (!(cache->entries[i].valid))
            break;
    }
    
    if (i != SR_ARPCACHE_SZ) {
        memcpy(cache->entries[i].mac, mac, 6);
        cache->entries[i].ip = ip;
        cache->entries[i].added = time(NULL);
        cache->entries[i].valid = 1;
    }
    
    pthread_mutex_unlock(&(cache->lock));
    
    return req;
}

/* Frees all memory associated with this arp request entry. If this arp request
   entry is on the arp request queue, it is removed from the queue. */
void sr_arpreq_destroy(struct sr_arpcache *cache, struct sr_arpreq *entry) {
    pthread_mutex_lock(&(cache->lock));
    
    if (entry) {
        struct sr_arpreq *req, *prev = NULL, *next = NULL; 
        for (req = cache->requests; req != NULL; req = req->next) {
            if (req == entry) {                
                if (prev) {
                    next = req->next;
                    prev->next = next;
                } 
                else {
                    next = req->next;
                    cache->requests = next;
                }
                
                break;
            }
            prev = req;
        }
        
        struct sr_packet *pkt, *nxt;
        
        for (pkt = entry->packets; pkt; pkt = nxt) {
            nxt = pkt->next;
            if (pkt->buf)
                free(pkt->buf);
            if (pkt->iface)
                free(pkt->iface);
            free(pkt);
        }
        
        free(entry);
    }
    
    pthread_mutex_unlock(&(cache->lock));
}

/* Prints out the ARP table. */
void sr_arpcache_dump(struct sr_arpcache *cache) {
    fprintf(stderr, "\nMAC            IP         ADDED                      VALID\n");
    fprintf(stderr, "-----------------------------------------------------------\n");
    
    int i;
    for (i = 0; i < SR_ARPCACHE_SZ; i++) {
        struct sr_arpentry *cur = &(cache->entries[i]);
        unsigned char *mac = cur->mac;
        fprintf(stderr, "%.1x%.1x%.1x%.1x%.1x%.1x   %.8x   %.24s   %d\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ntohl(cur->ip), ctime(&(cur->added)), cur->valid);
    }
    
    fprintf(stderr, "\n");
}

/* Initialize table + table lock. Returns 0 on success. */
int sr_arpcache_init(struct sr_arpcache *cache) {  
    /* Seed RNG to kick out a random entry if all entries full. */
    srand(time(NULL));
    
    /* Invalidate all entries */
    memset(cache->entries, 0, sizeof(cache->entries));
    cache->requests = NULL;
    
    /* Acquire mutex lock */
    pthread_mutexattr_init(&(cache->attr));
    pthread_mutexattr_settype(&(cache->attr), PTHREAD_MUTEX_RECURSIVE);
    int success = pthread_mutex_init(&(cache->lock), &(cache->attr));
    
    return success;
}

/* Destroys table + table lock. Returns 0 on success. */
int sr_arpcache_destroy(struct sr_arpcache *cache) {
    return pthread_mutex_destroy(&(cache->lock)) && pthread_mutexattr_destroy(&(cache->attr));
}

/* Thread which sweeps through the cache and invalidates entries that were added
   more than SR_ARPCACHE_TO seconds ago. */
void *sr_arpcache_timeout(void *sr_ptr) {
    struct sr_instance *sr = sr_ptr;
    struct sr_arpcache *cache = &(sr->cache);
    
    while (1) {
        sleep(1.0);
        
        pthread_mutex_lock(&(cache->lock));
    
        time_t curtime = time(NULL);
        
        int i;    
        for (i = 0; i < SR_ARPCACHE_SZ; i++) {
            if ((cache->entries[i].valid) && (difftime(curtime,cache->entries[i].added) > SR_ARPCACHE_TO)) {
                cache->entries[i].valid = 0;
            }
        }
        
        sr_arpcache_sweepreqs(sr);

        pthread_mutex_unlock(&(cache->lock));
    }
    
    return NULL;
}

