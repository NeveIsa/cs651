#define BBR_STARTUP             0
#define BBR_DRAIN               1
#define BBR_PROBE_BW            2
#define BBR_PROBE_RTT           3



uint8_t BBR_probebw_stage = 1;


//#define BBR_MAXBW_FILTER_SIZE   10
//#define BBR_MINRTT_FILTER_SIZE  10

uint32_t maxfilter_BW(uint32_t *p)
{
    uint32_t max = 0;
    uint8_t i=0;

    for(i=0; i<BBR_MAXBW_FILTER_SIZE; i++)
    {
        max = (p[i] > max) ? p[i] : max ;
    }

    return max;

}

float minfilter_RTT(float *p)
{
    float min = 99999; // a big value - 99 secs
    uint8_t i=0;
    for(i=0; i<BBR_MINRTT_FILTER_SIZE; i++)
    {
       min = (p[i] < min) ? p[i] : min ;
    }
    
    if(min==0) return 0.01;
    return min;
}

void RUN_BBR_STATEMACHINE(ctcp_state_t *state)
{
    if(state->BBR_STATE == BBR_STARTUP) print_debug("BBR_STARTUP\n");
    else if(state->BBR_STATE == BBR_DRAIN) print_debug("BBR_DRAIN\n");
    else if(state->BBR_STATE == BBR_PROBE_BW) print_debug("BBR_PROBE_BW\n");
    else if(state->BBR_STATE == BBR_PROBE_RTT) print_debug("BBR_PROBE_RTT\n");

    //get the current state
    uint8_t __bbr_state = state->BBR_STATE;

    uint32_t BDP = minfilter_RTT(state->BBR_RTT) * maxfilter_BW(state->BBR_BW);
    uint32_t inflight =  state->seqno_last_shipped -  state->receiver_has_acked_seqno;
    inflight++;
    BDP++;

    if(__bbr_state == BBR_STARTUP)
    {
        if(maxfilter_BW(state->BBR_BW) > (uint32_t)(1.25 * state->BBR_BW_LAST_MAX)) //bw is increasing
        {
            state->PACING_GAIN *= 2; //double gain - paper says (2/ln2)?
        }
        else 
        {
            //decrease PACING_GAIN
            state->PACING_GAIN /= 3; //decrease by 3 times
            state->BBR_STATE = BBR_DRAIN;
        }

        state->BBR_BW_LAST_MAX = maxfilter_BW(state->BBR_BW);


    }


    if(__bbr_state == BBR_DRAIN)
    {
        // state->PACING_GAIN /= 1.1;
        //if( BDP > inflight ) state->BBR_STATE = BBR_PROBE_BW;
    }
    
    
    if(__bbr_state == BBR_PROBE_BW)
    {
       /* if(BBR_probebw_stage == 1) state->PACING_GAIN = 1.25;
        if(BBR_probebw_stage == 2) state->PACING_GAIN = 0.75;
        else
        {
            state->PACING_GAIN = 1;
        }

        if(BBR_probebw_stage==8)
        {
            BBR_probebw_stage=1;
            state->BBR_STATE = BBR_PROBE_RTT;
        }*/


    }
    
    
    if(__bbr_state == BBR_PROBE_RTT)
    {
        /*state->PACING_GAIN = 0.25;
        if( minfilter_RTT (state->BBR_RTT) < state->BBR_RTT_LAST_MIN )
        {
                state->BBR_STATE = BBR_PROBE_BW;
        }*/
    }
}