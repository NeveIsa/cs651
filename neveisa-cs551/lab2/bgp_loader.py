import csv
import subprocess

class BGPNode:
	def __init__(self,host,loopback_ip,AS_number,debug=0):
		self.host = host
		self.loopback_ip = loopback_ip
		self.AS_number = AS_number
		self.debug = debug


	def talk(self,_cmd):
		pid = subprocess.check_output("""ps ax | grep "mininet:{}$" | grep bash | grep -v mxexec | awk '{{print $1}};'""".format(self.host),shell=True)
		pid = pid.strip()
		enter_node_cmd = "mxexec -a {0} -b {0} -k {0} bash".format(pid)
		#enter_node_cmd = "bash go_to.sh {0}".format(self.host)
		
		if pid=="":
			if self.debug>1:print "\n\t--->\tPID was not found for host: {}".format(self.host)
			return ("error","error") # return error
			
		p=subprocess.Popen(enter_node_cmd,stdin = subprocess.PIPE, stdout = subprocess.PIPE,shell=True)
		op=p.communicate(_cmd)
		return op

	def set_AS_number(self):
		cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "exit" -c "exit" -c "exit"'.format(self.AS_number)
		result=self.talk(cmd)
		if result[0]=="":
			if self.debug:print "---> Setup AS Number {0} to {1}:\tSuccess".format(self.host,self.AS_number)
		else:
			if self.debug:print "---> Setup AS Number {0} to {1}:\tFAIL".format(self.host,self.AS_number)


	def set_ibgp(self,neightbour_BGPnode):
		if self.AS_number != neightbour_BGPnode.AS_number:
			print "---> Setup i-BGP {0} -> {1} \t: FAIL | my ASN {2} != {3} neighbour's ASN".format(self.host,neightbour_BGPnode.host,self.AS_number,neightbour_BGPnode.AS_number)
			return False

		cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "neighbor {1} remote-as {2}"'.format(self.AS_number, neightbour_BGPnode.loopback_ip,neightbour_BGPnode.AS_number)
		result = self.talk(cmd)

		cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "neighbor {1} update-source host"'.format(self.AS_number, neightbour_BGPnode.loopback_ip)
		result = self.talk(cmd)
		#print result

		#print result
		if result[0]=="":
			if self.debug: print "---> Setup i-BGP {0} -> {1} \t: Success".format(self.host,neightbour_BGPnode.host) 
			return True
		else:
			if self.debug: print "---> Setup i-BGP {0} -> {1} \t: FAIL".format(self.host,neightbour_BGPnode.host)
			return False

	def set_next_hop_self(self,neightbour_BGPnode):
		cmd='vtysh -c "conf t" -c "router bgp {0}" -c "neighbor {1} next-hop-self"'.format(self.AS_number,neightbour_BGPnode.loopback_ip)
		result = self.talk(cmd)
		#print result
		if result[0]=="":
			if self.debug: print "---> Setup NEXT-HOP-SELF {0} -> {1} \t: Success".format(self.host,neightbour_BGPnode.host) 
			return True
		else:
			if self.debug: print "---> Setup NEXT-HOP-SELF {0} -> {1} \t: FAIL".format(self.host,neightbour_BGPnode.host)
			return False



	def publish_my_consumer_routes(self):
		
		# setup route aggregation in NEWY and SEAT
		if self.host in ["SEAT","NEWY"]:
			cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "aggregate-address {1}/8 summary-only"'.format(self.AS_number,self.loopback_ip)
			result=self.talk(cmd)

		if self.host in ["east","west","SEAT","NEWY"]:
			cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "network {1}/8"'.format(self.AS_number,self.loopback_ip)
		else:
			cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "network {1}/24"'.format(self.AS_number,self.loopback_ip)

		result=self.talk(cmd)
		if result[0]=="":
			if self.debug:print "---> Publish my[{0}] consumer routes {1}\t: Success".format(self.host,cmd.split("-c")[-1].replace("network ",""))
		else:
			if self.debug:print "---> Publish my[{0}] consumer routes {1}\t: FAIL".format(self.host,cmd.split("-c")[-1].replace("network ",""))
		

	def set_ebgp(self,neightbour_BGPnode):
		if self.AS_number == neightbour_BGPnode.AS_number:
			print "---> Setup e-BGP {0} -> {1} \t: FAIL | my ASN {2} == {3} neighbour's ASN".format(self.host,neightbour_BGPnode.host,self.AS_number,neightbour_BGPnode.AS_number)
			return False

		cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "neighbor {1} remote-as {2}"'.format(self.AS_number, neightbour_BGPnode.ebgp_loopback_ip, neightbour_BGPnode.AS_number)
		result = self.talk(cmd)

		#bgp_loopback_iface = neightbour_BGPnode.host.lower() if self.host in ["east","west"] else "host"
		neighbor_ebgp_loopback_iface = neightbour_BGPnode.host.lower()
		cmd = 'vtysh -c "conf t" -c "router bgp {0}" -c "neighbor {1} update-source {2}"'.format(self.AS_number, neightbour_BGPnode.ebgp_loopback_ip,neighbor_ebgp_loopback_iface)
		result = self.talk(cmd)

		#print result
		if result[0]=="":
			if self.debug: print "---> Setup e-BGP {0} -> {1} \t: Success".format(self.host,neightbour_BGPnode.host) 
			return True
		else:
			if self.debug: print "---> Setup e-BGP {0} -> {1} \t: FAIL".format(self.host,neightbour_BGPnode.host)


	def router_write_file(self):
		self.talk('vtysh -c "write file"')


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

	#return (_host1,_iface1,_ip_addr1,_dest_addr1,_ospf_cost),(_host2,_iface2,_ip_addr2,_dest_addr2,_ospf_cost)

	return {"host":_host1,"iface":_iface1,"ip":_ip_addr1},{"host":_host2,"iface":_iface2,"ip":_ip_addr2}


if __name__ == "__main__":
	import sys,time,os

	# return if not running as root
	if os.geteuid():
		print "Must run with sudo ! quitting."
		exit(0)

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

	with open("netinfo/routers.csv") as f:
		router_details = csv.DictReader(f)
		router_details = list(router_details)
		router_details_hash = { r["Name"]:r for r in router_details }
		#print router_details_hash

	with open("netinfo/asns.csv") as f:
		asns_details = csv.DictReader(f)
		asns_details = list(asns_details)
		asns_details_hash = { a["Net"]:a for a in asns_details }
		#print asns_details_hash

	ROUTER_INFO=[]
	for rname,r in router_details_hash.items():
		r["ASN"] = asns_details_hash[r["Net"]]["ASN"]
		ROUTER_INFO.append(r)


	#### SETUP LOGIC ######

	DEBUG=1

	for r in ROUTER_INFO:
		node = BGPNode(host=r["Name"],loopback_ip=r["Loopback"],AS_number=r["ASN"],debug=DEBUG)
		if DEBUG:
			print ""
			print "-----"*7
			print "Setting up AS numbers in routers"
			print "-----"*7
		
		# set my AS number
		node.set_AS_number()

		if DEBUG:
			print "-----"*7
			print "Setting up i-BGP in routers"
			if node.host in ["SEAT","NEWY"]: print "Also setting up NEXT_HOP_SELF for {}".format(node.host)
			print "-----"*7

		for nr in ROUTER_INFO:
			# if same router, continue
			if nr==r:
				continue
			# else if neighbour, setup i-BGP with them
			nr_node = BGPNode(host=nr["Name"],loopback_ip=nr["Loopback"],AS_number=nr["ASN"],debug=DEBUG)
			node.set_ibgp(nr_node)

			if node.host in ["SEAT","NEWY"]:
				node.set_next_hop_self(nr_node)

	
	# After all i-BGP sessions are brought up
	# publish my consumer routes
	if DEBUG:
		print ""
		print "-----"*7
		print "Publishing my consumer routes"
		print "-----"*7
	for r in ROUTER_INFO:
		node = BGPNode(host=r["Name"],loopback_ip=r["Loopback"],AS_number=r["ASN"],debug=DEBUG)
		node.publish_my_consumer_routes()
		

	time.sleep(3)
	print "\n\n	"
	for r in ROUTER_INFO:
			node = BGPNode(host=r["Name"],loopback_ip=r["Loopback"],AS_number=r["ASN"],debug=DEBUG)		
			node.router_write_file()
			print "Writing file ->",node.host





	# Setup e-BGP sessions
	if DEBUG:
		print ""
		print "-----"*7
		print "Setting up e-BGP in routers"
		print "-----"*7
	
	for rl in routerlinks:
		r1,r2 = getLinkInfo(rl)
		#if r2["host"] in ["west","east","sr"]:
		if r2["host"] in ["west","east"]:
			#print r1["host"],"--->",r2["host"]
			r2["Net"] = r2["host"]
			r2["Loopback"] = r2["ip"]
			r2["Name"] = r2["host"]
			r2["ASN"] = asns_details_hash[r2["Net"]]["ASN"]


			r1["Net"] = r1["host"] if r1["host"] in ["east","west"] else "i2"
			r1["ASN"] = asns_details_hash[r1["Net"]]["ASN"]
			r1["Name"] = r1["host"]
			r1["Loopback"] = router_details_hash[r1["Name"]]["Loopback"]

			#print r1,"--->",r2


			node1 = BGPNode(host=r1["Name"],loopback_ip=r1["Loopback"],AS_number=r1["ASN"],debug=DEBUG)
			node2 = BGPNode(host=r2["Name"],loopback_ip=r2["Loopback"],AS_number=r2["ASN"],debug=DEBUG)


			# Setup apparent loopback_ips for EBGP
			node1.ebgp_loopback_ip = node1.loopback_ip
			node2.ebgp_loopback_ip = node2.loopback_ip

			if node1.host == "SEAT":
				node1.ebgp_loopback_ip = "5.0.1.1"
			if node1.host == "NEWY":
				node1.ebgp_loopback_ip = "6.0.1.1"
			
			if node2.host == "SEAT":
				node2.egbp_loopback_ip = "5.0.1.1"
			if node2.host == "NEWY":
				node2.ebgp_loopback_ip = "6.0.1.1"

			node1.set_AS_number()
			node2.set_AS_number()

			print ""

			node1.set_ebgp(node2)
			node2.set_ebgp(node1)

			print ""

			node1.publish_my_consumer_routes()
			node2.publish_my_consumer_routes()

			#node1.set_next_hop_self(node2)
			#node2.set_next_hop_self(node1)

			print ""
			time.sleep(1)
			node1.router_write_file()
			node1.router_write_file()
			node2.router_write_file()
			node2.router_write_file()
			print "---> Writing files ->",node1.host,"&",node2.host,"\n\n"