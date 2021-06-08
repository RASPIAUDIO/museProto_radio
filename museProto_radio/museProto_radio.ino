extern "C"
{
#include "hal_i2c.h"
//
#include "tinyScreen128x64.h"
}
#include "Arduino.h"
#include <Audio.h>

#include "SPIFFS.h"
#include "IotWebConf.h"
#include "lwip/apps/sntp.h"
#include <NeoPixelBus.h>

///////////////////////////////////////////
//#define SCREEN 0            // no screen
#define SCREEN 1             // with screen
///////////////////////////////////////////

//////////////////////////////////////////
//#define beepOK 1
#define beepOK 0
//////////////////////////////////////////

#define I2S_DOUT      26
#define I2S_BCLK      5
#define I2S_LRC       25
#define I2S_DIN       35
#define I2SN (i2s_port_t)0
#define I2CN (i2c_port_t)0
#define SDA 18
#define SCL 23

//Buttons
#define MU GPIO_NUM_12      // short => mute/unmute  long => stop (deep sleep)
#define VM GPIO_NUM_32      // short => volume down  long => previous station
#define VP GPIO_NUM_19      // short => volume up   long => next station
#define CFG GPIO_NUM_12
#define STOP GPIO_NUM_12
//Amp power enable
#define PW GPIO_NUM_21        

#define MAXSTATION 17
#define maxVol 15

//////////////////////////////
// NeoPixel led control
/////////////////////////////
#define PixelCount 1
#define PixelPin 22
RgbColor RED(255, 0, 0);
RgbColor GREEN(0, 255, 0);
RgbColor BLUE(0, 0, 255);
RgbColor YELLOW(255, 128, 0);
RgbColor WHITE(255, 255, 255);
RgbColor BLACK(0, 0, 0);

RgbColor REDL(64, 0, 0);
RgbColor GREENL(0, 64, 0);
RgbColor BLUEL(0, 0, 64);
RgbColor WHITEL(64, 64, 64);
RgbColor BLACKL(0, 0, 0);
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);

int b0 = -1, b1 = -1, b2 = -1;

////////////////////////////////////////////////////
// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "muse";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "musemuse";


DNSServer dnsServer;
WebServer server(80);

void playWav(char* n);
void beep(void);
char* Rlink(int st);
char* Rname(int st);
int maxStation(void);
int touch_get_level(int t);


time_t now;
struct tm timeinfo;
int previousMin = -1;
char timeStr[10];
char comValue[16];
char newNameValue[16];
char newLinkValue[80];
// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "museRadio V0"


Audio audio;
//httpd_handle_t radio_httpd = NULL;

int station = 0;
int previousStation;
int vol= maxVol / 2;
int oldVol;
int previousVol = -1;
int selectedVol;
int previousLevel = -1;
int retries = 0;
int vlevel,vmute;

esp_err_t err;
char ssid[32]= "a";
char pwd[32]= "b";
size_t lg;
int MS;
bool beepON = false;
bool muteON = false;
uint32_t sampleRate;
char* linkS;
bool timeON = false;
bool connected = true;
char mes[200];
int iMes ;
bool started = false;

SemaphoreHandle_t buttonsSem = xSemaphoreCreateMutex();

//////////////////////////////////////////////////////////////////////////
// task for battery monitoring
//////////////////////////////////////////////////////////////////////////
#define NGREEN 2300
#define NYELLOW 1800
static void battery(void* pdata)
{
  int val;
  while(1)
  {
   val = adc1_get_raw(ADC1_GPIO33_CHANNEL);
   printf("Battery : %d\n");
   if(val < NYELLOW) strip.SetPixelColor(0, RED);
   else if(val > NGREEN) strip.SetPixelColor(0, GREEN);
   else strip.SetPixelColor(0, YELLOW);
   strip.Show();   
   delay(10000);
  }
}






void confErr(void)
{
  drawStrC(26, "Error...");
}


////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  param management (via local server)
//        add     : add a new station
//        del     : delete a station
//        mov     : move a station (change position)
////////////////////////////////////////////////////////////////////////////////////////////////////
void configRadio(void)
{
  char com[8];
  char comV[16];
  char* P;
  int n, m;
  char lf[] = {0x0A, 0x00};
  char li[80];
  char na[17];
  started = false;
  clearBuffer();
  drawStrC(16, "initializing...");
  sendBuffer();

  strcpy(comV, comValue);
   P = strtok(comV, ",");
   strcpy(com, P);
   P = strtok(NULL, ",");
   if( P != NULL) 
   {
    n = atoi(P);
    P = strtok(NULL, ",");
    if(P != NULL) m = atoi(P);
   }

  printf("xxxxxxxxxxxxxxxxxx %s   %d   %d\n",com, n, m);
 
  if(strcmp(com, "add") == 0)
  {
    printf("add\n");
    printf("link ==> %s\n", newLinkValue);
    printf("name ==> %s\n", newNameValue);

    
    File ln = SPIFFS.open("/linkS", "r+");
    ln.seek(0, SeekEnd);
    ln.write((const uint8_t*)newLinkValue, strlen(newLinkValue));
    ln.write((const uint8_t*)lf, 1);
    ln.close();
    ln = SPIFFS.open("/nameS", "r+");
    ln.seek(0,SeekEnd);
    ln.write((const uint8_t*)newNameValue, strlen(newNameValue));
    ln.write((const uint8_t*)lf, 1);
    ln.close();    
  }
  else
  {
 
    if(strcmp(com, "del") == 0)
    {
      File trn = SPIFFS.open("/trn", "w");
      File trl = SPIFFS.open("/trl", "w");
      for(int i=0;i<n;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
      for(int i=n+1;i<=MS;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
      SPIFFS.remove("/nameS");
      SPIFFS.remove("/linkS");
      SPIFFS.rename("/trn", "/nameS");
      SPIFFS.rename("/trl", "/linkS");
    }
    else if(strcmp(com, "mov") == 0)
    {
      File trn = SPIFFS.open("/trn", "w");
      File trl = SPIFFS.open("/trl", "w");
      if(n > m)
      {
      for(int i=0;i<m;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
        strcpy(li,Rlink(n));
        strcpy(na, Rname(n));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      for(int i=m;i<n;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
       for(int i=n+1;i<=MS;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
      }
      else
      {
        for(int i=0;i<n;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
       
      for(int i=n+1;i<m+1;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
      }
        strcpy(li,Rlink(n));
        strcpy(na, Rname(n));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1);
       for(int i=m+1;i<=MS;i++)
      {
        strcpy(li,Rlink(i));
        strcpy(na, Rname(i));
        trn.write((const uint8_t*)na, strlen(na));
        trn.write((const uint8_t*)lf, 1);
        trl.write((const uint8_t*)li, strlen(li));
        trl.write((const uint8_t*)lf, 1); 
      }
      }
      SPIFFS.remove("/nameS");
      SPIFFS.remove("/linkS");
      SPIFFS.rename("/trn", "/nameS");
      SPIFFS.rename("/trl", "/linkS");  
  }
  started = true;
}
}

//////////////////////////////////////////////////////////////////
// local
// detects non ASCII chars and converts them (if possible...)
/////////////////////////////////////////////////////////////////
void convToAscii(char *s, char *t)
{
  int j = 0;
  for(int i=0; i<strlen(s);i++)
  {
    if(s[i] < 128) t[j++] = s[i];
    else
    {
      if(s[i] == 0xC2)
      {
        t[j++] = '.';
        i++;
      }
      else if(s[i] == 0xC3)
      {
        i++;
        if((s[i] >= 0x80) && (s[i] < 0x87)) t[j++] = 'A';
        else if((s[i] >= 0x88) && (s[i] < 0x8C)) t[j++] = 'E';
        else if((s[i] >= 0x8C) && (s[i] < 0x90)) t[j++] = 'I'; 
        else if(s[i] == 0x91) t[j++] = 'N';
        else if((s[i] >= 0x92) && (s[i] < 0x97)) t[j++] = 'O'; 
        else if(s[i] == 0x97) t[j++] = 'x';
        else if(s[i] == 0x98) t[j++] = 'O';
        else if((s[i] >= 0x99) && (s[i] < 0x9D)) t[j++] = 'U'; 
        else if((s[i] >= 0xA0) && (s[i] < 0xA7)) t[j++] = 'a'; 
        else if((s[i] == 0xA7) ) t[j++] = 'c'; 
        else if((s[i] >= 0xA8) && (s[i] < 0xAC)) t[j++] = 'e'; 
        else if((s[i] >= 0xAC) && (s[i] < 0xB0)) t[j++] = 'i'; 
        else if(s[i] == 0xB1) t[j++] = 'n';
        else if((s[i] >= 0xB2) && (s[i] < 0xB7)) t[j++] = 'o'; 
        else if(s[i] == 0xB8) t[j++] = 'o';
        else if((s[i] >= 0xB9) && (s[i] < 0xBD)) t[j++] = 'u'; 
        else t[j++] = '.';
      }
    }
  }

  t[j] = 0;
}

///////////////////////////////////////////////////////////////////////
// task managing the speaker buttons
// 
//////////////////////////////////////////////////////////////////////
static void keyb(void* pdata)
{
static int v0, v1, v2;
static int ec0=0, ec1=0, ec2=0;
  while(1)
  {
    if((gpio_get_level(VP) == 1) && (ec0 == 1)){b0 = v0; ec0 = 0;}
    if((gpio_get_level(VP) == 1) && (b0 == -1)) {v0 = 0;ec0 = 0;}
    if(gpio_get_level(VP) == 0) {v0++; ec0 = 1;}
   
    if((gpio_get_level(VM) == 1) && (ec1 == 1)){b1 = v1; ec1 = 0;}
    if((gpio_get_level(VM) == 1) && (b1 == -1)) {v1 = 0;ec1 = 0;}
    if(gpio_get_level(VM) == 0) {v1++; ec1 = 1;}
   
    if((gpio_get_level(MU) == 1) && (ec2 == 1)){b2 = v2; ec2 = 0;}
    if((gpio_get_level(MU) == 1) && (b2 == -1)) {v2 = 0; ec2 = 0;}
    if(gpio_get_level(MU) == 0) {v2++; ec2 = 1;}
    
  //  printf("%d %d %d %d %d %d\n",b0,b1,b2,v0,v1,v2);
    delay(100);
  }
}

/////////////////////////////////////////////////////////////////////
// play station task (core 1)
//
/////////////////////////////////////////////////////////////////////
static void playRadio(void* data)
{
  while (started == false) delay(100);
  while(1)
  {

   //printf("st %d prev %d\n",station,previousStation);
    if((station != previousStation)||(connected == false))
   {     
      printf("station no %d\n",station);
      audio.stopSong();
      connected = false;
      //delay(100);
      linkS = Rlink(station);
      mes[0] = 0;
      audio.connecttohost(linkS);
      previousStation = station;
    }
  // delay(100);
  if(connected == false) delay(500);

   if(beepON == false)audio.loop();
  }
}


//////////////////////////////////////////////////////////////////////////
//
// plays .wav records (in SPIFFS file)
//////////////////////////////////////////////////////////////////////////
void playWav(char* n, uint8_t att)
{
  struct header
  {
    uint8_t a[16];
    uint8_t cksize[4];
    uint8_t wFormatTag[2];
    uint8_t nChannels[2];
    uint8_t nSamplesPerSec[4];
    uint8_t c[16];  
  };
   uint32_t rate;
   uint8_t b[46];
   int l;
   bool mono;
   size_t t;
   File f = SPIFFS.open(n, FILE_READ);
// mono/stereo
   l = (int) f.read(b, sizeof(b));
   if (b[22] == 1) mono = true; else mono = false;  
// sample rate => init I2S   
   rate =  (b[25] << 8) + b[24];
   printf(" rate = %d\n",rate);
   i2s_set_clk(I2SN, rate, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
//writes samples (16 bits) to codec via I2S   
   do
   {
    if(mono == true)
    {
       l = (int)f.read((uint8_t*)b, 2);
       b[2] = b[0]; b[3] = b[1];
       
    }
    else      
       l = (int)f.read((uint8_t*)b, 4);   
    if(att != 0)
    {
      union 
      {
        uint8_t b8[4];
        uint16_t b16[2];        
      }s;
      for(int i=0;i<4;i++)s.b8[i] = b[i];
      s.b16[0] = s.b16[0] >> att;
      s.b16[1] = s.b16[1] >> att;   
      for(int i=0;i<4;i++) b[i] = s.b8[i];   
    }
             
   i2s_write(I2SN, b, 4, &t,1000);
   }
   while(l != 0);
   i2s_zero_dma_buffer((i2s_port_t)0);
   f.close();
}


/////////////////////////////////////////////////////////////////////
// beep....
/////////////////////////////////////////////////////////////////////
void beep(void)
{
#if   beepOK == 1
#define attBeep 4
  beepON = true;
  playWav("/Beep.wav", attBeep);
  beepON = false;
  i2s_set_clk(I2SN, sampleRate, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
#endif  
}


/////////////////////////////////////////////////////////////////////////////
// gets station link from SPIFFS file "/linkS"
//
/////////////////////////////////////////////////////////////////////////////
char* Rlink(int st)
{
  int i;
  static char b[80];
  File ln = SPIFFS.open("/linkS", FILE_READ);
  i = 0;
  uint8_t c;
  while(i != st)
  {
    while(c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  }while(b[i-1] != 0x0a);
  b[i-1] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////////////
//  gets station name from SPIFFS file "/namS"
//
/////////////////////////////////////////////////////////////////////////////////
char* Rname(int st)
{
  int i;
  static char b[20];
  File ln = SPIFFS.open("/nameS", FILE_READ);
  i = 0;
  uint8_t c;
  while(i != st)
  {
    while(c != 0x0a)ln.read(&c, 1);
    c = 0;
    i++;
  }
  i = 0;
  do
  {
    ln.read((uint8_t*)&b[i], 1);
    i++;
  }while(b[i-1] != 0x0a);
  b[i-1] = 0;
  ln.close();
  return b;
}
/////////////////////////////////////////////////////////////////////////
//  defines how many stations in SPIFFS file "/linkS"
//
////////////////////////////////////////////////////////////////////////
int maxStation(void)
{
  File ln = SPIFFS.open("/linkS", FILE_READ);
  uint8_t c;
  int m = 0;
  int t;
  t = ln.size();
  int i = 0;
  do 
  {
    while(c != 0x0a){ln.read(&c, 1); i++;}
    c = 0;
    m++;
  }while(i < t);
  ln.close();
  return m;  
}

///////////////////////////////////////////////////////////////////////////////////
//
//  init. local server custom parameters
//
///////////////////////////////////////////////////////////////////////////////////

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter comParam = IotWebConfParameter("Action", "actionParam", comValue, 16, "action", "add, del or mov", NULL);
IotWebConfSeparator separator1 = IotWebConfSeparator();
IotWebConfParameter newNameParam = IotWebConfParameter("New Name", "nameParam", newNameValue, 16);
IotWebConfSeparator separator2 = IotWebConfSeparator();
IotWebConfParameter newLinkParam = IotWebConfParameter("New Link", "linkParam", newLinkValue, 80);

void setup() { 
Serial.begin(115200);

if(!SPIFFS.begin())Serial.println("Erreur SPIFFS");
// SPIFFS maintenance   
  
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file){
      Serial.print("FILE: ");
      Serial.println(file.name());
      file = root.openNextFile();
    }
    printf("====> %d\n",(int)SPIFFS.totalBytes());
    printf("====> %d\n",(int)SPIFFS.usedBytes());   
    //SPIFFS.format();
    
printf(" SPIFFS used bytes  ====> %d of %d\n",(int)SPIFFS.usedBytes(), (int)SPIFFS.totalBytes());      

////////////////////////////////////////////////////////////////
// init led handle
///////////////////////////////////////////////////////////////
  strip.Begin();  

////////////////////////////////////////////////////////////////
// init ADC interface for battery survey
/////////////////////////////////////////////////////////////////
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_GPIO33_CHANNEL, ADC_ATTEN_DB_11);
////////////////////////////////////////////////////////////////

  
// variables de travail
previousStation = -1;
station = 0;
MS = maxStation()-1;
printf("max ===> %d\n",MS);

/////////////////////////////////////////////////////////////
// recovers params (station & vol)
///////////////////////////////////////////////////////////////
        char b[4];
        File ln = SPIFFS.open("/station", "r");
        ln.read((uint8_t*)b, 2);
        b[2] = 0;
        station = atoi(b);
        ln.close();
        ln = SPIFFS.open("/volume", "r");
        ln.read((uint8_t*)b, 2);
        b[2] = 0;
        vol = atoi(b);
        ln.close();

///////////////////////////////////////////////////////   
// initi gpios
////////////////////////////////////////////////////////////
//gpio_reset_pin
        gpio_reset_pin(MU);
        gpio_reset_pin(VP);
        gpio_reset_pin(VM);
        gpio_reset_pin(CFG);
//       gpio_reset_pin(STOP);      
         
//gpio_set_direction
        gpio_set_direction(MU, GPIO_MODE_INPUT);  
        gpio_set_direction(VP, GPIO_MODE_INPUT);  
        gpio_set_direction(VM, GPIO_MODE_INPUT);  
        gpio_set_direction(CFG, GPIO_MODE_INPUT);           
//        gpio_set_direction(STOP, GPIO_MODE_INPUT);  

//gpio_set_pull_mode
        gpio_set_pull_mode(MU, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(VP, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(VM, GPIO_PULLUP_ONLY);
        gpio_set_pull_mode(CFG, GPIO_PULLUP_ONLY);
//        gpio_set_pull_mode(STOP, GPIO_PULLUP_ONLY);


// power enable
        gpio_reset_pin(PW);
        gpio_set_direction(PW, GPIO_MODE_OUTPUT);        

// init I2S   
   audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
   i2s_set_clk(I2SN, 44100, (i2s_bits_per_sample_t)16, (i2s_channel_t)2);
// power enable
   gpio_set_level(PW, 1);   
   printf("Volume = %d\n",vol);
   audio.setVolume(vol);
// init screen handler   
#if SCREEN == 1
   tinySsd_init(SDA, SCL, 0, 0x3C, 1);  
   clearBuffer();
   sendBuffer();
   drawBigStrC(24,"Ros&Co");
   sendBuffer();
#endif
   
/////////////////////////////////////////////////////////
//init WiFi  
//////////////////////////////////////////////////////////////
// init. local server main parameters
//////////////////////////////////////////////////////////////
  iotWebConf.addParameter(&comParam);
  iotWebConf.addParameter(&separator1);
  iotWebConf.addParameter(&newNameParam);
  iotWebConf.addParameter(&separator2);
  iotWebConf.addParameter(&newLinkParam);
//init custom parameters  management callbacks 
  iotWebConf.setConfigSavedCallback(&configRadio);  
  iotWebConf.setFormValidator(&formValidator);

// -- Initializing the configuration.
// pin for manual init  
  iotWebConf.setConfigPin(CFG);
  iotWebConf.init();
   // iotWebConf.setApTimeoutMs(30000);
// init web server
// -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", []{ iotWebConf.handleConfig(); });
  server.onNotFound([](){ iotWebConf.handleNotFound(); });

  xTaskCreate(playRadio, "radio", 5000 , NULL, 1, NULL);
//task managing the battery
  xTaskCreate(battery, "battery", 5000, NULL, 1, NULL);  
  xTaskCreate(keyb, "keyb", 5000, NULL, 5, NULL);
}

void loop() {
#define Press 9   
#define longPress  12
#define veryLongPress 30
//static int v0, v1, v2;
//static int ec0=0, ec1=0, ec2=0;
   iotWebConf.doLoop();
   if (WiFi.status() != WL_CONNECTED) return; 
   started = true;

   if(timeON == false)
   {
//////////////////////////////////////////////////////////////////
// initialisation temps NTP
//
////////////////////////////////////////////////////////////////////
// time zone init
    setenv("TZ", "CEST-1", 1);
    tzset();
//sntp init
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    int retry = 0;
    while((timeinfo.tm_year < (2016 - 1900)) && (++retry < 20))
    {
        delay(500);
        time(&now);
        localtime_r(&now, &timeinfo);
     }
     timeON = true;
   }
 
// Volume + (VP short) Volume - (VM short)
   oldVol = vol;
 // xSemaphoreTake(buttonsSem, portMAX_DELAY);
  if((b0 > 0) && (b0 < Press)) {vol++ ;b0 = -1;printf("P\n");}
  if((b1 > 0) && (b1 < Press)) {vol-- ;b1 = -1;printf("M\n");}
//  xSemaphoreGive(buttonsSem);
  if (vol > maxVol) vol = maxVol;
  if (vol < 0) vol = 0;
  //if (vol != oldVol) beep();

//changement de volume
   if(vol != oldVol)
   {
      beep();
      muteON = false;
      oldVol = vol;  
      printf("vol = %d\n",vol);
      audio.setVolume(vol);
      char b[4];
      sprintf(b,"%02d",vol);     
      File ln = SPIFFS.open("/volume", "w");
      ln.write((uint8_t*)b, 2);
      ln.close();     
   }

// station + (VP long)  station - (VM long) 
 // xSemaphoreTake(buttonsSem, portMAX_DELAY);
  if((b0 > 0) && (b0 > longPress)) {station++; b0 = -1;}
  if((b1 > 0) && (b1 > longPress)) {station--; b1 = -1;}
  if((b0 >= Press) && (b0 <= longPress))b0 = -1;
  if((b1 >= Press) && (b1 <= longPress))b1 = -1;
  //xSemaphoreGive(buttonsSem);
  
   if(station > MS) station = 0;
   if(station < 0) station = MS;
   if(station != previousStation)
   {
        beep();
        char b[4];
        sprintf(b,"%02d",station);
        File ln = SPIFFS.open("/station", "w");
        ln.write((uint8_t*)b, 2);
        ln.close();      
   }
   //xSemaphoreTake(buttonsSem, portMAX_DELAY);

// mute / unmute   (MU short)
   if((b2 > 0) && (b2 < longPress))
   {
    if(muteON == false)
    {
       audio.setVolume(0);
       printf("mute on\n");
       muteON = true;           
    }
    else
    {
       audio.setVolume(vol);
       printf("mute off\n");                                     
       muteON = false;
    }
    b2 = -1; 
   }
   
// stop (MU very long)   
  if((b2 > 0) && (b2 > veryLongPress))
    {
      b2 = -1;
      clearBuffer();
      sendBuffer();
      strip.SetPixelColor(0, BLACK);
      strip.Show();
      esp_sleep_enable_ext0_wakeup(STOP,LOW);     
      esp_deep_sleep_start();
    }
   //xSemaphoreGive(buttonsSem); 

   
#if SCREEN == 1
//////////////////////////////////////////////////////////   
// handling display 
/////////////////////////////////////////////////////////

// time
   time(&now);
   localtime_r(&now, &timeinfo);

clearBuffer();
  
//displays station name
  drawStrC(1,Rname(station));
  drawHLine(14, 0, 128);
  drawHLine(50, 0, 128);
 
//displays time (big chars) 
  sprintf(timeStr,"%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min); 
  drawBigStrC(24, timeStr);

  if (connected == false) 
  {
    drawStrC(14,"..........");
  }
 
// displays sound index (60x10, 10 values)
  drawIndexb(22, 119, 20, 8, 10, vol*10/maxVol);
  
//displays scrolling messages
 if(strlen(mes) != 0)
 {
  char mesa[17];
  strncpy(mesa, &mes[iMes],16);
  if(strlen(mesa) < 16) iMes = 0; else iMes++;
  mesa[16] = 0;
 
 drawStr(56, 0, mesa);
 }
   sendBuffer();
#endif
   delay(100);
}

///////////////////////////////////////////////////////////////////////
//  stuff for  web server intialization
//       wifi credentials and other things...
//
/////////////////////////////////////////////////////////////////////////
void handleRoot()
{
  char b[6];
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>Muse Radio</title></head><body>--- MUSE Radio --- Ros & Co ---";
  s += "<li>---- Stations ----";
  s += "<ul>";

  for(int i=0;i<=MS;i++)
  {
  s += "<li>";
  sprintf(b, "%02d  ",i);
  s += (String)b;
  s += (String) Rname(i);
  }
  s += "</ul>";
  s += "Go to <a href='config'>configure page</a> to change values (wifi credentials, stations names and links...)";
  s += "</body></html>\n";

  server.send(200, "text/html", s);

}
////////////////////////////////////////////////////////////////////////
// custom parameters verification
///////////////////////////////////////////////////////////////////
boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;
  if(server.arg(comParam.getId()).length() > 0)
  {
    String buf;
    String com;
    String name;
    String link;
    int n,m;
    buf = server.arg(comParam.getId());
    com = server.arg(comParam.getId()).substring(0,3);

    if((com != "add") && (com != "del") && (com != "mov")) 
    {
      comParam.errorMessage = "Action should be add, del or mov";
      valid = false;
      return valid;
    }
    if(com == "add")
    {
      name = server.arg(newNameParam.getId());
      link = server.arg(newLinkParam.getId());
      if((name.length() == 0) || (name.length() > 16))
      {
        newNameParam.errorMessage = "add needs a station name (16 chars max)";
        valid = false;
        return valid;
      }
      if(link.length() == 0)
      {
        newLinkParam.errorMessage = "add needs a valid link";
        valid = false;
        return valid;
      }
    }
    if(com == "del")
    {
      int l = buf.indexOf(',');
      if(l == -1)
      {
        comParam.errorMessage = "incorrect del... del,[station to delete] (ie del,5)";
        valid = false;
        return valid;
      }
      sscanf(&buf[l+1],"%d",&n);
      if((n < 0) || (n >= MS))
      {
        comParam.errorMessage = "incorrect station number";
        valid = false;
        return valid;
      }
      
    }
    if(com == "mov")
    {
      int l = buf.indexOf(',');
      int k = buf.lastIndexOf(',');
      if((l == -1)||(k == -1))
      {
        comParam.errorMessage = "incorrect mov... mov,[old position],[new position] (ie mov,5,7)";
        valid = false;
        return valid;
      }
      sscanf(&buf[l+1],"%d",&n);
      sscanf(&buf[k+1],"%d",&m);
      if((n < 0) || (n > MS)|| (m < 0) || (m > MS) || (m == n))
      {
        comParam.errorMessage = "incorrect station number";
        valid = false;
        return valid;
      }
      
    }  
  }
  return valid;
}

// optional
void audio_info(const char *info){
#define maxRetries 4
   // Serial.print("info        "); Serial.println(info);
    if(strstr(info, "SampleRate=") > 0) 
    {
    sscanf(info,"SampleRate=%d",&sampleRate);
    printf("==================>>>>>>>>>>%d\n", sampleRate);
    }
    connected = true;   
    if(strstr(info, "failed") > 0){connected = false; printf("failed\n");}
}
void audio_id3data(const char *info){  //id3 metadata
    //Serial.print("id3data     ");Serial.println(info);
}
void audio_eof_mp3(const char *info){  //end of file
    //Serial.print("eof_mp3     ");Serial.println(info);
}
void audio_showstation(const char *info){
    //Serial.print("station     ");Serial.println(info);
}
void audio_showstreaminfo(const char *info){
  //  Serial.print("streaminfo  ");Serial.println(info);
}
void audio_showstreamtitle(const char *info){
   Serial.print("streamtitle ");Serial.println(info);
   if(strlen(info) != 0) 
   {
   convToAscii((char*)info, mes);
   iMes = 0;
   }
   else mes[0] = 0;
}
void audio_bitrate(const char *info){
   // Serial.print("bitrate     ");Serial.println(info);
}
void audio_commercial(const char *info){  //duration in sec
   // Serial.print("commercial  ");Serial.println(info);
}
void audio_icyurl(const char *info){  //homepage
   // Serial.print("icyurl      ");Serial.println(info);
}
void audio_lasthost(const char *info){  //stream URL played
    //Serial.print("lasthost    ");Serial.println(info);
}
void audio_eof_speech(const char *info){
    //Serial.print("eof_speech  ");Serial.println(info);
}
