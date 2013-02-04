#!/usr/bin/python

import serial
import time
import re
import socket
import argparse
import subprocess
import random
import serial_control
from operator import itemgetter

LEDS_PER_RACK = 58

def main():

    parser = argparse.ArgumentParser(description = 'Retrieve metrics from Ganglia.')
    parser.add_argument('host_ip', help="host IP address (i.e. 129.93.239.169)")
    parser.add_argument('racks', help="Number of racks with LEDS being controlled.", type=int)
    parser.add_argument('-p', '--port', help="port to connect", default=0, type=int)
    parser.add_argument('-m', '--metric', help="Specific temperature metric you wish to observe. Must exactly match metric name in ganglia and must be a temperature metric", default="", type=str)
    parser.add_argument('-n', '--node_name', help="Names of the node you wish to get temperature from as listed on Ganglia. This can be the exact name or a regular expression using Python syntax.", default="", type=str)

    #Parse the arguments passed to the program
    args = parser.parse_args()

    #Set up a serial connection to the Arduino
    try:
    	#Try ttyACM0 first
        ser = serial.Serial('/dev/ttyACM0', 115200)
    except:
        try:
            #Try ttyACM1 second
            ser = serial.Serial('/dev/ttyACM1', 115200)
        except:
            print "Unable to open Arduino device. Please ensure the device is connected to the computer."
            exit(1)

    host_ip = args.host_ip
    port = args.port
    num_racks = args.racks
    user_metric = args.metric
    node_name = args.node_name

    getMetrics(host_ip, port, num_racks, user_metric, node_name, ser)
 
def getMetrics(host_ip, port, num_racks, user_metric, node_name, ser):
 
    #Default parameters for RED
    if(host_ip == "129.93.239.169"):
        if(user_metric == ""):
            metric = "<METRIC NAME=\"planar_temp\" VAL=\"[0-9]"
        else:
            metric = "<METRIC NAME=\"" + user_metric + "\" VAL=\"[0-9]"

        if(node_name == ""):
            node_name = "<HOST NAME=\"red-d"
        else:
            node_name = "<HOST NAME=\"" + node_name
        if(port == 0):
            port = 8651

    #Create an array to store lines of data we will get from ganglia
    lines = [""]

    #Create new socket to connect to cluster
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((host_ip, port))
    except:
        print "Unable to connect to the specified host. Please ensure you have correclty typed the name or IP address of the host and specified the correct port."
        exit(1)

    #Get all the lines that ganglia sends back and store in lines array
    while 1:
        data = s.recv(4096)
        if not data:
            break
        datachunks = data.split('\n')
        for chunk in datachunks:
            lines.append(chunk)
    s.close()

    #create temps array to store temperature data 
    temps = []

    #Active flag is used once we hit a node we are interested in
    active = 0

    for line in lines:
        #Identify a node we are interested based on the node_name and grab the full IP address
        if(re.match(node_name, line)):
            chunks = line.split()
            if(len(chunks) > 2):
                bits = chunks[2].split('"')
                if(len(bits) > 1):
                    ip = bits[1]
            active = 1

        #It is possible to get the metric from a node we are not interested in hence the active variable
        #This will identify when we find the required metric and are in an acceptable node
        if(active and re.match(metric, line)):
            chunks = line.split()
            if(len(chunks) > 2):
                val = chunks[2].split('"')
            if(len(val) > 1):
                temp = val[1]
            if(temp != "" and temp != "0" and temp != 0):
                ip_split = ip.split(".")            
                if(len(ip_split) > 3):
                    #store the 3rd IP octet, 4th IP octet, and temperature in a tuple and append the tuple to the temps array
                    try:
                        #For some reason, red-d8-6 has an ip of 3.6 instead of 8.6
                        #ask garhan about this
                        if (ip_split[2] == 3 or ip_split[2] == '3'):
                            ip_split[2] = 8
                        temps.append((int(ip_split[2]),int(ip_split[3]), int(float(temp))))
                    except ValueError:
                        print "Encountered incorrect value."
                        print "3rd IP octet: %s" % ip_split[2]
                        print "4th IP octet: %s" % ip_split[3]
                        print "Temp: %s" % temp
                    
            active = 0 

    #This will sort the list of temperatures according to the 3rd octet of the IP and then by the
    #4th octet of the IP address. 
    sortedtemps = sorted(temps, key=itemgetter(0,1))

    host_count = len(sortedtemps)

    if(host_count == 0):
        print "No metrics found. Please ensure the parameters you have entered are correct."
        exit(1)

    #Find unique Identifier for each rack (this will be the 3rd octet of the IP)
    rack_ID = []

    #Build a list of all rack ID's
    for node in sortedtemps:
        rack_ID.append(node[0])

    #eliminate duplicates by casting to a set then back to a list
    rack_ID = sorted(list(set(rack_ID)))

    #determine temp range
    nonzero_temps = []

    for node in sortedtemps:
        if node[2] != 0:
            nonzero_temps.append(node[2])

    temp_min = min(nonzero_temps)

    temp_max = max(nonzero_temps)

    #send temp range to arduino, temp range is always 3 digits
    serial_control.serialWriteWithZeroPadding(3, temp_min, ser)

    serial_control.serialWriteWithZeroPadding(3, temp_max, ser)

    print temp_min
    print temp_max

    i = 0

    for node in sortedtemps:
	   print node

    #Need a counter to start from the rack ID and count up sequentially
    dummy_rack_counter = rack_ID[0]

    #also need a separate index for the rack_ID array because it will likely have
    #a different number of elements than racks specified
    actual_rack_counter = 0

    node_index = 0

    #For each rack
    for r in range(num_racks):

        node_count = 0

        #Assuming that racks are placed from left to right with ascending third IP octet
        #Because we will not get metrics from some racks, it is possible to identify holes
        #based on the third octed of the IP 
        if(r == 0 or (i < len(sortedtemps) and r > 0 and sortedtemps[i][0] == dummy_rack_counter)):

            curr_rack = rack_ID[actual_rack_counter]
        
            actual_rack_counter += 1

            #Determine how many nodes are in this rack based on IP
            while(i < len(sortedtemps) and sortedtemps[i][0] == curr_rack):
                i += 1
                node_count += 1


            lower_led_count = (LEDS_PER_RACK % node_count) * (int)(LEDS_PER_RACK / node_count + 1)

            upper_led_count = (node_count - LEDS_PER_RACK % node_count) * (int)(LEDS_PER_RACK / node_count)

            p=0
            

            while(p < LEDS_PER_RACK):

                print(node_index)

                temp = sortedtemps[node_index][2]

                #determines how many LEDS per node

                if(p < lower_led_count):
	                leds_per_node = (int)(LEDS_PER_RACK/node_count + 1)
                else:
                    leds_per_node = (int)(LEDS_PER_RACK / node_count)

                for num in range(0, leds_per_node):
                    pixel_number = (r * LEDS_PER_RACK) + p + 1

                    serial_control.serialWriteWithZeroPadding(3, temp, ser)

                    serial_control.serialWriteWithZeroPadding(3, pixel_number, ser)

                    p += 1

                node_index += 1

        else:
            print 'This will be a dummy rack and this is r: %d' % r

            p=0
            while(p < 54):

                temp = random.randint(temp_min, temp_max)

                for num in range(0,2):
                    pixel_number = (r * LEDS_PER_RACK) + p + 1
    
                    serial_control.serialWriteWithZeroPadding(3, temp, ser)

                    serial_control.serialWriteWithZeroPadding(3, pixel_number, ser) 

                    p += 1

            temp = random.randint(temp_min, temp_max)

            for num in range(p,58):
                pixel_number = (r * LEDS_PER_RACK) + num + 1

                serial_control.serialWriteWithZeroPadding(3, temp, ser)

                serial_control.serialWriteWithZeroPadding(3, pixel_number, ser)  

        dummy_rack_counter += 1

if __name__ == "__main__":
    main()

