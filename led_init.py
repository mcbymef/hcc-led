#!/usr/bin/python

import serial
import time
import re
import socket
import argparse
import subprocess
import serial_control

LEDS_PER_RACK = 58  

def main():

    parser = argparse.ArgumentParser(description = 'Control the LEDs set up on the RED racks.')
    parser.add_argument('racks', help="Number of racks with LEDS being controlled.", type=int)

    #Parse the arguments passed to the program
    args = parser.parse_args();

    num_racks = args.racks

    num_leds = num_racks * LEDS_PER_RACK

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

    #Write number of leds to Arduino
    #Max number of LEDS is 1200 which is 4 digits long hence, number of LEDs is always
    #send to the Arduino as a 4 digit number
    serial_control.serialWriteWithZeroPadding(4, num_leds, ser)

if __name__ == "__main__":
    main()
