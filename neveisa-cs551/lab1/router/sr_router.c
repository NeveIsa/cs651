/**********************************************************************
 * file:  sr_router.c
 * date:  Mon Feb 18 12:50:42 PST 2002
 * Contact: casado@stanford.edu
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>

#include <stdlib.h>
#include <string.h>


#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);
    
    /* Add initialization code here! */

} /* -- sr_init -- */



void printIP(uint32_t ip)
{
  ip = ntohl(ip);

  printf("IP: %d.%d.%d.%d\n",(ip>>24)&0xff,(ip>>16)&0xff,(ip>>8)&0xff,ip&0xff);
}



void printMAC(char *_mac)
{
    int j=0;
    printf("MAC: ");
    for(j=0;j<6;j++) 
    {
      printf("%02X",_mac[j] & 0xff);
      if(j!=5)printf(":");
    }
    printf("\n");
}

/* find the interface the packet arrived on */
struct sr_if* ip_2_iface(struct sr_instance* sr, uint32_t ip)
{
  ip = ntohl(ip);
  char _ethName[25];

  printf("%u",ip);

  
  struct sr_if* if_walker = 0;
  if_walker = sr->if_list;
    while(if_walker)
    {
       if(ntohl(if_walker->ip)==ip)
        {
          /*printf("-----> IP 2 IFACE\n");
          printIP(if_walker->ip);*/
          /*printMAC((char*)if_walker->addr);*/
          /*printf("%s\n",if_walker->name);
          printf("IP 2 IFACE <-----\n");*/
          return if_walker;
        }
        if_walker = if_walker->next;
    }

  return NULL;  /* did not find the IP in interface */


  /* OLD CODE BELOW - NOT USED NOW - DO NOT DELETE*/

   if(ip==167772417) /* 10.0.1.1 */
    {
      printf("-> IP: 10.0.1.1 - eth3\n");
      strcpy(_ethName,"eth3");
    }
    else if(ip==3232236033)
    {
      printf("-> IP: 192.168.2.1 - eth1\n");
      strcpy(_ethName,"eth1");
    }
    else if(ip==2889876225)
    {
      printf("-> IP: 172.64.3.1 - eth2\n");
      strcpy(_ethName,"eth2");
    }
    else
    {
      print_addr_ip_int(ip);
      printf("\t<----- Target IP not one of my IPs.\n");
      return 0; 
    }

    return sr_get_interface(sr,_ethName);
}


struct sr_if* mac_2_iface(struct sr_instance* sr, uint8_t* mac)
{
  printf("-> Determining INTERFACE from MAC <-\n");

  

  /* FOR LAB  2*/
  struct sr_if* if_walker = 0;
  if_walker = sr->if_list;
    while(if_walker)
    {
       if(memcmp(if_walker->addr,mac,6)==0)
        {
          printf("-----> MAC 2 IFACE\n");
          /*printIP(if_walker->ip);*/
          printf("%s\n",if_walker->name);
          printf("MAC 2 IFACE <-----\n");
          return if_walker;
        }
        if_walker = if_walker->next;
    }

  return NULL;  /* did not find the IP in interface */

  /* FOR LAB 2 */

  struct sr_if* _if;

  _if = ip_2_iface(sr,htonl(167772417));
  if(memcmp(_if->addr,mac,6)==0) return _if;

  _if = ip_2_iface(sr,htonl(3232236033));
  if(memcmp(_if->addr,mac,6)==0) return _if;

  _if = ip_2_iface(sr,htonl(2889876225));
  if(memcmp(_if->addr,mac,6)==0) return _if;

  return 0;
}




uint32_t find_best_route(struct sr_instance* sr,uint32_t dst_ip)
{

  uint8_t debug=0;

  if(debug)
  {
    printf("\n=======================================\n");
    printf("FINDING BEST ROUTE FOR THE ");printIP(dst_ip);printf("\n");
  }


  struct sr_rt* route=0;
  struct sr_rt* best_route=0;

  for(route=sr->routing_table;route!=NULL;route=route->next)
  {
    
    /* apply mask to the dst_ip */
    uint32_t masked_dst_ip = dst_ip & (route->mask).s_addr;

    /* compare this with the route->dest IP - must match for a valid route */
    if(masked_dst_ip == (route->dest).s_addr)
    {
      if(debug)
      {
      printf("Found one matching route -> \n");
      printf("Masked Destination IP -> "); printIP(masked_dst_ip);
      printf("Gateway IP -> ");printIP((route->gw).s_addr);
      sr_print_routing_entry(route);
      }

      /* now compare if the previous best route had longer prefix - this can be done by comparing their masks */
      if(best_route==0) 
      {
        best_route = route; /* set best_route to the first found match */
        if(debug)printf("Updating best_route to the first matching route.\n");
      }
      else
      {
        
        if( ntohl((best_route->mask).s_addr) < ntohl((route->mask).s_addr) )
        {
          if(debug)printf("Updating best_route b/c of Longest Prefix Match poilicy.\n");
          best_route = route;
        }
        else
        {
          if(debug)printf("NOT the best ROUTE b/c of Longest Prefix Match poilicy.\n");
        }
      }

      if(debug)printf("\n");
      
    }

  }

  if(best_route==0)
  {
    if(debug)printf("-----> NO ROUTES FOUND FOR ");printIP(dst_ip);
    return 0;
  }

  if(debug)
  {
    printf("\nFound Best Route For ->");
    printIP(dst_ip);
    printf("\n");

    printf("DEST\t\tGW\t\tMASK\tINTERFACE\n");
    sr_print_routing_entry(best_route);

    printf("\n=======================================\n");
  }

  return (best_route->gw).s_addr;

  
/*
  struct in_addr dest;
    struct in_addr gw;
    struct in_addr mask;
    char   interface[sr_IFACE_NAMELEN];
    struct sr_rt* next;
    */
}




uint32_t target_ip_2_network(uint32_t t_ip)
{
  uint32_t network = t_ip & 0xffffff;

  printf("Match IP-NETWORK ->");
  printIP(t_ip);
  printIP(network);
  
  return network;
}

struct sr_if* target_ip_2_iface(struct sr_instance* sr, uint32_t t_ip)
{

  uint32_t dst_ip = t_ip;

uint8_t debug=0;

  if(debug)
  {
    printf("\n=======================================\n");
    printf("FINDING BEST ROUTE FOR THE ");printIP(dst_ip);printf("\n");
  }


  struct sr_rt* route=0;
  struct sr_rt* best_route=0;

  for(route=sr->routing_table;route!=NULL;route=route->next)
  {
    
    /* apply mask to the dst_ip */
    uint32_t masked_dst_ip = dst_ip & (route->mask).s_addr;

    /* compare this with the route->dest IP - must match for a valid route */
    if(masked_dst_ip == (route->dest).s_addr)
    {
      if(debug)
      {
      printf("Found one matching route -> \n");
      printf("Masked Destination IP -> "); printIP(masked_dst_ip);
      printf("Gateway IP -> ");printIP((route->gw).s_addr);
      sr_print_routing_entry(route);
      }

      /* now compare if the previous best route had longer prefix - this can be done by comparing their masks */
      if(best_route==0) 
      {
        best_route = route; /* set best_route to the first found match */
        if(debug)printf("Updating best_route to the first matching route.\n");
      }
      else
      {
        
        if( ntohl((best_route->mask).s_addr) < ntohl((route->mask).s_addr) )
        {
          if(debug)printf("Updating best_route b/c of Longest Prefix Match poilicy.\n");
          best_route = route;
        }
        else
        {
          if(debug)printf("NOT the best ROUTE b/c of Longest Prefix Match poilicy.\n");
        }
      }

      if(debug)printf("\n");
      
    }

  }

  if(best_route==0)
  {
    if(debug)printf("-----> NO ROUTES FOUND FOR ");printIP(dst_ip);
    return 0;
  }

  if(debug)
  {
    printf("\nFound Best Route For ->");
    printIP(dst_ip);
    printf("\n");

    printf("DEST\t\tGW\t\tMASK\tINTERFACE\n");
    sr_print_routing_entry(best_route);

    printf("\n=======================================\n");
  }

  return sr_get_interface(sr,best_route->interface);

  /* FOR LAB  2*/
  /*
  struct sr_if* if_walker = 0;
  if_walker = sr->if_list;
    while(if_walker)
    {
       if(memcmp(if_walker->addr,mac,6)==0)
        {
          printf("-----> MAC 2 IFACE\n");
         
          printf("%s\n",if_walker->name);
          printf("MAC 2 IFACE <-----\n");
          return if_walker;
        }
        if_walker = if_walker->next;
    }

  return NULL; */  /* did not find the IP in interface */

  /* FOR LAB 2 */




  uint32_t net = target_ip_2_network(t_ip);
  uint32_t iface_ip = htonl(ntohl(net)+1); /*convert to host byte order > add 1 > convert back to network order */
  
  return ip_2_iface(sr,iface_ip);

}


void forward_ip_packet(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  
  /* NOTE THAT SINCE THE LOGIC IS COPIED FROM THE ICMP REPLY PACKET FORWARDING LOGIC,
  THE VARIABLE NAMES ARE STILL CONTAIN ICMP in them. HOWEVER THE FORWARDING IS TOTALLY GENERIC TO ANY IP 
  PACKET, not limited to only ICMP REPLY packets */

  /*
  sr_print_routing_table(sr);
  return;*/

          sr_ip_hdr_t *IP_packet = (sr_ip_hdr_t*)(packet+14);
          

          /* UNCOMMENT below to test the Longest Prefix Match Algorithm */
          /* find_best_route(sr,IP_packet->ip_dst);return; */



            /* make a copy of the received ethernet frame */
          uint8_t* FORWARDING_ICMP_REP_Ethernet_Frame = malloc(len);
          memcpy(FORWARDING_ICMP_REP_Ethernet_Frame,packet,len);

         

          /* decrease TTL in the IP Packer */
          sr_ip_hdr_t* FORWARDING_ICMP_REP_IP_Frame = (sr_ip_hdr_t*)(FORWARDING_ICMP_REP_Ethernet_Frame+14);

           /* Check if TTL is 1 or 0. 
             If 1 or 0, then send ICMP type 11 and do not forward the packet */
          if(FORWARDING_ICMP_REP_IP_Frame->ip_ttl<=1)
          {
              

            /* Send ICMP type 11 */
            uint32_t icmp_pkt_type11_len = 14+ 20 + sizeof(sr_icmp_hdr_t)+4 + 64 + 4;
             /* 14 for ethernet header 
               20 for IP header
               sr_icmp_hdr_t = 4 bytes
               4 bytes filled with zeroes after sr_icmp_hdr_t - http://www.networksorcery.com/enp/protocol/icmp/msg11.htm
               64 bytes of original IP datagram after the sr_icmp_hdr_t
               ethernet trailer crc - 4
               */

            uint8_t *icmp_pkt_type11 = calloc(icmp_pkt_type11_len ,1); /* init with zeroes */


              /* set src as my own router MAC - which is also destination MAC from received packet */
              memcpy(icmp_pkt_type11+6,packet,6);

              /* set dst MAC as the incomming packets src MAC */
              memcpy(icmp_pkt_type11,packet+6,6);


              /* Set the EthType to that of IP - i.e 0x0800 */
              ((sr_ethernet_hdr_t*) icmp_pkt_type11)->ether_type = htons(0x0800);


              
              sr_ip_hdr_t *icmp_pkt_type11_IP_HEADER = (sr_ip_hdr_t*)(icmp_pkt_type11+14);

              /*copy the IP header from IP_packet to icmp_pkt_type11_IP_HEADER - so that its easier to 
                manipulate rather than filling all trivial fields of IP packet ourselves */
              memcpy(icmp_pkt_type11_IP_HEADER,IP_packet,sizeof(sr_ip_hdr_t));


              /* set destination IP as the received packets source IP */
              icmp_pkt_type11_IP_HEADER->ip_dst =  IP_packet->ip_src;


              /* set source IP as IP of the router */
                            /* we have to send this ICMP packet back to the source IP, lets get the interface*/
                            /*struct sr_if* forward_via_my_ifce = target_ip_2_iface(sr,IP_packet->ip_src);*/
                            struct sr_if* forward_via_my_ifce = sr_get_interface(sr,interface); /* we know the interface it came from */
                            
                            /* return if IFACE not found -> drop the packet*/
                            if(!forward_via_my_ifce) return;
              icmp_pkt_type11_IP_HEADER->ip_src = forward_via_my_ifce->ip;

              /* set TTL to 64 - Note this is the TTL of the IP packet we are sending back, this 
              has nothing to do with the TTL we received from the IP_packet whose value is 1 or 0 */
             icmp_pkt_type11_IP_HEADER->ip_ttl=64;
              
              /* Set IP Protocol type to 1 for ICMP */
              icmp_pkt_type11_IP_HEADER->ip_p = 1;

              /* set IP length */
              /* however setting the length makes the traceroute 1st hop to router trace fail,
               hence commenting it*/
              /*icmp_pkt_type11_IP_HEADER->ip_len = 20+8+64; */
              /* 20 IP header, 8 ICMP headr, 64 ICMP data */



              /* calculate the IP header checksum and insert */
              /* while calculating the checksum, the checksum filed in the IP header must be set to zero */
              /* after calculating checksum, update the checksum field */
              icmp_pkt_type11_IP_HEADER->ip_sum = 0;
              icmp_pkt_type11_IP_HEADER->ip_sum = cksum(icmp_pkt_type11_IP_HEADER,20);

              /* SET ICMP HEADERS */
              sr_icmp_hdr_t* icmp_pkt_type11_ICMP_HEADER = (sr_icmp_hdr_t*)(icmp_pkt_type11 + 14 + 20);

              icmp_pkt_type11_ICMP_HEADER->icmp_type=11;
              icmp_pkt_type11_ICMP_HEADER->icmp_code=0;


              /* COPY 64 bytes from the received IP_packet into the reply ICMP packet - used for matching */
              memcpy(icmp_pkt_type11+icmp_pkt_type11_len-64-4,IP_packet,64); /* -4 for ethernet crc, -64 for the data to be copied */

              /* set ICMP Checksum - checksum calculated over all of ICMP message including header and data*/
              icmp_pkt_type11_ICMP_HEADER->icmp_sum = 0;
              icmp_pkt_type11_ICMP_HEADER->icmp_sum = cksum(icmp_pkt_type11+14+20,sizeof(sr_icmp_hdr_t)+4 + 64);

              /* Set the CRC */
              /* MY GUESS IS THE CRC IS NOT STRICTLY CHECKED BY THE END-HOSTS, 
              hence we are able to get away without packets being dropped even when CRC is wrong */ 

                  /* send the packet */
                  sr_send_packet(sr /* borrowed */,
                         icmp_pkt_type11 /* borrowed */ ,
                         icmp_pkt_type11_len,
                         forward_via_my_ifce->name /* borrowed */);

                          /* send the packet */
                  sr_send_packet(sr /* borrowed */,
                         icmp_pkt_type11 /* borrowed */ ,
                         icmp_pkt_type11_len,
                         forward_via_my_ifce->name /* borrowed */);



            return;
          }

          FORWARDING_ICMP_REP_IP_Frame->ip_ttl--;

          /* update the IP checksum 
          Due to the decrease in in TTL by one, we can add one to previous checksum 
          WHAT I dont understand is why I do not have to use ntohs and htons

          HOWEVER FORWARDING_ICMP_REP_IP_Frame->ip_sum++; seems to work
          */
          FORWARDING_ICMP_REP_IP_Frame->ip_sum++;

          

          uint32_t __sip = ntohl(FORWARDING_ICMP_REP_IP_Frame->ip_src);
          uint32_t __tip = ntohl(FORWARDING_ICMP_REP_IP_Frame->ip_dst);

          char __sip_string[16],__tip_string[16];
          sprintf(__sip_string,"%d.%d.%d.%d",(__sip>>24)&0xff,(__sip>>16)&0xff,(__sip>>8)&0xff,__sip&0xff);
          sprintf(__tip_string,"%d.%d.%d.%d",(__tip>>24)&0xff,(__tip>>16)&0xff,(__tip>>8)&0xff,__tip&0xff);
          printf("-----> Forwarding IP PACKET from %s to %s\n",__sip_string,__tip_string);


          /* GET ROUTING DESTINATION IP */
          uint32_t ROUTE_2_IP;

          /*printf("\n=============================================================\n");*/
          /* dummy 
          ROUTE_2_IP = find_best_route(sr,IP_packet->ip_dst);*/

          /*printIP(ROUTE_2_IP);*/

          /* OLD CODE - WORKS but cannot generalise much - has to be used with the checks for extended topology IPs
          for manual - hrdcded routing*/

          /* ROUTE_2_IP = IP_packet->ip_dst; */

            /* check if extndd tplgy */
           /*
          {'server1': '192.168.2.2', 'sw0-eth3': '10.0.1.1', 'sw0-eth1': '192.168.2.1',
           'sw0-eth2': '172.64.3.1', 'client': '10.0.1.100', 'server2': '172.64.3.10', 
           'server4-eth0': '172.64.3.34', 'server3-eth0': '172.64.3.18',
            'server2-eth1': '172.64.3.17', 'server2-eth2': '172.64.3.33'}
            */
          /*server-2 - 172.64.3.10 is the next hop router for these IPs */
          /*
          uint32_t etxndd_tplgy_ip_diff = ntohl(ROUTE_2_IP)-2889876224;
          if(etxndd_tplgy_ip_diff==17 || etxndd_tplgy_ip_diff==18 || etxndd_tplgy_ip_diff==33 || etxndd_tplgy_ip_diff==34)
          {
            ROUTE_2_IP = htonl(2889876224+10); 
          }
          */

          /*printIP(ROUTE_2_IP);

          printf("\n=============================================================\n");*/

          /* NEW ROUTE/GATEWAY SELECTION using Longest Prefix Match */
          ROUTE_2_IP = find_best_route(sr,IP_packet->ip_dst);
          


          /* find the forward INTERFACE for the target IP  - LAB2 MOD */
          struct sr_if* forward_via_my_ifce = target_ip_2_iface(sr,ROUTE_2_IP);
          /*struct sr_if* forward_via_my_ifce = sr_get_interface(sr,BEST_ROUTE_ENTRY->interface);*/

          
          /* return if IFACE not found -> drop the packet*/
          if(!forward_via_my_ifce) return;

          

          /* insert client MAC here to test - client ping server1 */
          /*sr_arpcache_insert(&(sr->cache),(unsigned char*)"\xe6\x95\x4b\xb2\x16\xb8",IP_packet->ip_dst);*/

          /* lookup for ARP entry */
          struct sr_arpentry *target_arp_entry = sr_arpcache_lookup(&(sr->cache), ROUTE_2_IP);

          
          if(target_arp_entry==NULL)
          {
            /* if no entry for the target IP - insert packet to arp_queue_request */
            printf("\n-----> QUEUING ARP - MAC not found for -> "); 
            printIP(ROUTE_2_IP);

            sr_arpcache_queuereq(&(sr->cache),
                         ROUTE_2_IP,
                         (uint8_t*) FORWARDING_ICMP_REP_Ethernet_Frame,               /* borrowed */
                         len,
                         forward_via_my_ifce->name);

            return;
          }
          else
          {
            printf("\n-----> FOUND ARP entry for -> ");
            printIP(target_arp_entry->ip);

            /* replace the source MAC to one of the Router's interface */
            memcpy(FORWARDING_ICMP_REP_Ethernet_Frame+6,forward_via_my_ifce->addr,6);
            
            /* replace the destination MAC with the target node */
            memcpy(FORWARDING_ICMP_REP_Ethernet_Frame,target_arp_entry->mac,6); /* destination MAC is first 6 bytes in MAC frame */

          
          /* send the packet */
           sr_send_packet(sr /* borrowed */,
                         FORWARDING_ICMP_REP_Ethernet_Frame /* borrowed */ ,
                         len,
                         forward_via_my_ifce->name /* borrowed */);

          }
}


/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
  /* REQUIRES */
  assert(sr);
  assert(packet);
  assert(interface);

  sr_ethernet_hdr_t *ehdr = (sr_ethernet_hdr_t *)packet;
  enum sr_ethertype ethType=ntohs(ehdr->ether_type);


/* time_t is arithmetic time type */
	time_t now;
	/*Obtain current time
	 time() returns the current time of the system as a time_t value*/
	time(&now);

	/* Convert to local time format and print to stdout */
	printf("\n\n%s-------------------------\n", ctime(&now));
  /* DETECT AND PRINT PACKET INFO */
  printf("-----> Received packet of length:%d on interface:%s of ethType:%d \n",len,interface,ethType);
  print_hdrs(packet,len);
  printf("\n");



  /* ARP LOGIC */
  if(ethType==ethertype_arp)
  {

  sr_arp_hdr_t* ARP_Header =  (sr_arp_hdr_t*)(packet+14); /* 6dest, 6src, 2ethType */
  
  enum sr_arp_opcode ARP_OPCODE = ntohs(ARP_Header->ar_op); /* must be 1 for req, 2 for reply */
  enum sr_arp_hrd_fmt ARP_HW_FMT= ntohs(ARP_Header->ar_hrd); /* must be 1 for ethernet */
  
  if(ARP_HW_FMT!=arp_hrd_ethernet) printf("xxx -> Unknown ARP_HW_FMT\n");

  if(ARP_OPCODE==arp_op_request) 
  {
    

    printf("---> ARP Request.\n");

    /* FOR UNLAB 2 
    char _ethName[25];
    unsigned long ip = ntohl(ARP_Header->ar_tip);

    if(ip==167772417) 
    {
      printf("-> target IP: 10.0.1.1 - eth3\n");
      strcpy(_ethName,"eth3");
    }
    else if(ip==3232236033)
    {
      printf("-> target IP: 192.168.2.1 - eth1\n");
      strcpy(_ethName,"eth1");
    }
    else if(ip==2889876225)
    {
      printf("-> target IP: 172.64.3.1 - eth2\n");
      strcpy(_ethName,"eth2");
    }
    else
    {
      printf("-> Target IP:");
      print_addr_ip_int(ip);
      printf("| not one of my IPs.\n");
      return;
    }

     FOR UNLAB 2 */

    /* MAKE ARP REPLY */

    uint8_t ARP_Reply[42]; /* ETHERNET PACKET */
    memcpy(ARP_Reply,packet,len); /* copy the ARP request frame */ 

    uint8_t srcMAC[6]; /*this is the source in the received packet - needs to be the destination in our reply */
    memcpy(srcMAC,packet+6,6);
    memcpy(ARP_Reply,srcMAC,6); /* change MAC destination */

    /* get the MAC from interface name */
    /* FOR UNLAB 2 
    struct sr_if* ifce=sr_get_interface(sr, _ethName);
     FOR UNLAB 2 */

    /* FOR LAB 2 */
    struct sr_if* ifce=ip_2_iface(sr,ARP_Header->ar_tip);
    /* FOR LAB 2 */
    

    /* LAB 2 MOD
    assert(ifce); */
    if(!ifce) return; /* return if interface not found */
    
    memcpy(ARP_Reply+6,ifce->addr,6); /* change MAC source */

    

    /* change ARP OPCODE to reply */
    sr_arp_hdr_t* ARP_Reply_Header =  (sr_arp_hdr_t*)(ARP_Reply+14);
    ARP_Reply_Header->ar_op = htons(arp_op_reply);

    /* exchange source and target IPs in ARP headers */
    ARP_Reply_Header->ar_sip = ARP_Header->ar_tip;
    ARP_Reply_Header->ar_tip = ARP_Header->ar_sip;

    /* exchange sender hardware/MAC address and target hardware/MAC address in ARP header */
    memcpy(ARP_Reply_Header->ar_sha,ifce->addr,6); /* put src MAC*/
    memcpy(ARP_Reply_Header->ar_tha,srcMAC,6);

    printf("\n-----> Sending ARP REPLY Frame\n");
    print_hdrs(ARP_Reply,42);



    /* Send the ETHERNET FRAME */
    sr_send_packet(sr, ARP_Reply , 42, ifce->name);

  }
  else if(ARP_OPCODE==arp_op_reply)
  {
    printf("---> ARP Reply.\n");
    
    /* check if intended to my IP and from the same network*/
    {
      /* LAB 2 MOD */
      struct sr_if* src_ifce = sr_get_interface(sr,interface); /*target_ip_2_iface(sr,ARP_Header->ar_sip);*/ /* find which iface the packet arrived from */
      struct sr_if* tgt_ifce = ip_2_iface(sr,ARP_Header->ar_tip); /* which iface or the router is the ARP targetted to */

      /* drop the packet if any of the interfaces is not found, i.e a packet which is from a network 
      which I do not have my interface connected to,
      or a target IP address which is not mine.
       In addition also drop the packet if the network from which the ARP Reply came from
       is replying(to my earlier ARP req) to my IP which is in a different network */
      if(src_ifce==NULL || tgt_ifce==NULL || strcmp(src_ifce->name,tgt_ifce->name)) return;


      /* SAVE to the routing table data structure */
      struct sr_arpreq *req = sr_arpcache_insert(&(sr->cache),
                                     ARP_Header->ar_sha,
                                     ARP_Header->ar_sip);  


      /* if no requests are pending on this IP->MAC mapping, return */
      if(req==NULL)
      {
        printf("NO REQ. PENDING\n");
        return;
      }


    
      struct sr_packet *pkt; /* init */
      for(pkt=req->packets;pkt!=NULL;pkt=pkt->next)
      {

        /* set source MAC */
        memcpy(pkt->buf+6,sr_get_interface(sr,pkt->iface)->addr,6);

        memcpy(pkt->buf,ARP_Header->ar_sha,6); /* set destination to the recently found sender's MAC from ARP Reply */

        /* send the packet */
                  sr_send_packet(sr /* borrowed */,
                         pkt->buf /* borrowed */ ,
                         pkt->len,
                         pkt->iface /* borrowed */);
      }


        /* delete the request after sending all the packets queues for that ARP request */
        sr_arpreq_destroy(&(sr->cache),req);
               


    }


    

  }
  else printf("xxx -> Unknown ARP_CODE\n");

  /*print_hdr_arp((uint8_t *)ARP_Header);
  sr_arpcache_insert(&(sr->cache))
  sr_arpcache_dump(&(sr->cache));*/
   
  }
  /* ARP LOGIC */

  /* IP LOGIC */
  else if(ethType==ethertype_ip)
  {
    printf("-> Got IP \n");
    sr_ip_hdr_t *IP_packet = (sr_ip_hdr_t*)(packet+14);
    /* print_hdr_ip((uint8_t*) IP_packet); */

    if(IP_packet->ip_p==ip_protocol_icmp)
    {
      printf("-> Got ICMP\n");
      sr_icmp_hdr_t *ICMP_packet = (sr_icmp_hdr_t*)(packet+14+20);


      /*print_hdr_icmp((uint8_t*)ICMP_packet);*/
      

      if(ICMP_packet->icmp_type==8) /* echo request -> type 8 */
      {
        
        /* check if ping request is for my IP */
        struct sr_if* _myifce = ip_2_iface(sr,IP_packet->ip_dst);

        /*if myiface is null, then the ping is for ANOTHER DEVICE, not me*/
        /* DO SOMETHING ELSE -> FORWARD THE PACKET TO THE RIGHT HOST  */
        /* TAKE CARE TO -
        1. Change destination MAC to correct host(use ARP to get MAC from IP)
        2. Decrement the TTL by one
        3. Recalculate the ICMP checksum - may use tricks like adding 1 to previous checksum because of TTL--
        */
        if(_myifce==0)
        {
          printf("-> Got ICMP Echo Req. for another node -> ");
          printIP(IP_packet->ip_dst);

          forward_ip_packet(sr,packet,len,interface);
          return;
          
        } 

        printf("-> Got ICMP Echo Req. for one of my interfaces: %s\n",_myifce->name);

        /* copy the Ethernet frame */
        uint8_t* ICMP_Reply_Eth_Frame = malloc(len);
        memcpy(ICMP_Reply_Eth_Frame,packet,len);
        

        /* modify the type from 8 to 0 */
        sr_icmp_hdr_t *ICMP_Reply_ICMP_Frame = (sr_icmp_hdr_t*)(ICMP_Reply_Eth_Frame+14+20);
        ICMP_Reply_ICMP_Frame->icmp_type = 0; /* set to 0 for reply */
        

        /* exchange IP addresses */
        sr_ip_hdr_t *ICMP_Reply_IP_Frame = (sr_ip_hdr_t*)(ICMP_Reply_Eth_Frame+14);
        ICMP_Reply_IP_Frame->ip_src = IP_packet->ip_dst; /* set source IP */
        ICMP_Reply_IP_Frame->ip_dst = IP_packet->ip_src; /* set destination IP */

        

         /* set checksum -
         this trick only works for ICMP ping requests and responses -
         Since in a ping reply the ICMP type field changes from 8(in request) to 0(in reply),
         the checksum only changes by 0x0800.
         Now taking care of endianness, we can just add 0x0800 to the original checksum received*/
        ICMP_Reply_ICMP_Frame->icmp_sum = htons(ntohs(ICMP_packet->icmp_sum) + 0x0800);
        

        /* exchange MAC addresses in Ethernet Frame */
        memcpy(ICMP_Reply_Eth_Frame,packet+6,6); /* set dest */
        memcpy(ICMP_Reply_Eth_Frame+6,packet,6); /* set src */
        /* mac2_iface compares first 6 bytes of packet, which is also the destination MAC of the router */
        printf("ICMP received on the interface:%s\n",mac_2_iface(sr,packet)->name); 
        printMAC((char*)mac_2_iface(sr,packet)->addr);
        
      printf("-----> Sending ICMP Ping Reply on interface: %s for Ping request on ",mac_2_iface(sr,packet)->name);
   

      /* send the packet */
      sr_send_packet(sr /* borrowed */,
                         ICMP_Reply_Eth_Frame /* borrowed */ ,
                         len,
                         mac_2_iface(sr,packet)->name /* borrowed */);
        

        /*print_hdrs(ICMP_Reply_Eth_Frame,len);*/
        return;
   
      }


      else if(ICMP_packet->icmp_type==0) /* echo reply -> type 0 */
      {
          printf("-> GOT ICMP Echo Reply - forwarding to ");
          printIP(IP_packet->ip_dst);

          forward_ip_packet(sr,packet,len,interface);
          return;
       

      }


    } /* END of if ICMP */


    /* IF IP packet is not ICMP */

    /* check if the IP packet is for one of my IP, if it is drop it and send 
    an ICMP Port unreachable (type 3, code 3)*/

    if(ip_2_iface(sr,IP_packet->ip_dst)==NULL); /* not for my IP*/
    else 
    {
      /* Make an ICMP Type 3 Code 3 Packet and send */

      uint8_t* ICMP_T3C3_Eth_Packet = malloc(14+20+sizeof(sr_icmp_t3_hdr_t) + 4); /* total 74*/
      sr_ip_hdr_t* ICMP_T3C3_IP_Packet = (sr_ip_hdr_t*)(ICMP_T3C3_Eth_Packet+14);
      sr_icmp_t3_hdr_t* ICMP_T3C3_ICMP_Packet = (sr_icmp_t3_hdr_t*)(ICMP_T3C3_Eth_Packet+14+20);

      /* switch the src MAC and dst MAC */
      memcpy(ICMP_T3C3_Eth_Packet,packet+6,6); /* copy src as dst */
      memcpy(ICMP_T3C3_Eth_Packet+6,packet,6); /* copy dst as src */

      /*set eth headers */
      ((sr_ethernet_hdr_t*)ICMP_T3C3_Eth_Packet)->ether_type = htons(0x0800) ; /* for IP */

      /* copy IP headers */
      memcpy(ICMP_T3C3_Eth_Packet+14,packet+14,20);

      /* switch src IP and dst IP */
      ICMP_T3C3_IP_Packet->ip_dst = IP_packet->ip_src;
      ICMP_T3C3_IP_Packet->ip_src = IP_packet->ip_dst;

      /* set IP headers */
      ICMP_T3C3_IP_Packet->ip_len = htons(20+sizeof(sr_icmp_t3_hdr_t)); /* 20 for header + 36 for ICMP */
      ICMP_T3C3_IP_Packet->ip_ttl = 64;
      ICMP_T3C3_IP_Packet->ip_p = 1; /* 1 for ICMP */
      ICMP_T3C3_IP_Packet->ip_sum = 0;
      ICMP_T3C3_IP_Packet->ip_sum = cksum(ICMP_T3C3_IP_Packet,20); /* calc checksum for IP */

    
      /* Fill the ICMP Headers */
      ICMP_T3C3_ICMP_Packet->icmp_type = 3;
      ICMP_T3C3_ICMP_Packet->icmp_code = 3;
      ICMP_T3C3_ICMP_Packet->icmp_sum = 0;
      ICMP_T3C3_ICMP_Packet->unused = 0;
      ICMP_T3C3_ICMP_Packet->next_mtu = 0;
      ICMP_T3C3_ICMP_Packet->icmp_sum = cksum(ICMP_T3C3_ICMP_Packet,sizeof(sr_icmp_t3_hdr_t));

      /* copy data from the original packet into the ICMP data field */
      memcpy(ICMP_T3C3_ICMP_Packet,IP_packet,28);


       /* send the packet */
      sr_send_packet(sr /* borrowed */,
                          ICMP_T3C3_Eth_Packet /* borrowed */ ,
                         74,
                         mac_2_iface(sr,ICMP_T3C3_Eth_Packet)->name /* borrowed */);




      
      

      return; /* return immediately*/
    }
    
    forward_ip_packet(sr,packet,len,interface);
    return;
    

  }
  /* IP LOGIC */

  /* OTHER FRAMES */
  else
  {
    printf("xxx -> Got Unknown EtherType \n");
  }
  /* OTHER FRAMES */

  

  /* fill in code here */

}/* end sr_ForwardPacket */

