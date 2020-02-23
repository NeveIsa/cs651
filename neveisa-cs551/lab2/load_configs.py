import os,sys,subprocess,time


config_dir = "configs"
ospf_conf_filename = "ospfd.conf.sav"
zebra_conf_filename = "zebra.conf.sav"

if len(sys.argv)>1:
    subcmd=sys.argv[1]
    if subcmd=="infinity":
        os.system("python config_i2_hosts.py router")
    else:
        config_dir = subcmd



def load_config_from_file(__conf_file):
    
    with open(__conf_file) as f:
        conf_text = f.read()

    print d, " | {} conf: ->".format(__conf_file.split("/")[-1][:5]), "exists" if os.path.isfile(__conf_file) else "doesn't exist",
    print " -> read",
    
    __host = __conf_file.split("/")[-2]

    pid = subprocess.check_output("""ps ax | grep "mininet:{}$" | grep bash | grep -v mxexec | awk '{{print $1}};'""".format(__host),shell=True)
    pid = pid.strip()
    enter_node_cmd = "mxexec -a {0} -b {0} -k {0} bash".format(pid)

    if pid=="":
        print "\n\t--->\tPID was not found for host: {}".format(__host)
        return

    #print enter_node_cmd
    #enter_node_cmd = "bash go_to.sh {0}".format(__host)

    
    p=subprocess.Popen(enter_node_cmd,stdin = subprocess.PIPE, stdout = subprocess.PIPE,shell=True)
    output = p.communicate('vtysh -c "conf t" -c "{0}"'.format(conf_text))
    
    #if successful, output should be blank
    if(output[0]==""):
        print "-> loaded"
    else:
        print "->",output[0].replace("\n",",")[:81]


if __name__=="__main__":
    print "\nCONFIG. DIR -> ",config_dir
    print "------------"*2
    valid_router_dirs=filter(lambda y: os.path.isdir(y) and y.split("/")[-1]!="EXAMPLE" , map(lambda x: os.path.join(config_dir,x),os.listdir(config_dir)))
    
    # Print the valid router directories
    for d in valid_router_dirs:
        ospfd_conf_file_fullpath = os.path.join(d,ospf_conf_filename)
        zebra_conf_file_fullpath = os.path.join(d,zebra_conf_filename)

        load_config_from_file(ospfd_conf_file_fullpath)
        load_config_from_file(zebra_conf_file_fullpath)
        print ""

        
            

        

    
        

    

        
