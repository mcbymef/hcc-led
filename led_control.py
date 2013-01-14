#!/usr/bin/python

import serial
import time
import re
import socket
import argparse
import subprocess
import gangliacomm

LEDS_PER_RACK = 58

def serialWriteWithZeroPadding(sigfigs, value, ser):

    #Don't need to send any padding 0's if value is only one digit in length
    if(sigfigs != 1):
        val_size = len(str(value))

        padding_size = sigfigs - val_size

        for num in range(0,padding_size):
            ser.write(str(0))
            time.sleep(.2)

    ser.write(str(value))    

def main():

    parser = argparse.ArgumentParser(description = 'Control the LEDs set up on the RED racks.')
    parser.add_argument('mode', help="Mode of operation: \n\t1) Map Ganglia metrics to the LEDs \n\t2) Set all LEDs to one color \n\t 3) Color Chase \n\t 4) Rainbow Wheel", type=int)
    parser.add_argument('racks', help="Number of racks with LEDS being controlled.", type=int)
    parser.add_argument('-c', '--color', help="Color to set when using mode 2 or mode 3 (set all LEDs to one color and Color chase). Format is: R,G,B where each value is an integer between 0 and 127, inclusive.", default="",type=str)
    parser.add_argument('-o', '--host_ip', help="Host IP of the cluster to gather Ganglia metrics from.")
    parser.add_argument('-p', '--port', help="Port to connect to if retrieving metrics from Ganglia", default=0, type=int)
    parser.add_argument('-m', '--metric', help="Specific metric you wish to observe when retrieving metrics from Ganglia. Must exactly match metric name in ganglia (e.g. load_fifteen).", default="", type=str)
    parser.add_argument('-n', '--node_name', help="Names of the node you wish to get metrics from as listed on Ganglia. This can be the exact name or a regular expression using Python syntax.", default="", type=str)

    #Parse the arguments passed to the program
    args = parser.parse_args();

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

    mode = args.mode
    num_racks = args.racks
    color = args.color
    host_ip = args.host_ip
    port = args.port
    user_metric = args.metric
    node_name = args.node_name

    #Write mode to Arduino
    serialWriteWithZeroPadding(1, mode, ser)

    if(mode == 1):
        gangliacomm.getMetrics(host_ip, port, num_racks, user_metric, node_name, ser)
    elif(mode == 2 or mode == 3):
        if(color == ""):
            #default color is red
            color = 127,0,0
        else:
            color = color.split(',')
            for num in color:
                print num

        time.sleep(.2)
        ser.write(str(unichr(int(color[0]))))
        time.sleep(.2)
        ser.write(str(unichr(int(color[1]))))
        time.sleep(.2)
        ser.write(str(unichr(int(color[2]))))

if __name__ == "__main__":
    main()
