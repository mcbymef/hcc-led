TO RUN:

1.) Physically press the reset button on the Arduino
2.) Run the led_init.py script entering the number of active racks as the argument
    - This tells the Arduino how many LEDs it's controlling and CAN NOT BE CHANGED unless you reset the Arduino and run the led_init.py script again.
3.) Run the led_control.py script to perform whatever function you desire with the strips. For more on the operation of this script, type "python led_control.py -h"



NOTE:
    Number of LED's must alway be represented by 4 digits. For example if you are running 438 LEDs, this must be sent to the Arduino as 0438. The led_init.py script takes care of this but if you're going to modify this file, be aware.
