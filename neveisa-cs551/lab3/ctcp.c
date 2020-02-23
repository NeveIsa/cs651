/******************************************************************************
 * ctcp.c
 * ------
 * Implementation of cTCP done here. This is the only file you need to change.
 * Look at the following files for references and useful functions:
 *   - ctcp.h: Headers for this file.
 *   - ctcp_iinked_list.h: Linked list functions for managing a linked list.
 *   - ctcp_sys.h: Connection-related structs and functions, cTCP segment
 *                 definition.
 *   - ctcp_utils.h: Checksum computation, getting the current time.
 *
 *****************************************************************************/

#include "ctcp.h"
#include "ctcp_linked_list.h"
#include "ctcp_sys.h"
#include "ctcp_utils.h"

long current_time_us() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000 + tv.tv_usec;
}

char bbr_txt_name[100];

void ship_ack(ctcp_state_t *state, uint32_t ackno);

int tx_flush_conn(ctcp_state_t *state);

#define BBR_MAXBW_FILTER_SIZE   10
#define BBR_MINRTT_FILTER_SIZE  10

/**
 * Connection state.
 *
 * Stores per-connection information such as the current sequence number,
 * unacknowledged packets, etc.
 *
 * You should add to this to store other fields you might need.
 */


/* HELPERS */


#define TEST 1

int DEBUG_ALL=2;
int DEBUG_WARNING=1;

int DEBUG=0;


void print_debug(char* msg)
{
  fprintf(stderr, "%s", msg);
}

typedef struct {
  uint32_t         num_xmits;
  long             timestamp_of_last_send;
  long             bbr_pacing_sendtime;
  long             bbr_actual_sendtime;
  unsigned long    bytes_acked_till_this_packet;
  ctcp_segment_t   ctcp_segment;
} wrapped_ctcp_segment_t;

/* HELPERS */




struct ctcp_state {
  struct ctcp_state *next;  /* Next in linked list */
  struct ctcp_state **prev; /* Prev in linked list */

  conn_t *conn;             /* Connection object -- needed in order to figure
                               out destination when sending */
  linked_list_t *segments;  /* Linked list of segments sent to this connection.
                               It may be useful to have multiple linked lists
                               for unacknowledged segments, segments that
                               haven't been sent, etc. Lab 1 uses the
                               stop-and-wait protocol and therefore does not
                               necessarily need a linked list. You may remove
                               this if this is the case for you */



  /* FIXME: Add other needed fields. */

  /* Konf */
  ctcp_config_t *cfg;


  linked_list_t *tx_segments_unacked;
  linked_list_t *rx_segments_buffered;

  wrapped_ctcp_segment_t *tx_seg_last_added;

  wrapped_ctcp_segment_t *tx_seg_last_acked;
  wrapped_ctcp_segment_t *rx_seg_last_output;


  uint32_t rx_fin; // set if we receive FIN from the other end 
  int tx_eof; // set if ctpc_read gets EOF


  unsigned long seqno_last_shipped;
  unsigned long receiver_has_acked_seqno;

  unsigned long next_ackno;


  /* BBR STATES */

    uint8_t BBR_STATE;
    float CWND_GAIN;
    float PACING_GAIN;

    float BBR_RTT[BBR_MINRTT_FILTER_SIZE]; //milliseconds
    uint32_t BBR_BW[BBR_MAXBW_FILTER_SIZE]; //bytes per millisecond

    uint8_t bbr_rttfilter_current_index;
    uint8_t bbr_bwfilter_current_index;

    long BBR_lastpacket_pacing_sendtime; //when last packet was sent

    uint32_t BBR_BW_LAST_MAX;
    float BBR_RTT_LAST_MIN;
  
  /* BBR STATES */


 /* Konf */

};

#include "ctcp_bbr.h"

/**
 * Linked list of connection states. Go through this in ctcp_timer() to
 * resubmit segments and tear down connections.
 */
static ctcp_state_t *state_list;

/* FIXME: Feel free to add as many helper functions as needed. Don't repeat
          code! Helper functions make the code clearer and cleaner. */


ctcp_state_t *ctcp_init(conn_t *conn, ctcp_config_t *cfg) {
  /* Connection could not be established. */
  if (conn == NULL) {
    return NULL;
  }

  if (DEBUG>=DEBUG_ALL)
  {
    print_debug("---> Init conn..\n");
    
  }

  /* Established a connection. Create a new state and update the linked list
     of connection states. */
  ctcp_state_t *state = calloc(sizeof(ctcp_state_t), 1);
  state->next = state_list;
  state->prev = &state_list;
  if (state_list)
    state_list->prev = &state->next;
  state_list = state;

  /* Set fields. */
  state->conn = conn;
  /* FIXME: Do any other initialization here. */

  /* add config*/
  state->cfg = cfg;

  state->tx_seg_last_added=NULL; //last added to tx_segments_unacked
  state->tx_seg_last_acked=NULL;


  state->rx_seg_last_output=NULL;


  state->rx_fin=0; // set if we receive FIN from the other end 
  state->tx_eof=0; // set if ctpc_read gets EOF

  state->seqno_last_shipped=0; //init sequence number

  state->receiver_has_acked_seqno=0;

  state->next_ackno=1; //ackno to be sent = expected seqno of next packet;
  //state->next_ackno=0; //ackno to be sent = expected seqno of next packet;


  state->tx_segments_unacked = ll_create();
  state->rx_segments_buffered = ll_create();

  //BBR STUFF
    state->BBR_STATE = BBR_STARTUP;
    state->CWND_GAIN = 1;
    state->PACING_GAIN = 1;

    state->bbr_rttfilter_current_index=0;
    state->bbr_bwfilter_current_index=0;

    uint8_t i=0;

    for(i=0;i<BBR_MAXBW_FILTER_SIZE;i++)
    {
      state->BBR_BW[i] = 0; //smallest positive number
    }
    state->BBR_BW[0] = 2; //start with 2kbps =  2 byte per millisecond
    state->BBR_BW_LAST_MAX = 0;

    for(i=0;i<BBR_MINRTT_FILTER_SIZE;i++)
    {
      state->BBR_RTT[i] = 99999; // a big number 
    }
    state->BBR_RTT[0] = 1; //start with 1 millisecond
    state->BBR_RTT_LAST_MIN = 99999;


    state->BBR_lastpacket_pacing_sendtime = current_time();
  //BBR STUFF

  sprintf(bbr_txt_name,"bbr_%d.txt",rand());
  return state;
}

void ctcp_destroy(ctcp_state_t *state) {
  /* Update linked list. */
  if (state->next)
    state->next->prev = state->prev;

  *state->prev = state->next;
  conn_remove(state->conn);

  /* FIXME: Do any other cleanup here. */

  ll_destroy(state->tx_segments_unacked);
  ll_destroy(state->rx_segments_buffered);

  free(state->rx_seg_last_output);
  free(state->cfg);

  free(state);
  end_client();
}

void ctcp_read(ctcp_state_t *state) {
  /* FIXME */

  //uint16_t SEND_WINDOW_SIZE = state->cfg->send_window; //UPDATE this from the RCV_WINDOW_SIZE of the other host
  char stdin_buf[MAX_SEG_DATA_SIZE+1];
  
  
  int stdin_len=0;

  
  
  while((stdin_len=conn_input(state->conn,stdin_buf,MAX_SEG_DATA_SIZE)) > 0) //as long as data is not empty
  {
    /************* DEBUG ***********/
      char debug_msg[100];
      sprintf(debug_msg,"---> stdin - %d bytes\n",stdin_len);

      if (DEBUG>=DEBUG_ALL)
      print_debug(debug_msg);
    /************* DEBUG ***********/



    ctcp_segment_t *tx_segment = malloc(sizeof(ctcp_segment_t) + stdin_len);

    
    if(state->tx_seg_last_added==NULL)
    {
      //set seq to 1 if no packets are sent
      tx_segment->seqno=ntohl(1);        /* Sequence number (in bytes) */
    }
    else
    {
      tx_segment->seqno = htonl(state->tx_seg_last_added->ctcp_segment.seqno) + htons(state->tx_seg_last_added->ctcp_segment.len) - sizeof(ctcp_segment_t) ; //think why no +1 here 
      tx_segment->seqno = htonl(tx_segment->seqno);
    }



    /* THIS(segment.ackno) SHOULD NOT BE UPDATED HERE - THIS SHOULD BE UPDATED WHEN WE ARE ACTUALLY SENDING - think why 
    if(state->rx_seg_last_acked==NULL)
    {
      //set expecting ack to 1 if no packets have been received
      tx_segment->ackno=1;        
    }
    else
    {
       tx_segment->ackno=1;
    }*/


      
      tx_segment->len= htons(sizeof(ctcp_segment_t) + stdin_len);          /* Total segment length in bytes (including headers) */
      
      tx_segment->flags = 0;
      tx_segment->flags |= ACK; //htonl(0);    //no flags set...    /* TCP flags */ // this should not be updated here - SHOULD BE UPDATED WHEN ACTUALLY SENDING 
      tx_segment->flags = htonl(tx_segment->flags);

      tx_segment->window = htons(state->cfg->recv_window * 1);       /* Window size, in bytes */ // RECEIVER advt. window from me - advertsing my rcv_window buffer size
      tx_segment->cksum=0;        /*  Set to zero now and CALC CHECKSUM WHEN ACTUALLY SENDING AS ackno and flasgs will be updated only during actual sending  */
      
      
      //copy data
      memcpy(tx_segment->data,stdin_buf,stdin_len);


      wrapped_ctcp_segment_t *wrapped_tx_segment = calloc(sizeof(wrapped_ctcp_segment_t)+ntohs(tx_segment->len),1);
  
      //BBR- SET BBR SEND TIME
      uint32_t maxbw = maxfilter_BW(state->BBR_BW);
      if(maxbw<5)
      {
        maxbw=5;
      }

     // fprintf(stderr,"----------------------> %f" , ntohs(tx_segment->len) / (state->PACING_GAIN * maxbw));
      
      //else
      //{
        wrapped_tx_segment->bbr_pacing_sendtime = (long)( state->BBR_lastpacket_pacing_sendtime + ntohs(tx_segment->len) / (state->PACING_GAIN * maxbw) ); //in milliseconds
      //}
      // update last pacing time
      //state->BBR_lastpacket_pacing_sendtime = wrapped_tx_segment->bbr_pacing_sendtime;
      
      //BBR- STORE THE BYTES ACKED TILL NOW - this is used to calculate BW for BBR - moved this to ship_tx_node
      //wrapped_tx_segment->bytes_acked_till_this_packet = state->receiver_has_acked_seqno;

      fprintf(stderr,"-----------------> %ld\n",state->receiver_has_acked_seqno);
      
      // copy raw segment
      memcpy(&(wrapped_tx_segment->ctcp_segment),tx_segment,ntohs(tx_segment->len));

      ll_add(state->tx_segments_unacked,wrapped_tx_segment);
      state->tx_seg_last_added = wrapped_tx_segment;

      //free the segment
      //free(tx_segment);

      /*print_debug("---\n");
      print_debug(tx_segment->data);
      print_debug("---\n");*/

  }

  if(stdin_len == -1)
  {
    //EOF was read from stdin if not already received once
    if(state->tx_eof==0)state->tx_eof = 1; // set tx_eof
  }

  //BBR_FIX - this somehow fixed the sending part
  tx_flush_conn(state);

}


void ackwipe__tx_segments_unacked(ctcp_state_t *state, uint32_t ackno)
{

  fprintf(stderr,"--->ack_no: %d\n",ackno);

  if (state->receiver_has_acked_seqno < ackno) state->receiver_has_acked_seqno = ackno;

  //safety
  if(state->tx_segments_unacked->length==0) return;

  // remove tx_segments which are acked
  ll_node_t *node = ll_front(state->tx_segments_unacked);

  do
  {
    wrapped_ctcp_segment_t *wrapped_tx_seg = node->object;
    
    ll_node_t *node2 = node;
    node = node->next;
    
    uint32_t last_seqno_in_seg = ntohl(wrapped_tx_seg->ctcp_segment.seqno) + ntohs(wrapped_tx_seg->ctcp_segment.len) - sizeof(ctcp_segment_t) - 1;
    
    // on match, insert the Bandwidth into the array
    if(last_seqno_in_seg == ackno-1)
    {


      //BBR- insert RTT into filter
      float rtt = current_time() - wrapped_tx_seg->bbr_actual_sendtime;
      state->BBR_RTT[state->bbr_rttfilter_current_index] = rtt;
      fprintf(stderr,"RTT -> %f\n",rtt);

      //BBR - insert BW into filter
      unsigned long bw = ackno - wrapped_tx_seg->bytes_acked_till_this_packet;
      fprintf(stderr,"BW -> %ld\n",bw);
      fprintf(stderr,"WAS_ACKED_UNTILL -> %ld\n",wrapped_tx_seg->bytes_acked_till_this_packet);
      state->BBR_BW[state->bbr_bwfilter_current_index] = bw;
      
      //BBR--call state machine - check if transition needed
      RUN_BBR_STATEMACHINE(state);
      
      //state->BBR_RTT_LAST_MIN = state->BBR_RTT[state->bbr_rttfilter_current_index];
      state->bbr_rttfilter_current_index++;
      if(state->bbr_rttfilter_current_index >= BBR_MINRTT_FILTER_SIZE) state->bbr_rttfilter_current_index=0;

      state->bbr_bwfilter_current_index++;
      if(state->bbr_bwfilter_current_index >= BBR_MAXBW_FILTER_SIZE) state->bbr_bwfilter_current_index=0;
      
      //print_debug("MATCH\n");
    }
    


    //note ackno is the expected next seqno to be sent by us
    if(last_seqno_in_seg < ackno)
    {
      //free the object as well after removing
      free(ll_remove(state->tx_segments_unacked,node2));
    }
  }
  while(node);
}




void ctcp_receive(ctcp_state_t *state, ctcp_segment_t *segment, size_t len) {
  /* FIXME */

  uint16_t computed_cksum, actual_cksum, num_data_bytes;
  //uint32_t last_seqno_of_segment, largest_allowable_seqno, smallest_allowable_seqno;
#define ENABLE_DBG_PRINTS 1
   /* If the segment was truncated, ignore it and hopefully retransmission will fix it. */


  // Check the checksum.
  actual_cksum = segment->cksum;
  segment->cksum = 0;
  computed_cksum = cksum(segment, ntohs(segment->len));

  // put it back in case we want to examine the value later
  segment->cksum = actual_cksum;
  computed_cksum = computed_cksum; // fool the compiler to pass

  /*
  We do not care about checksum matching at this point in our progress
  */

  if (actual_cksum != computed_cksum)
  {
    
    #ifdef ENABLE_DBG_PRINTS
    //if (DEBUG>=DEBUG_ALL)
    /*fprintf(stderr, "Invalid cksum! Computed=0x%04x, Actual=0x%04x    ",
            computed_cksum, actual_cksum);*/
    //print_ctcp_segment(segment);
    #endif
   
    //state->rx_state.num_invalid_cksums++;
   /* #if TEST
    free(segment);
    return;
    #endif*/
  }

  if (len < ntohs(segment->len)) {
  
  #ifdef ENABLE_DBG_PRINTS
  if (DEBUG>=DEBUG_ALL)
  fprintf(stderr, "Ignoring truncated segment.   ");
  //print_ctcp_segment(segment);
  #endif
  //free(segment);
  //state->rx_state.num_truncated_segments++;
  //return;

  segment->len = htons(len);
}



  

  //num_data_bytes are not used here, but just computed
  num_data_bytes = ntohs(segment->len) - sizeof(ctcp_segment_t);
  num_data_bytes = num_data_bytes; //fool the compiler 


  //BBR_FIX - Send ACK immediately
  //uint32_t rx_seqno_start = ntohl(segment->seqno);
	//uint32_t rx_seqno_end = rx_seqno_start + num_data_bytes -1; //-1 as start seq number is included
  //ship_ack(state,rx_seqno_end+1); //ack number = expected next seqno


  //BBR-update send window
   state->cfg->send_window =  ntohs(segment->window);

  //check if it is an ACK
  if( ntohl(segment->flags) & ACK )  
  {
    uint32_t ackno_recvd = ntohl(segment->ackno);
    
    char debug_msg[100];

    if( num_data_bytes==0 )
      sprintf(debug_msg,"rx -> RECEIVED ACK_ONLY - ACKNO: %d\n",ackno_recvd);
    else
      sprintf(debug_msg,"rx -> RECEIVED ACK+DATA - ACKNO: %d\n",ackno_recvd);
    
    if (DEBUG>=DEBUG_ALL)    
    print_debug(debug_msg);


    //BBR_FIX? -- immediately send ack?
    //ship_ack(state,ackno_recvd);

    //if( num_data_bytes==0)
    ackwipe__tx_segments_unacked(state,ackno_recvd);
    
  }

  //check if got FIN
  if( ntohl(segment->flags) & FIN)  
  {
    //set rx_fin to the FIN seqno
    state->rx_fin = ntohl(segment->seqno);
    //print_debug("--->FIN\n");

  }  

  // do not send if no data
  if( num_data_bytes==0 ) 
  {
    free(segment);
    return;
  } 

  
  wrapped_ctcp_segment_t *wrapped_rx_segment = calloc( sizeof(wrapped_ctcp_segment_t) + ntohs(segment->len), 1);
  memcpy(&(wrapped_rx_segment->ctcp_segment),segment,ntohs(segment->len));

  //insert into the rx_segments_buffered
  ll_add(state->rx_segments_buffered,wrapped_rx_segment);

  
  ctcp_output(state);

  //free the segment received
  free(segment);



  /*
  char debug_msg[100];
  sprintf(debug_msg,"ctcp_receive - %d bytes\n", num_data_bytes);
  print_debug(debug_msg);
  print_debug("ctcp_receive ->");
  memcpy(debug_msg,segment->data,num_data_bytes);
  print_debug(debug_msg);
  */

}


uint32_t get_last_output_seqno(ctcp_state_t *state)
{
  return ntohl(state->rx_seg_last_output->ctcp_segment.seqno) + ntohs(state->rx_seg_last_output->ctcp_segment.len) - sizeof(ctcp_segment_t) -  1; //-1 as the starting seqno is included 
}

void ship_ack(ctcp_state_t *state, uint32_t ackno)
{

  //if (ackno==0) ackno=1;

  //update this even before checking -->  if(state->tx_segments_unacked->length !=0 && state->tx_eof != 100) return ;
  //because if state->tx_eof == 100, we are blocked and we cannot update the state->next_ackno
  
  state->next_ackno=ackno;


  // if data is there to be sent, do not send ack-only segment, 
  // in the next timer call, ack will be sent along with data
  // --> However, do not block the FIN sending when we read CTRL+D i.e when state->tx_eof != 100  
  //if(state->tx_segments_unacked->length !=0 && state->tx_eof != 100) return ;

  //print_debug("HareKrishna\n");

  ctcp_segment_t *ack_segment = calloc(sizeof(ctcp_segment_t),1); // we need no data to send, so data field is empty

  ack_segment->seqno = htonl(state->seqno_last_shipped+1);
  ack_segment->len = htons(sizeof(ctcp_segment_t));
  ack_segment->ackno = htonl(ackno);

  ack_segment->flags = 0;
  ack_segment->flags |= ACK;  
  ack_segment->flags = htonl(ack_segment->flags);


  // if explicitly told to send fin by updating state->tx_eof to 100
  if(state->tx_eof == 100)
  {
    //ack_segment->flags = 0; //clear ACK
    //BBR MOD
    //ack_segment->flags = ntohl(ack_segment->flags) | FIN;
    ack_segment->flags =  htonl(ack_segment->flags);
  }

  uint16_t rcv_window_size=state->cfg->recv_window * 1;
  
  /*uint32_t last_output_seqno = get_last_output_seqno(state);
  uint16_t byte_pending_to_be_output = ackno - last_output_seqno;
  if( state->cfg->recv_window < byte_pending_to_be_output)
  rcv_window_size = state->cfg->recv_window;
  else
  rcv_window_size = byte_pending_to_be_output;*/

  ack_segment->window = htons(rcv_window_size);

  ack_segment->cksum = 0;
  ack_segment->cksum = cksum(ack_segment,sizeof(ctcp_segment_t));

  conn_send(state->conn, ack_segment , ntohs(ack_segment->len));

  if (DEBUG>=DEBUG_ALL)
  fprintf(stderr,"tx -> ACK ackno: %d\n",ackno);

  //update - this is probably being done above, but still, a duplicate doesn't harm
  state->next_ackno=ackno;

  free(ack_segment);

}




void ctcp_output(ctcp_state_t *state) {
  /* FIXME */
  
  if (state->rx_segments_buffered->length == 0) return;

  ll_node_t *node = ll_front(state->rx_segments_buffered);
  do 
  {
	//get the node object
	wrapped_ctcp_segment_t *rx_wrapped_segment = (wrapped_ctcp_segment_t *)node->object;
	

	//keep a copy and update node to next
  ll_node_t *node_duplicate = node;
	node = node->next;



	int num_data_bytes = ntohs(rx_wrapped_segment->ctcp_segment.len) - sizeof(ctcp_segment_t);
	
        
	//output only if present segment contains the right data to be output next
	// --------1------------2-----------3---- 
	// 1 - this received segment start seqno
	// 2 - seqno already output till this point + 1
	// 3 - this received segment's last seqno = start seqno + length of data
 	
	uint32_t rx_seqno_start = ntohl(rx_wrapped_segment->ctcp_segment.seqno);
	uint32_t rx_seqno_end = rx_seqno_start + num_data_bytes -1; //-1 as start seq number is included
	uint32_t last_output_seqno = 0; //init to 0

  
	
	// only if rx_seg_last_output is not null - i.e do not do it for the first time
	if(state->rx_seg_last_output!=NULL)
  last_output_seqno = get_last_output_seqno(state);


  int bytes_to_output = (rx_seqno_end - last_output_seqno);
  int output_start_offset_from_start_of_segment = num_data_bytes - bytes_to_output; 
  //example -
  //  num_data_bytes = 10
  //  ctcp_segment.data indices - 0 to 9
  //  bytes_to_output=3
  //  output_start_offset_from_start_of_segment = 10 - 3 = 7
  //  So indices 7,8,9 will be output
  //if (DEBUG>=DEBUG_ALL)  
  fprintf(stderr, "rx -> num_data_bytes: %d | bytes2output: %d | last_seqno: %d | segment__start-end-> %d - %d\n",num_data_bytes,bytes_to_output,last_output_seqno,rx_seqno_start,rx_seqno_end);


  
  //duplicate packet - send ACK
  if(last_output_seqno==rx_seqno_end) 
  {
    fprintf(stderr,"---> DUP: %d\n",rx_seqno_start);
    ship_ack(state,rx_seqno_end+1);
    return;
  }
	
  if( (rx_seqno_start <= (last_output_seqno+1)) &&   ((last_output_seqno+1) <= rx_seqno_end) )
	{
    

		// if no space available, return
		if(bytes_to_output > conn_bufspace(state->conn)) 
    {
      fprintf(stderr,"---> No space in output buffer\n");
      return;

    }    
    //else send an ACK
    else
    {
      //if(state->rx_fin) fprintf(stderr,"%d - %d",rx_seqno_start,rx_seqno_end);
      ship_ack(state,rx_seqno_end+1); //ack number = expected next seqno
    }

    


		// send
    fprintf(stderr,"---> LAST OP: %d\n",last_output_seqno);
   	conn_output( state->conn, rx_wrapped_segment->ctcp_segment.data+output_start_offset_from_start_of_segment, bytes_to_output);
    
    //fprintf(stderr,"output_start_offset_from_start_of_segment -> %d\n",output_start_offset_from_start_of_segment);
    /*output_start_offset_from_start_of_segment++;
    char *ppp = malloc(bytes_to_output+1);
    memcpy(ppp,rx_wrapped_segment->ctcp_segment.data+output_start_offset_from_start_of_segment,bytes_to_output);
    ppp[bytes_to_output]=0;
   	fprintf(stdout,"%s",ppp);*/
	
    //free the last rx_seg_last_output
    free(state->rx_seg_last_output);
		// update
		state->rx_seg_last_output = calloc(sizeof(wrapped_ctcp_segment_t) + ntohs(rx_wrapped_segment->ctcp_segment.len), 1);
		memcpy(&(state->rx_seg_last_output->ctcp_segment),&(rx_wrapped_segment->ctcp_segment), ntohs(rx_wrapped_segment->ctcp_segment.len));
		
		//delete the current node and free the object returned by ll_remove -> see ll_remove documentation
		free(ll_remove(state->rx_segments_buffered,node_duplicate));
	}

  }
  while(node);
   
}


int ship_tx_node(ll_node_t *node,ctcp_state_t *state)
{
	wrapped_ctcp_segment_t *tx_wrapped_segment = (wrapped_ctcp_segment_t *)node->object;

  //DO BBR PACING
  /*if(current_time() >  tx_wrapped_segment->bbr_pacing_sendtime) 
  {
    
  }
  else 
  {
    fprintf(stderr,"--> bbr_pacing_sendtime wait -> scheduled: %ld, current: %ld \n",tx_wrapped_segment->bbr_pacing_sendtime,current_time());
    fprintf(stderr,"MAX_BW: %d\n", maxfilter_BW(state->BBR_BW)); 
    return 0; 
  }*/

   

	
  if(tx_wrapped_segment->num_xmits>=5) 
  {
    fprintf(stderr,"--> num_xmits > 5\n");
    //return 0; // end the connection 
    //return -1; // end the connection 
  }

        
	if(current_time()-tx_wrapped_segment->timestamp_of_last_send < state->cfg->rt_timeout)
  {
    fprintf(stderr,"rt_timeout\n");
	  return 0;	
	  // not timeout out yet, hence return without sending

  }

   fprintf(stderr,"--> %d\n",state->tx_segments_unacked->length);

        


        //calc last seqno of data in this segment
        uint32_t last_databyte_seq_no = ntohl(tx_wrapped_segment->ctcp_segment.seqno) + ntohs(tx_wrapped_segment->ctcp_segment.len) - sizeof(ctcp_segment_t) - 1;


        //Check receiver flow control 
        //uint32_t rcvd_acked_untill_seqno = state->next_ackno; //already  acked till 1 less than expected next ackno
        
        //if(state->seqno_last_shipped<=0);
        //if(state->cfg->send_window > state->seqno_last_shipped - rcvd_acked_untill_seqno);//state->seqno_last_shipped - rcvd_acked_untill_seqno - 1000 );
        // ex - rec_window=5, next_ack_witing = 9, then we can ship upto seqno 13 -> 13 - (9 - 1) = 13 - 8 = 5
        //else return 0;
        //fprintf(stderr,"send win: %d\n",state->cfg->send_window);

        if( state->cfg->send_window +50 >  state->seqno_last_shipped -  state->receiver_has_acked_seqno);
        //if( 8*8*8*2 >  state->seqno_last_shipped -  state->receiver_has_acked_seqno);
        //if(state->cfg->send_window  >= ntohs(tx_wrapped_segment->ctcp_segment.len) - sizeof(ctcp_segment_t) );
        else 
        {
          fprintf(stderr,"--> send_window < data to be shipped\n");
          //fprintf(stderr,"data to ship: %d, send_window: %d\n",ntohs(tx_wrapped_segment->ctcp_segment.len), state->cfg->send_window );
          fprintf(stderr,"data to ship: %ld, send_window: %d\n",state->seqno_last_shipped -  state->receiver_has_acked_seqno, state->cfg->send_window + 50);
          return 0;
        }

        
        
        // Calculate the cksum
        //tx_wrapped_segment->ctcp_segment.ackno = htonl(state->next_ackno);
        tx_wrapped_segment->ctcp_segment.cksum = cksum(&(tx_wrapped_segment->ctcp_segment),ntohs(tx_wrapped_segment->ctcp_segment.len));
        
        if (DEBUG>=DEBUG_ALL)
        fprintf(stderr,"tx -> seqno: %d\n",ntohl(tx_wrapped_segment->ctcp_segment.seqno));

        //BBR_FIX_FILETRANSFER UPDATE ACK_NO ????
        tx_wrapped_segment->ctcp_segment.ackno = htonl(state->next_ackno);


        
        conn_send(state->conn, &(tx_wrapped_segment->ctcp_segment) , ntohs(tx_wrapped_segment->ctcp_segment.len));

      // BBR update last pacing time
        //state->BBR_lastpacket_pacing_sendtime = current_time();//wrapped_tx_segment->bbr_pacing_sendtime;
        
        
        tx_wrapped_segment->timestamp_of_last_send = current_time();

        if(tx_wrapped_segment->num_xmits==0) // only if this is the first xmit
        {
         tx_wrapped_segment->bbr_actual_sendtime = tx_wrapped_segment->timestamp_of_last_send;
          //BBR- STORE THE BYTES ACKED TILL NOW - this is used to calculate BW for BBR
          tx_wrapped_segment->bytes_acked_till_this_packet = state->receiver_has_acked_seqno;
        } 
        tx_wrapped_segment->num_xmits++;


        //update the latest sent seqno - this is used by send_ack()
        if(state->seqno_last_shipped < last_databyte_seq_no) state->seqno_last_shipped = last_databyte_seq_no;
        
	/*
	      char debug_msg[100];
        sprintf(debug_msg,"%s %d\n", tx_wrapped_segment->ctcp_segment.data ,state_walker->cfg->rt_timeout);
        print_debug(debug_msg);*/


        fprintf(stderr,"last sent seqno: %d\n",last_databyte_seq_no);

 
        // BBR -> log the timestamp and BDP (in bits) to bbr.txt - do this only on the client side
        //if(is_client)
        {
          FILE *f = fopen(bbr_txt_name,"a+");
          fprintf(f,"%ld,%d\n",tx_wrapped_segment->timestamp_of_last_send,maxfilter_BW(state->BBR_BW)*(uint32_t)minfilter_RTT(state->BBR_RTT)*8);//minfilter_RTT(state->BBR_RTT));
          //fprintf(f,"%ld,%d,%f\n",tx_wrapped_segment->timestamp_of_last_send,maxfilter_BW(state->BBR_BW),minfilter_RTT(state->BBR_RTT));//minfilter_RTT(state->BBR_RTT));
          //fprintf(f,"%ld,%f\n",tx_wrapped_segment->timestamp_of_last_send,minfilter_RTT(state->BBR_RTT));//minfilter_RTT(state->BBR_RTT));
          fclose(f);
        }


	return 1;
}




int tx_flush_conn(ctcp_state_t *state)
{
    

    if(state->tx_segments_unacked->length>0)
    {
      ll_node_t *tx_wrapped_segment_node = ll_front(state->tx_segments_unacked);

      //fprintf(stderr,"--> %d\n",state_list->rx_segments_buffered->length);
        
      
      do
      {
          //ship
          int retVal=ship_tx_node (tx_wrapped_segment_node, state);
          
          if(retVal<0)
          {
            //teardown
            //print_debug("max retries\n");
            state->tx_eof = 2;

              //remove this segment
             free(ll_remove(state->tx_segments_unacked,tx_wrapped_segment_node));

            //send fin by updating tx_eof to 100
            //state->tx_eof = 100;
            //ship_ack(state,state->next_ackno);


          }
          //get next
          tx_wrapped_segment_node = tx_wrapped_segment_node->next;
          
      }
      while(tx_wrapped_segment_node);
      
    }


    if(state->tx_eof==1 || state->tx_eof==2)
    {
            //teardown
            //uint16_t temp = state->tx_eof;

            //send fin by updating tx_eof to 100
            state->tx_eof = 100;
            ship_ack(state,state->next_ackno);
            //print_debug("again\n");
            // if we have sent the FIN previously, we have to increment state->seqno_last_shipped 
            state->seqno_last_shipped++;

           

            
            


    }

    //AFTER SENDING THE WAITING TX SEGMENTS, CHECK IF WE HAVE RECEIVED FIN
    if(state->rx_fin && state->rx_fin!=0xffffffff)
    {
        

        if(state->next_ackno == state->rx_fin) 
        {
          if (DEBUG>=DEBUG_ALL)
          fprintf(stderr,"---FIN---\n");
          //fprintf(stderr,"\n###teardown###\n");


          //we have already received the state->next_ackno = state->rx_fin, so send expected ack by adding one
          // this is also because FIN is counted as one data byte
          state->next_ackno++;

          //send ack
         // ship_ack(state,state->next_ackno); 

          //teardown in next timer run if not already done
          //if(state->tx_eof!=100)state->tx_eof = 1;
          //else
          //wait untill we receive pending data
          //fprintf(stderr,"%d-%d\n",state->rx_fin,state->next_ackno);
          //if(state->rx_fin != state->next_ackno) return 0;

          {
            // if we have sent the FIN previously, we have to increment state->seqno_last_shipped 
            //state->seqno_last_shipped++;

            // check if FIN already send, if so prevent another FIN from being sent with ACK
            if(state->tx_eof == 100) state->tx_eof=101;
            //send the ACK for FIN received
            ship_ack(state,state->next_ackno);
            
            if (DEBUG>=DEBUG_ALL)
            print_debug("BOO\n");
            //while(1);
          }
          if (DEBUG>=DEBUG_ALL)
          fprintf(stderr,">>> %d\n",state->tx_eof);


          //turn off duplicate processing of FIN once processed
          state->rx_fin=0xffffffff;

          //flush
          ctcp_output(state);
          conn_output(state->conn,"",0);

          


        }

    }

    if(state->tx_eof == 101)
    {
      
      ctcp_destroy(state);
    }


    if(state->rx_fin==0xffffffff && state->tx_eof == 100)
    {
      
      //conn_output(state->conn,"",0);
      //ctcp_destroy(state);

      //conn_remove(state->conn);

      
      //long now = current_time();
      //long MAX_SEG_LIFETIME_MS=35;//4000;
      //print_debug("tick");
      //while(current_time() - now < 2*MAX_SEG_LIFETIME_MS); //wait 20 ms
      //sudoprint_debug("tock");
      //ctcp_destroy(state);
      state->tx_eof++;
    }

    

    return 1;
}

void ctcp_timer() {
  /* FIXME */

  ctcp_state_t *state_walker = state_list;
  if(state_walker==NULL) return; // safety

  /* SEND ALL DATA THAT HAS NOT BEEN ACKED and also TIMEDOUT from all connections */
  do
  {
   ctcp_output(state_walker); //flush rx
   tx_flush_conn(state_walker);
   state_walker=state_walker->next;

  } while(state_walker!=NULL);


  /*char debug_msg[100];
  sprintf(debug_msg,"%d\n",state_list->rx_segments_buffered->length);
  print_debug(debug_msg);*/

}
