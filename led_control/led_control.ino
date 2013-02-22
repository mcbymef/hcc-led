#include "LPD8806.h"
#include "SPI.h"
#include "Time.h"
 
#define MAX_LEDS 2200
#define LEDS_PER_RACK 110

#define RECV_MODE 0
#define METRIC_SETUP 1
#define METRIC_MODE 2
#define COLOR_FILL_MODE 3
#define COLOR_WIPE_MODE 4
#define COLOR_CHASE_MODE 5
#define RAINBOW_WHEEL_MODE 6
#define WATERFALL_MODE 7

#define BLUE 0x00
#define GREEN 0x01
#define YELLOW 0x02
#define ORANGE 0x03
#define RED 0x04
#define INTENSITY 1

inline uint8_t setPrevColor(uint8_t colorbyte, uint8_t color) {
  return ((colorbyte & 0x0F) | color << 4);
}

inline uint8_t setCurrColor(uint8_t colorbyte, uint8_t color) {
  return ((colorbyte & 0xF0) | color);
}

inline uint8_t getPrevColor(uint8_t colorbyte) {
  return (colorbyte >> 4);
}

inline uint8_t getCurrColor(uint8_t colorbyte) {
  return (colorbyte & 0x0F);
}

uint8_t temp, pixel, low, high;
uint8_t lowgreen, lowyellow, highyellow, highorange;
uint8_t num_iters = 0;
uint8_t state;
uint8_t currTemp[MAX_LEDS];
uint8_t delta[MAX_LEDS/2];
unsigned char colors[MAX_LEDS];
float resolution = 0.0;
boolean needToUpdate = false;
boolean heatup = false;
boolean scaleTempsUp = false;
time_t timestamp;

inline uint8_t getDelta(uint16_t index) {

  if(index % 2 == 0) {
   return ( delta[index >> 1] >> 4 ); 
  } else {
   return ( delta[index >> 1] & 0x0F);
  }    
}

inline uint8_t setDelta(uint16_t index, uint8_t deltaValue) {

  if(index % 2 == 0) {
   return ( (delta[index >> 1] & 0x0F) |(deltaValue << 4) );
  } else {
    //DELTA is guaranteed to be contained in lower 4 bits
   return ( (delta[index >> 1] & 0xF0) | deltaValue);
  }
}

uint16_t num_leds = 0;
uint8_t num_racks = 0;
unsigned char info_available = 0;

LPD8806 strip = LPD8806();

void setup() {
  
  XMCRA = _BV(SRE); //Enable external memory interface
  pinMode(38, OUTPUT);
  digitalWrite(38, LOW); //Enable RAM device
  
  //Select Bank 0 - this extends the RAM to ~ 56KB
  pinMode(42, OUTPUT);
  pinMode(43, OUTPUT);
  pinMode(44, OUTPUT);
  digitalWrite(42, LOW);
  digitalWrite(43, LOW);
  digitalWrite(44, LOW);
  
  //Keeps the Arduino as Master in SPI communication
  pinMode(53, OUTPUT);
  
  Serial.begin(115200);	// opens serial port, sets data rate to 115200 bps
  
  //Recieve number of LEDS from the python script
  while(!info_available) {
    //Wait until 4 bytes are recieved
    if(Serial.available() > 3) {
      info_available = 1;
    }
  }
  
  uint8_t byte1 = (Serial.read() - 48);
  uint8_t byte2 = (Serial.read() - 48);
  uint8_t byte3 = (Serial.read() - 48);
  uint8_t byte4 = (Serial.read() - 48);
  num_leds = byte1*1000 + byte2*100 + byte3*10 + byte4;
 
  num_racks = num_leds / LEDS_PER_RACK;
  
  //Second parameter is data pin, third paramter is clock pin
  //these can be assigned to any pin on the Arduino
  strip = LPD8806(num_leds);
  Serial.print("This is num pixels: ");
  Serial.println(strip.numPixels());
  Serial.flush();
  
  strip.begin();

  for(uint16_t i = 0; i < strip.numPixels(); i++)
  {
    colors[i] = 0x00;
    currTemp[i] = 0;
    delta[i] = 0;
    strip.setPixelColor(i, strip.Color(0,0,127/INTENSITY));
  }

  strip.show();

  info_available = 0;

  Serial.print("Num racks: ");
  Serial.println(num_racks);
  state = RECV_MODE;
}

void loop()
{
  if(state == RECV_MODE) 
  { 
    if(Serial.available() == 1) 
    {
        //Read ASCII byte from python script, convert to integer
        state = Serial.read() - 48;
    }    
  }
  else if(state == METRIC_SETUP)
  {
    if(Serial.available() == 6)
    {
      int stepsize = 0;

      low = ((Serial.read() - 48)*100 + (Serial.read() - 48)*10 + (Serial.read() - 48));
      high = ((Serial.read() - 48)*100 + (Serial.read() - 48)*10 + (Serial.read() - 48));

      stepsize = (high - low)/(5);  //this will determine how big each range of colors will be

      if(stepsize == 0) 
      {
        //The temperatures are too close together
        //stepsize set automatically to one
        stepsize = 1;
        
        //scale high and low temps
        high = high << 1;
        low = low << 1;
        //the temperatures will be scaled to give an interesting visual
        scaleTempsUp = true;
      }

      lowgreen = low + stepsize;
      lowyellow = lowgreen + stepsize;
      highorange = high - stepsize;
      highyellow = highorange - stepsize;

      Serial.print("Low is: ");
      Serial.println(low);
      Serial.print("High is: ");
      Serial.println(high);

      state = METRIC_MODE;    
    }
  }
  else if(state == METRIC_MODE)
  {  
    if (Serial.available() == 6)
    {
      timestamp = now();

      temp = ((Serial.read() - 48)*100 + (Serial.read() - 48)*10 + (Serial.read() - 48));
      pixel = ((Serial.read() - 48)*100 + (Serial.read() - 48)*10 + (Serial.read() - 48)) - 1;
      
      if(scaleTempsUp)
      {
        temp = temp << 1;
      }

      Serial.print("Temp is: ");
      Serial.println(temp);
      Serial.print("Pixel is: ");
      Serial.println(pixel);


      //Update the temperature array. Faster than trying to update each pixel as data becomes available 
      if(pixel < strip.numPixels() && pixel >= 0 && currTemp[pixel] != temp)
      {
        currTemp[pixel] = temp;

        needToUpdate = true;
      }
    }    

    //If 5 seconds have elapsed since last input, update the temperature arrays 
    if(now() - timestamp >= 5 && needToUpdate)
    {     
      //Set the new colors based off of the new temperatures received
      updateColorsArrays();

      //update the detla array
      for(uint16_t p = 0; p < num_leds; p++)
      {
        delta[p >> 1] = setDelta(p, abs((int8_t)getCurrColor(colors[p]) - (int8_t)getPrevColor(colors[p])));
      }    

      for(uint8_t j = 0; j < 128; j++)
      { 
        for(uint8_t i = 0; i < num_racks; i++)
        {

          for(uint16_t k = 0; k < 58; k++) {
            
            int pixelnum = k + (52 * i);

            if(getCurrColor(colors[pixelnum]) - getPrevColor(colors[pixelnum]) < 0)
            {
              heatup = false;              
            }
            else
            {
              heatup = true;
            }

            Serial.print("Color diff: ");
            Serial.println(getCurrColor(colors[pixelnum]) - getPrevColor(colors[pixelnum]));
            Serial.print("Delta: ");
            Serial.println(getDelta(pixelnum));

            if(getCurrColor(colors[pixelnum]) - getPrevColor(colors[pixelnum]) != 0 && getDelta(pixelnum) != 0)
            {   
              pixelUpdate(pixelnum, j, getDelta(pixelnum), heatup);
            }   
          }  
        }
        strip.show();
        //Processing time for a full strip will make this delay unnecesary I believe
        delay(10); 
      }

      strip.show();   

      needToUpdate = false;

      state = RECV_MODE;  

      Serial.println("changing state");
    }
  } 
  else if (state == COLOR_FILL_MODE)
  {
     while(!info_available) {
       if(Serial.available() == 3) {
         info_available = 1; 
       }
     }
     
     uint8_t r = Serial.read();
     uint8_t g = Serial.read();
     uint8_t b = Serial.read();
     
     colorFill(strip.Color(r/INTENSITY, g/INTENSITY, b/INTENSITY), 10);
         
     strip.show();
     
     info_available = 0;
    
     state = RECV_MODE;
  }
  else if (state == COLOR_WIPE_MODE) {
    colorFill(strip.Color(0,0,0), 10);
    strip.show();
    
    state = RECV_MODE;
  }
  else if (state == COLOR_CHASE_MODE)
  {
    while(!info_available) {
       if(Serial.available() == 3) {
         info_available = 1; 
       }
     }
     
     uint8_t r = Serial.read();
     uint8_t g = Serial.read();
     uint8_t b = Serial.read();
    
     colorChase(strip.Color(r/INTENSITY, g/INTENSITY, b/INTENSITY), 20);
     info_available = 0;
     
     state = RECV_MODE;
  }
  else if (state == RAINBOW_WHEEL_MODE)
  {
    rainbowCycle(10);
    state = RECV_MODE;
  }
  else if (state == WATERFALL_MODE) 
  {
    while(!info_available) {
       if(Serial.available() == 3) {
         info_available = 1; 
       }
     }
     
     uint8_t r = Serial.read();
     uint8_t g = Serial.read();
     uint8_t b = Serial.read();
    
     waterfallChase(strip.Color(r/INTENSITY, g/INTENSITY, b/INTENSITY), 10);    
     
     info_available = 0;
     
     state = RECV_MODE;   
  }
  /*
  else if (state == LAFORGE_MODE)
  {
    
    while(!info_available) {
       if(Serial.available() == 3) {
         info_available = 1; 
       }
     }
     
     uint8_t r = Serial.read();
     uint8_t g = Serial.read();
     uint8_t b = Serial.read();
   
     int i;
  
   for(i = 0; i < 5; i++) {
    laforgeMode(strip.Color(r/INTENSITY, g/INTENSITY, b/INTENSITY), 10);
   }
    info_available = 0;
    
    state = RECV_MODE;  
  }*/
}

void pixelUpdate(int pixelnum, uint8_t j, int delta, boolean heatup)
{  
  Serial.println("IN PIXEL UPDATE");
  num_iters = 128/delta + 1;
  
  int relativepixelnum = pixelnum % 110;
  
  int crosspixel = ((58 - relativepixelnum) * 2 - 1) + pixelnum;
  
  if(relativepixelnum < 26) crosspixel -= 6;
  
  if(j == 0) {
  Serial.print("Pixelnum: ");
  Serial.println(pixelnum);
  Serial.print("Crosspixel: ");
  Serial.println(crosspixel);
  }
  
  if(heatup)
  {
    switch(getPrevColor(colors[pixelnum]))
    {
    case 0: //Blue 
      resolution = 127.0/(num_iters-1);

      if(resolution * j < 127) 
      {
        strip.setPixelColor(pixelnum, strip.Color(0, (resolution*j)/INTENSITY, (127 - resolution*j)/INTENSITY));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
        strip.setPixelColor(crosspixel, strip.Color(0, (resolution*j)/INTENSITY, (127 - resolution*j)/INTENSITY));
        }
      }
      else
      {
        colors[pixelnum] = setPrevColor(colors[pixelnum], GREEN);
      }
      break;
    case 1: //Green

      resolution = 127.0/(num_iters-1);

      if((j%num_iters)*resolution < 127)
      {
        strip.setPixelColor(pixelnum, strip.Color( resolution*(j % (num_iters)/INTENSITY), 127/INTENSITY, 0));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color( resolution*(j % (num_iters)/INTENSITY), 127/INTENSITY, 0));
        }  
      }
      else
      {
        colors[pixelnum] = setPrevColor(colors[pixelnum], YELLOW);
      }
      break;
    case 2: //Yellow      

      resolution = 87.0/(num_iters-1);

      if((resolution * (j % num_iters)) < 87)
      {
        strip.setPixelColor(pixelnum, strip.Color(127/INTENSITY, (127 - (resolution * (j % num_iters)))/INTENSITY, 0));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color(127/INTENSITY, (127 - (resolution * (j % num_iters)))/INTENSITY, 0));
        }
      }
      else
      {
        colors[pixelnum] = setPrevColor(colors[pixelnum], ORANGE);
      }
      break;
    case 3: //Orange  

      resolution = 40.0/(num_iters-1);

      if((resolution * (j % num_iters)) < 40)
      {
        strip.setPixelColor(pixelnum, strip.Color(127/INTENSITY, (40 - (resolution * (j % num_iters)))/INTENSITY, 0));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color(127/INTENSITY, (40 - (resolution * (j % num_iters)))/INTENSITY, 0));
        }
      }
      else
      {
        colors[pixelnum] = setPrevColor(colors[pixelnum], RED);
      }
      break;
    }
  }
  else
  {  
    //Now we are cooling down
    switch(getPrevColor(colors[pixelnum]))
    {                   

    case 1: //Green

      resolution = 127.0/(num_iters-1);

      if(resolution * (j % num_iters) < 127) 
      {
        strip.setPixelColor(pixelnum, strip.Color(0, (127 - (resolution * (j % num_iters)))/INTENSITY, (resolution * (j % num_iters))/INTENSITY));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color(0, (127 - (resolution * (j % num_iters)))/INTENSITY, (resolution * (j % num_iters))/INTENSITY));
        }
      }
      else
      {
        colors[pixelnum] = setPrevColor(colors[pixelnum], BLUE);
      }
      break;
    case 2: //Yellow

      resolution = 127.0/(num_iters-1);

      if(resolution * (j % num_iters) < 127) 
      {
        strip.setPixelColor(pixelnum, strip.Color((127 - (resolution * (j % num_iters)))/INTENSITY, 127/INTENSITY, 0));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color((127 - (resolution * (j % num_iters)))/INTENSITY, 127/INTENSITY, 0));
        }
      }
      else
      {
       colors[pixelnum] = setPrevColor(colors[pixelnum], GREEN);
      }
      break;
    case 3: //Orange

      resolution = 87.0/(num_iters-1);

      if(resolution * (j % num_iters) < 87) 
      {
        strip.setPixelColor(pixelnum, strip.Color(127/INTENSITY, (40 + (resolution * (j % num_iters)))/INTENSITY, 0));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color(127/INTENSITY, (40 + (resolution * (j % num_iters)))/INTENSITY, 0));
        }
      }
      else
      {
       colors[pixelnum] = setPrevColor(colors[pixelnum], YELLOW);
      }
      break;
    case 4: //Red

      resolution = 40.0/(num_iters-1);

      if( j * resolution < 40)
      {
        strip.setPixelColor(pixelnum, strip.Color(127/INTENSITY, (j*resolution)/INTENSITY, 0));
        if( relativepixelnum <= 25 || relativepixelnum >= 32) {
          strip.setPixelColor(crosspixel, strip.Color(127/INTENSITY, (j*resolution)/INTENSITY, 0));
        }
      }
      else
      {
       colors[pixelnum] = setPrevColor(colors[pixelnum], ORANGE);
      }
      break;      
    }   
  }
}


void updateColorsArrays()
{
  for(uint16_t i = 0; i < num_leds; i++)
  {        
    //prev = curr
    colors[i] = setPrevColor(colors[i], getCurrColor(colors[i]));  

    if(currTemp[i] >= high)
    {
      colors[i] = setCurrColor(colors[i], RED);
    }
    else if(currTemp[i] < high && currTemp[i] >= highyellow)
    {
      colors[i] = setCurrColor(colors[i], ORANGE);
    }
    else if(currTemp[i] < highyellow && currTemp[i] >= lowyellow)
    {
      colors[i] = setCurrColor(colors[i], YELLOW);
    }
    else if(currTemp[i] < lowyellow && currTemp[i] >= low)
    {		
      colors[i] = setCurrColor(colors[i], GREEN);
    }
    else if(currTemp[i] < low)
    {        
      colors[i] = setCurrColor(colors[i], BLUE);
    }
    
    
    Serial.print("Current color: ");
    Serial.println(colors[i]);
    
  }
}  

/*************************************************************************************************
|
| THIS SECTION HAS BEEN COPIED DIRECTLY FROM THE "strandtest" EXAMPLE IN THE LPD8806 LIBRARY
|
**************************************************************************************************/


// Slightly different, this one makes the rainbow wheel equally distributed 
// along the chain
void rainbowCycle(uint8_t wait) {
  uint16_t i, j;
  
  for (j=0; j < 384 * 5; j++) {     // 5 cycles of all 384 colors in the wheel
    for (i=0; i < strip.numPixels(); i++) {
      // tricky math! we use each pixel as a fraction of the full 384-color wheel
      // (thats the i / strip.numPixels() part)
      // Then add in j which makes the colors go around per pixel
      // the % 384 is to make the wheel cycle around
      strip.setPixelColor(i, Wheel( ((i * 384 / strip.numPixels()) + j) % 384) );
    }  
    strip.show();   // write all the pixels out
    delay(wait);
  }
}

// Fill the dots progressively along the strip.
void colorFill(uint32_t c, uint8_t wait) {
  int i;

  for (i=0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, c);
      strip.show();
      delay(wait);
  }
}

// Chase one dot down the full strip.
void colorChase(uint32_t c, uint8_t wait) {
  int i;

  // Start by turning all pixels off:
  for(i=0; i<strip.numPixels(); i++) strip.setPixelColor(i, 0);

  // Then display one pixel at a time:
  for(i=0; i<strip.numPixels(); i++) {
    strip.setPixelColor(i, c); // Set new pixel 'on'
    strip.show();              // Refresh LED states
    strip.setPixelColor(i, 0); // Erase pixel, but don't refresh!
    delay(wait);
  }

  strip.show(); // Refresh to turn off last pixel
}

void waterfallChase(uint32_t c, uint8_t wait) {
   int i;

   //start by turning all pixels off:
   for(i = 0; i < strip.numPixels(); i++) strip.setPixelColor(i, 0);

   //Then display one pixel on each side of the strip at a time:
   int numracks = strip.numPixels()/LEDS_PER_RACK;
   
   for(i = 0; i < numracks; i++) {

     //i is the rack number
     int offset = i * LEDS_PER_RACK;
     int altpixel = 109 + offset;
     int primarypixel = 0 + offset;
     
     while(altpixel >= 0 + offset && primarypixel <= 109 + offset) {
       
       if(altpixel >= 84 + offset) {
         strip.setPixelColor(primarypixel,c);
         strip.setPixelColor(altpixel, c);
         
       } else if (altpixel < 84 + offset && primarypixel <= 31 + offset) { //This is the handle area where it gets weird
         strip.setPixelColor(primarypixel,c);
         
       } else if (altpixel < 84 + offset && primarypixel < 84 + offset) {
         strip.setPixelColor(primarypixel,c);
         strip.setPixelColor(altpixel,c);
         
       } else if (primarypixel >= 84 + offset && altpixel >= 25 + offset) {//This is the second weird handle part
         strip.setPixelColor(altpixel,c);

       } else if (altpixel < 25 + offset) {
         strip.setPixelColor(primarypixel,c);
         strip.setPixelColor(altpixel, c);
       }
         
       strip.show();
       
     if(altpixel >= 84 + offset) {
         strip.setPixelColor(primarypixel,0);
         strip.setPixelColor(altpixel, 0);
         altpixel--;
         primarypixel++;
       } else if (altpixel < 84 + offset && primarypixel <= 31 + offset) { //This is the handle area where it gets weird
         strip.setPixelColor(primarypixel,0);
         primarypixel++;
       } else if (altpixel < 84 + offset && primarypixel < 84 + offset) {
         strip.setPixelColor(primarypixel,0);
         strip.setPixelColor(altpixel,0);
         altpixel--;
         primarypixel++;
       } else if (primarypixel >= 84 + offset && altpixel >= 25 + offset) {//This is the second weird handle part
         strip.setPixelColor(altpixel,0);
         altpixel--;
       } else if (altpixel < 25 + offset) {
         strip.setPixelColor(primarypixel,0);
         strip.setPixelColor(altpixel, 0);
         altpixel--;
         primarypixel++;
       }
     delay(wait);       
     }
   }

  strip.show();
}

/* Helper functions */

//Input a value 0 to 384 to get a color value.
//The colours are a transition r - g -b - back to r

uint32_t Wheel(uint16_t WheelPos)
{
  byte r, g, b;
  switch(WheelPos / 128)
  {
    case 0:
      r = 127 - WheelPos % 128;   //Red down
      g = WheelPos % 128;      // Green up
      b = 0;                  //blue off
      break; 
    case 1:
      g = 127 - WheelPos % 128;  //green down
      b = WheelPos % 128;      //blue up
      r = 0;                  //red off
      break; 
    case 2:
      b = 127 - WheelPos % 128;  //blue down 
      r = WheelPos % 128;      //red up
      g = 0;                  //green off
      break; 
  }
  return(strip.Color(r,g,b));
}

