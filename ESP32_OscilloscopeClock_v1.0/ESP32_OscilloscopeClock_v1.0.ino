/******************************************************************************
  
  ESP32 Oscilloscope Clock 
  using internal DACs, with WiFi and ntp sync.
  
  Mauro Pintus , Milano 2018/05/25
  
  How to use it:
  Load this sketch on a ESP32 board using the Arduino IDE 1.8.7
  See Andreas Spiess video linked below if you dont know how to...
  Connect your oscilloscope channels to GPIO25 and GPIO26 of the ESP32
  Connect the ground of the oscilloscope to the GND of the ESP32 board
  Put your Oscilloscope in XY mode
  Adjust the vertical scale of the used channels to fit the clock

  Enjoy Your new Oscilloscope Clock!!! :)

  Additional notes:
  By default this sketch will start from a fix time 10:08:37 everityme 
  you reset the board.
  To change it, modify the variables h,m,s below.

  To synchronize the clock with an NTP server, you have to install 
  the library NTPtimeESP from Andreas Spiess.
  Then ncomment the line //#define NTP, removing the //.
  Edit the WiFi credential in place of Your SSID and Your PASS.
  Check in the serial monitor if it can reach the NTP server.
  You mignt need to chouse a different pool server for your country.

  If you want there is also a special mode that can be enabled uncommenting 
  the line //#define EXCEL, removing the //. In this mode, the sketch
  will run once and will output on the serial monitor all the coordinates
  it has generated. You can use this coordinates to draw the clock 
  using the graph function in Excel or LibreOffice
  This is useful to test anything you want to display on the oscilloscope
  to verify the actual points that will be generated.

  Frank Exoo, Den Bosch 2021/03/20
  Added functionality:
  - Using the RTC functionality of the ESP32
  - Support for printing characters
  - Digital clock

  GitHub Repository
  https://github.com/FrankX0/ESP32_OscilloscopeClock

  Credits:
  Mauro Pintus

  Andreas Spiess
  https://www.youtube.com/watch?v=DgaKlh081tU

  Andreas Spiess NTP Library
  https://github.com/SensorsIot/NTPtimeESP
  
  My project is based on this one:
  https://github.com/maurohh/ESP32_OscilloscopeClock
  
  Thank you!!

******************************************************************************/

#include <driver/dac.h>
#include <soc/rtc.h>
#include <soc/sens_reg.h>
#include "DataTable.h"
#include <ESP32Time.h>

//#define EXCEL
#define NTP


#if defined NTP
  #include <NTPtimeESP.h>
  #include <WiFi.h>
  
  NTPtime NTPch("europe.pool.ntp.org"); // Choose your server pool
  char *ssid      = "";        // Set you WiFi SSID
  char *password  = "";        // Set you WiFi password
  
  int status = WL_IDLE_STATUS;
  strDateTime dateTime;
#endif //

// Change this to set the initial Time
// Now is 10:08:37 (12h)
int h=10;   //Start Hour 
int m=8;    //Start Minutes
int s=37;   //Start Seconds

//Variables
int       lastx,lasty,show;
int       Timeout = 20;
byte      xoffset;
ESP32Time rtc;

//*****************************************************************************
// PlotTable 
//*****************************************************************************

void PlotTable(byte *SubTable, int SubTableSize, int skip, int opt, int offset)
{
  int i=offset;
  while (i<SubTableSize){
    if (SubTable[i+2]==skip){
      i=i+3;
      if (opt==1) if (SubTable[i]==skip) i++;
    }
    Line(SubTable[i],SubTable[i+1],SubTable[i+2],SubTable[i+3]);  
    //if (opt==2){
    //  Line(SubTable[i+2],SubTable[i+3],SubTable[i],SubTable[i+1]); 
    //}
    i=i+2;
    if (SubTable[i+2]==0xFF) break;
  }
}

// End PlotTable 
//*****************************************************************************


//*****************************************************************************
// PlotChar
//*****************************************************************************

void PlotChar(byte (*SubTable)[112], int Character, byte *XoffsetP, byte Yoffset, byte Size)
{
  int i = 2;
  byte Xoffset = *XoffsetP;
  *XoffsetP = Xoffset + SubTable[Character-32][1]*Size;
  int last = SubTable[Character-32][0]*2;
  while (i < last){
    if (SubTable[Character-32][i+2]==0xFF){
      i=i+4;
    }
    //delay(5);
    Line(SubTable[Character-32][i]*Size+Xoffset,SubTable[Character-32][i+1]*Size+Yoffset,SubTable[Character-32][i+2]*Size+Xoffset,SubTable[Character-32][i+3]*Size+Yoffset);  
    i=i+2;
  }
}

// End PlotChar
//*****************************************************************************


//*****************************************************************************
// PlotText
//*****************************************************************************

void PlotText(char *Text, byte *Xoffset, byte Yoffset, byte Size)
{
  int i,t,last;
  t = 0;
  int len = strlen(Text);
  while (t < len) {
    //Serial.print(Text[t]);
      PlotChar(CharData,Text[t],Xoffset,Yoffset,Size);
    t++;
  }
}

// End PlotText
//*****************************************************************************


//*****************************************************************************
// Dot 
//*****************************************************************************

inline void Dot(int x, int y)
{
    x = 254 - x; //mirror
    if (lastx!=x){
      lastx=x;
      dac_output_voltage(DAC_CHANNEL_1, x);
    }
    #if defined EXCEL
      Serial.print("0x");
      if (x<=0xF) Serial.print("0");
      Serial.print(x,HEX);
      Serial.print(",");
    #endif
    #if defined EXCEL
      Serial.print("0x");
      if (lasty<=0xF) Serial.print("0");
      Serial.print(lasty,HEX);
      Serial.println(",");
    #endif
    if (lasty!=y){
      lasty=y;
      dac_output_voltage(DAC_CHANNEL_2, y);
    }
    #if defined EXCEL
      Serial.print("0x");
      if (x<=0xF) Serial.print("0");
      Serial.print(x,HEX);
      Serial.print(",");
    #endif
    #if defined EXCEL
      Serial.print("0x");
      if (y<=0xF) Serial.print("0");
      Serial.print(y,HEX);
      Serial.println(",");
    #endif
}

// End Dot 
//*****************************************************************************


//*****************************************************************************
// Line 
//*****************************************************************************
// Bresenham's Algorithm implementation optimized
// also known as a DDA - digital differential analyzer

void Line(byte x1, byte y1, byte x2, byte y2) //Y-mirrored
//void Line(byte y1, byte x1, byte y2, byte x2) //CCW-rotated
{
    int acc;
    // for speed, there are 8 DDA's, one for each octant
    if (y1 < y2) { // quadrant 1 or 2
        byte dy = y2 - y1;
        if (x1 < x2) { // quadrant 1
            byte dx = x2 - x1;
            if (dx > dy) { // < 45
                acc = (dx >> 1);
                for (; x1 <= x2; x1++) {
                    Dot(x1, y1);
                    acc -= dy;
                    if (acc < 0) {
                        y1++;
                        acc += dx;
                    }
                }
            }
            else {   // > 45
                acc = dy >> 1;
                for (; y1 <= y2; y1++) {
                    Dot(x1, y1);
                    acc -= dx;
                    if (acc < 0) {
                        x1++;
                        acc += dy;
                    }
                }
            }
        }
        else {  // quadrant 2
            byte dx = x1 - x2;
            if (dx > dy) { // < 45
                acc = dx >> 1;
                for (; x1 >= x2; x1--) {
                    Dot(x1, y1);
                    acc -= dy;
                    if (acc < 0) {
                        y1++;
                        acc += dx;
                    }
                }
            }
            else {  // > 45
                acc = dy >> 1;
                for (; y1 <= y2; y1++) {
                    Dot(x1, y1);
                    acc -= dx;
                    if (acc < 0) {
                        x1--;
                        acc += dy;
                    }
                }
            }        
        }
    }
    else { // quadrant 3 or 4
        byte dy = y1 - y2;
        if (x1 < x2) { // quadrant 4
            byte dx = x2 - x1;
            if (dx > dy) {  // < 45
                acc = dx >> 1;
                for (; x1 <= x2; x1++) {
                    Dot(x1, y1);
                    acc -= dy;
                    if (acc < 0) {
                        y1--;
                        acc += dx;
                    }
                }
            
            }
            else {  // > 45
                acc = dy >> 1;
                for (; y1 >= y2; y1--) { 
                    Dot(x1, y1);
                    acc -= dx;
                    if (acc < 0) {
                        x1++;
                        acc += dy;
                    }
                }

            }
        }
        else {  // quadrant 3
            byte dx = x1 - x2;
            if (dx > dy) { // < 45
                acc = dx >> 1;
                for (; x1 >= x2; x1--) {
                    Dot(x1, y1);
                    acc -= dy;
                    if (acc < 0) {
                        y1--;
                        acc += dx;
                    }
                }

            }
            else {  // > 45
                acc = dy >> 1;
                for (; y1 >= y2; y1--) {
                    Dot(x1, y1);
                    acc -= dx;
                    if (acc < 0) {
                        x1--;
                        acc += dy;
                    }
                }
            }
        }
    
    }
}

// End Line 
//*****************************************************************************


//*****************************************************************************
// setup 
//*****************************************************************************

void setup() 
{
  Serial.begin(115200);
  Serial.println("\nESP32 Oscilloscope Clock v1.0");
  Serial.println("Mauro Pintus 2018\nwww.mauroh.com");
  //rtc_clk_cpu_freq_set(RTC_CPU_FREQ_240M);
  Serial.println("CPU Clockspeed: ");
  Serial.println(rtc_clk_cpu_freq_value(rtc_clk_cpu_freq_get()));
  
  dac_output_enable(DAC_CHANNEL_1);
  dac_output_enable(DAC_CHANNEL_2);

  if (h > 12) h=h-12;

  #if defined NTP
    Serial.println("Connecting to Wi-Fi");
    
    WiFi.begin (ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(1000);
      Timeout--;
      if (Timeout==0){
        Serial.println("\nWiFi Timeout");
        break;
      }
    }
    
    if (Timeout!=0){
      Serial.println("\nWiFi connected");
      Serial.println("NTP request sent to Server.");
      dateTime = NTPch.getNTPtime(1.0, 1);
      Timeout=20;
  
      while (!dateTime.valid) {
        dateTime = NTPch.getNTPtime(1.0, 1);
        Serial.print(".");
        delay(1000);
        Timeout--;
        if (Timeout==0){
          Serial.println("\nNTP Server Timeout");
          break;
        }
      }
      
      if (Timeout!=0){

        Serial.println("\nUsing NTP Time");
        NTPch.printDateTime(dateTime);
    
        byte actualHour      = dateTime.hour;
        byte actualMinute    = dateTime.minute;
        byte actualsecond    = dateTime.second;
        int  actualyear      = dateTime.year;
        byte actualMonth     = dateTime.month;
        byte actualday       = dateTime.day;
        byte actualdayofWeek = dateTime.dayofWeek;

        //if (actualHour > 12) actualHour=actualHour-12;

        rtc.setTime(actualsecond, actualMinute, actualHour, actualday, actualMonth, actualyear);
        
        h=actualHour;
        m=actualMinute;
        s=actualsecond;
      }
      else{
        Serial.println("\nUsing Fix Time");
      }
    }
    WiFi.mode(WIFI_OFF);  
  #endif    

  #if !defined NTP
    Serial.println("Using Fix Time");
  #endif

  if (h<10) Serial.print("0");
  Serial.print(h);
  Serial.print(":");
  if (m<10) Serial.print("0");
  Serial.print(m);
  Serial.print(":");
  if (s<10) Serial.print("0");
  Serial.println(s);
  h=(h*5)+m/12;
}

// End setup 
//*****************************************************************************


//*****************************************************************************
// loop 
//*****************************************************************************
void loop() {

  s = rtc.getSecond();
  m = rtc.getMinute();
  h = rtc.getHour()*5+m/12;
  
  show = s / 10;
  if (show == 0 or show == 2 or show == 4) {
    // Analog clock
    PlotTable(DialData,sizeof(DialData),0x00,1,0);
    PlotTable(DialDigits12,sizeof(DialDigits12),0x00,1,0);
    PlotTable(HrPtrData, sizeof(HrPtrData), 0xFF,0,9*h);  // 9*h
    PlotTable(MinPtrData,sizeof(MinPtrData),0xFF,0,9*m);  // 9*m
    PlotTable(SecPtrData,sizeof(SecPtrData),0xFF,0,5*s);  // 5*s
  }
  else {
    // Digital clock
    offset = 10;
    char myString[20];
    sprintf(myString, "%02d:%02d", rtc.getHour(true),rtc.getMinute());
    PlotText(myString,&offset,140,2);
    sprintf(myString, ":%02d", rtc.getSecond());
    PlotText(myString,&offset,140,1);
    sprintf(myString, "%02d-%02d-%d", rtc.getDay(),rtc.getMonth(),rtc.getYear());
    //Serial.println(myString);
    offset = 20;
    PlotText(myString,&offset,80,1);
  }

  #if defined EXCEL
    while(1);
  #endif 
}

// End loop 
//*****************************************************************************
