import csv
import subprocess


def print_caller():
	import inspect
	print "\n================="
	print inspect.stack()[1][3].upper()
	print "================="


class Node:
	ROUTER=1
	ENDHOST=0
	# _dr = default route
	def __init__(self,_host,_iface,_ip,_dr,_ospf_cost=0,_node_type=ENDHOST,_debug=0):
		self.host = _host
		self.iface = _iface
		self.ip = _ip
		self.dr = _dr

		self.ospf_cost = _ospf_cost

		self.node_type=_node_type

		if _node_type==self.ENDHOST:
			self.queryDR="route | grep default | awk '{print $2}'"
			self.queryIP="ifconfig {0} | grep -Po 'inet addr:([\d+\.]+)' | grep -Po '[\d+\.]+' ".format(self.iface)
			
			self.setIP_template="sudo ifconfig {0} {1}/24"
			self.setDR_template = "sudo route add default gw {0}"


		if _node_type==self.ROUTER:
			self.queryDR="route | grep default | awk '{print $2}'"
			self.queryIP="ifconfig {0} | grep -Po 'inet addr:([\d+\.]+)' | grep -Po '[\d+\.]+' ".format(self.iface)
			
			self.setIP_template='vtysh -c "conf t" -c "interface {0}" -c "ip address {1}/24" -c "exit" -c "exit"'

			self.writeFILE='vtysh -c "write file"'
			

		self.debug=_debug

		if self.debug:
			print "------------"*4
			print "Node Instace : {} | Node Type : {}".format(self.host,"ROUTER" if self.node_type else "ENDHOST")
			print "------------"*4

	def talk(self,_cmd):
		pid = subprocess.check_output("""ps ax | grep "mininet:{}$" | grep bash | grep -v mxexec | awk '{{print $1}};'""".format(self.host),shell=True)
		pid = pid.strip()
		enter_node_cmd = "mxexec -a {0} -b {0} -k {0} bash".format(pid)
		#enter_node_cmd = "bash go_to.sh {0}".format(self.host)
		
		if pid=="":
			if self.debug>0:print "\n\t--->\tPID was not found for host: {}".format(self.host)
			return ("error","error") # return error
			
		p=subprocess.Popen(enter_node_cmd,stdin = subprocess.PIPE, stdout = subprocess.PIPE,shell=True)
		op=p.communicate(_cmd)
		return op

	def verify(self,_query,_expect):
		found=self.talk(_query)[0].strip()
		if self.debug>1:
			print "-> set: {0} | found: {1} --> validity : {2}".format(_expect,found,_expect==found)
		return _expect==found


	def setIP(self):
		if self.debug>1:
			print "-> setting IP"
		cmdIP = self.setIP_template.format(self.iface,self.ip)
		self.talk(cmdIP)

	def setDR(self):
		if self.debug>1:
			print "-> setting DR"
		cmdDR = self.setDR_template.format(self.dr)
		self.talk(cmdDR)
		

	def verifyIP(self):
		if self.debug>1:
			print "-> verifying {0} : IP".format(self.host)
		return self.verify(self.queryIP,self.ip)
		

	def verifyDR(self):
		if self.debug>1:
			print "-> verifying {0} : DR".format(self.host)
		return self.verify(self.queryDR,self.dr)

	def query_info(self):
		#print "-> dumping info: {0}".format(self.host)
		ip_found=self.talk(self.queryIP)[0].strip()
		dr_found=self.talk(self.queryDR)[0].strip()
		if self.debug>1:
			print "found IP:",ip_found
			print "found DR:",dr_found
		return ip_found,dr_found
		#print "IP: {0} | DR: {1}".format(ip_found,dr_found)

	def setup_endhost(self):

		# set IP
		self.setIP()
		vIP=self.verifyIP()


		# set DR only if not set already correclty, only then write the correct route
		# else ignore
		if self.debug>1:
				print "---> Checking DR on Startup - setting DR freshly"		
		vDR=self.verifyDR()

		if vDR==False:
			if self.debug>1:
				print "---> Invalid DR on Startup - setting DR freshly"
			self.setDR()

			# query again for further checks done below
			if self.debug>1:
				print "---> Querryin DR again after setting up DR freshly"
			vDR=self.verifyDR()
		else:
			if self.debug>1:
				print "---> Valid DR on Startup - skipping setting up DR freshly"


		if self.debug>0:
			print "=> valid IP?",vIP,"|","valid DR?",vDR

			if not vIP:
				print "===> IP verification error @ {} ".format(self.host)
				
			if not vDR:
				print "===> DR verification error @ {} ".format(self.host)

		return vIP and vDR


	def router_write_file(self):
		if self.node_type==self.ROUTER:
			self.talk(self.writeFILE)

	def router_enable_ospf(self):
		enable_ospf_cmd = 'vtysh -c "conf t" -c "router ospf" -c "network {0}/24 area 0"'.format(self.ip)

		# skip
		if self.host in ["west","east"]: 
			print "Skipping...",
			return
		
		result=self.talk(enable_ospf_cmd)

		if self.debug>0:
			print enable_ospf_cmd
			print result
		

		if not self.ospf_cost:
			self.ospf_cost = 0

		set_ospfcost_cmd = 'vtysh -c "conf t" -c "interface {0}" -c "ip ospf cost {1}"'.format(self.iface,self.ospf_cost)
		
		result=self.talk(set_ospfcost_cmd)
		
		if self.debug>0:
			print set_ospfcost_cmd
			print result

		

	def setup_router(self,linebreak="\n"):
		if node.verifyIP():
			if self.debug==0:
				sys.stdout.write("\t exists:ok" + linebreak)
			
		else:
			node.setIP()
			if self.debug==0:
				sys.stdout.write("\t setup:")
			if  node.verifyIP():
				if self.debug==0:
					sys.stdout.write("ok"+linebreak)
			else:
				if self.debug==0:
					sys.stdout.write("fail"+linebreak)

		self.router_enable_ospf()

		# setup routing from west to client via sr
		if self.host=="west":
			cmd = 'vtysh -c "conf t" -c "ip route 5.1.1.1/24 5.0.2.2"'
			self.talk(cmd)
		
		


def getEndHostInfo(link):
	_host				=	link["Node2"]
	_iface				=	link["Interface2"]
	_ip_addr			=	link["Address2"]
	_default_gw_addr	=	link["Address1"]

	return _host,_iface,_ip_addr,_default_gw_addr



def getLinkInfo(link):
	_host1				=	link["Node1"]
	_iface1				=	link["Interface1"]
	_ip_addr1			=	link["Address1"]
	_dest_addr1			=	link["Address2"]

	_host2				=	link["Node2"]
	_iface2				=	link["Interface2"]
	_ip_addr2			=	link["Address2"]
	_dest_addr2			=	link["Address1"]

	_ospf_cost 			= int(link["Cost"]) if link["Cost"] else 0 # check if empty string, then make it 0

	return (_host1,_iface1,_ip_addr1,_dest_addr1,_ospf_cost),(_host2,_iface2,_ip_addr2,_dest_addr2,_ospf_cost)




if __name__ == "__main__":
	import sys,time

	DEBUG=0

	with open("netinfo/links.csv") as f:
		links = csv.DictReader(f)
		links = list(links)
		hostlinks = filter(lambda x: x["Node2"].endswith("-host") or x["Node2"] in ["server1","server2","client"],links)
		#hostlinks = filter(lambda x: x["Node2"].endswith("-host"),links)

		routerlinks = filter(lambda x: ( ( not x["Node2"].endswith("-host") ) and ( not x["Node2"] in ["server1","server2","client"]) ),links)
		print "TOTAL LINKS:",len(links)
		print "HOST-ROUTER LINKS:",len(hostlinks)
		print "ROUTER-ROUTER LINKS:",len(routerlinks),"\n"



	# only is sub command is given, mainly used for debugging

	if len(sys.argv)>1:
		if "debug" in sys.argv[-1]:
			try:
				DEBUG=int(sys.argv[-1].split("=")[-1])
				print ">>> Setting DEBUG level to",DEBUG,"\n"
			except:
				print "Usage :\n python",sys.argv[0],"[subcommand] [debug=level] ; for level in range(4)"
		


		subcmd = sys.argv[1]

		if subcmd=="dump":
			print "Querrying ...\n"
			for h in hostlinks:
				_host,_iface,_ip_addr,_default_gw_addr= getEndHostInfo(h)
				print "HOST: {}\tIP: {}\tDR: {}".format(_host,*Node(_host,_iface,_ip_addr,_default_gw_addr).query_info())
			exit(0)

		if subcmd=="router":

			########### SETUP router for ROUTER-ROUTER LINKS ###########
			print "Setting routers on ROUTER-ROUTER LINKS --->"
			
			#print "Filtering ROUTER-ROUTER LINKS for non-zero/non-empty OSPF costs --->\n"
			# filter if Cost is empty string
			#routerlinks_with_nonzero_ospf_costs = filter(lambda x: x["Cost"],routerlinks)
			#routerlinks_with_nonzero_ospf_costs = routerlinks
			routerlinks_with_nonzero_ospf_costs = routerlinks

			# print all links
			for indx in range(len(routerlinks_with_nonzero_ospf_costs)):
				rlink =  routerlinks_with_nonzero_ospf_costs[indx]

				r1,r2=getLinkInfo(rlink)
				print r1,"\t--->\t",r2

			#wait
			#time.sleep(1)

			# move cursor up to begining of previous prints - only in DEBUG mode 0
			if DEBUG==0:sys.stdout.write("\033[A"* len(routerlinks_with_nonzero_ospf_costs) )

			# setup the router configurations
			for indx in range(len(routerlinks_with_nonzero_ospf_costs)):
				rlink =  routerlinks_with_nonzero_ospf_costs[indx]

				r1,r2=getLinkInfo(rlink)


				# HANDLE ROUTER r1
				node=Node(*r1,_node_type=Node.ROUTER,_debug=DEBUG) #set debug to 1 to print node_type
				
				
				
				if DEBUG==0:sys.stdout.write("%s\t--->\t%s\t||" % (r1,r2))
					
				node.setup_router(linebreak="")


				
				# HANDLE ROUTER r2				
				node=Node(*r2,_node_type=Node.ROUTER,_debug=DEBUG) #set debug to 1 to print node_type
				#node.debug=0 # reset debug
				if DEBUG==0:sys.stdout.write("\t--->")
				
				node.setup_router()
				
						
				sys.stdout.flush() # once every loop


			########### SETUP router for ROUTER-ENDHOST LINKS ###########
			print "-----------"*14
			print "\nSetting routers on ROUTER-ENDHOST LINKS --->\n"

			# print all routers on ROUTER-ENDHOST LINKS
			for rhlink in hostlinks:
				router,endhost = getLinkInfo(rhlink)
				print router

			#wait
			time.sleep(1)

			# move cursor up to begining of previous prints - only in DEBUG mode 0
			if DEBUG==0:sys.stdout.write("\033[A"* len(hostlinks) )

			# setup all routers on ROUTER-ENDHOST LINKS
			for rhlink in hostlinks:
				router,endhost = getLinkInfo(rhlink)
				print router,

				node = Node(*router,_node_type=Node.ROUTER,_debug=DEBUG)

				node.setup_router()

				sys.stdout.flush()
				

				

			print "-----------"*14,"\n"		
			#exit(0)

			## SAVE ALL ROUTER SETTINGS TO FILE

			for indx in range(len(routerlinks_with_nonzero_ospf_costs)):
				rlink =  routerlinks_with_nonzero_ospf_costs[indx]

				r1,r2=getLinkInfo(rlink)

				# HANDLE ROUTER r1
				node1=Node(*r1,_node_type=Node.ROUTER,_debug=DEBUG) #set debug to 1 to print node_type
				node2=Node(*r2,_node_type=Node.ROUTER,_debug=DEBUG) #set debug to 1 to print node_type

				print "-> writing file for {}".format(node1.host)
				node1.router_write_file()

				print "-> writing file for {}".format(node2.host)
				node2.router_write_file()
		


			
		else:
			print "Usage :"
			print "\tpython",sys.argv[0],"[debug=level] ; for level in range(4)"
			print "\tpython",sys.argv[0],"routers","[debug=level] ; for level in range(4)"
			print "\tpython",sys.argv[0],"dump"




	print "Found Host Links ->"
	all_nodes_string=""
	for h in hostlinks: 
		all_nodes_string = all_nodes_string + getEndHostInfo(h)[0]+"|"
	print all_nodes_string
	#time.sleep(0.2)
	
	for h in hostlinks:
			
		host,iface,ip_addr,default_gw_addr = getEndHostInfo(h)

		node = Node(host,iface,ip_addr,default_gw_addr,_node_type=Node.ENDHOST,_debug=DEBUG)
		if node.setup_endhost():
			if node.debug==0:
				sys.stdout.write("."*(len(node.host)-len("ok...."))+"ok...."+"|")
				sys.stdout.flush()
		else: 
			if node.debug==0:
				sys.stdout.write("."*(len(node.host)-len("fail"))+"fail"+"|")
				sys.stdout.flush()
	print ""
	


			
			

			
			
