#!/usr/bin/python

import serial
import time

def serialWriteWithZeroPadding(digits, value, ser):

    #Don't need to send any padding 0's if value is only one digit in length
    if(digits != 1):
        val_size = len(str(value))

        padding_size = digits - val_size

        for num in range(0,padding_size):
            ser.write(str(0))
            
    time.sleep(.15)

    ser.write(str(value))   
