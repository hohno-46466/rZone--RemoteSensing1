/*
 * See Also:
 * https://www.switch-science.com/catalog/6726/
 * https://drive.google.com/file/d/14Jg8LPF2fG9bfNT8qb6fgsKFYBCOuHhy/view
 * https://drive.google.com/drive/folders/1Ib54mnCSxEleeLSwUtSPLilcFZ--zytd
 */

/*----------------------------------------------
 *  M5STACK Weather Server Sample
 *    #2020/08/13 - #2020/10/10
 *
 *    Board Select -> M5Stack-Core-ESP32
 *    writeing tool -> arduinoISP
  -----------------------------------------------*/
#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define VERSIONINFO     "Weather Server #2020/10/10"

//-- Mode Command (MXX)
#define COMMAND_VERSION  ('V'*256+'R')       // VR
#define COMMAND_RESET    ('R'*256+'S')       // RS
#define COMMAND_LOGREAD  ('L'*256+'G')       // LG
#define COMMAND_LOGSHIFT ('S'*256+'F')       // SFX       X=1-4day

//-- NTPで時刻情報を得るためのWifi情報（最大3台まで探す）
//    自分で使えるWifiのIDとPasswrdを書き換える
const char* wifi_ssid1 = "       ";
const char* wifi_pass1 = "       ";
const char* wifi_ssid2 = "       ";
const char* wifi_pass2 = "       ";
const char* wifi_ssid3 = "       ";
const char* wifi_pass3 = "       ";

const IPAddress my_ip(192,168,42,1);
const IPAddress my_subnet(255,255,255,0);
const char* udp_ssid = "WEATHERSERVERDEMO";
const char* udp_pass = "weather123456";
const int to_udp_port = 50100;            // 送信相手のポート番号
const int my_server_udp_port = 50100;     // 開放する自ポート(支障ないポート番号 49152–65535)

const char* ntpServer  =  "ntp.jst.mfeed.ad.jp";

#define LCDWIDTH    320
#define LCDHEIGHT   240
#define ILCDBRIGHT  64

#define DELIMITCODE 0x0a       // Delimit Code
#define MAXCOMBUFN  128
#define NPTTIME     (12*3600+8)

byte SubGetDataValue();
void setupWiFiUDPserver();
byte SubCheckButton();
void SubDisplayInfo();
void SubWakeupLCD();
bool SubWifiStart();
void SubWifiEnd();
bool SubGetNTPTime();

uint16_t getColor(uint8_t red, uint8_t green, uint8_t blue){
  return ((red>>3)<<11) | ((green>>2)<<5) | (blue>>3);
}

TaskHandle_t task_handl; //マルチタスクハンドル定義
WiFiUDP   udp;

struct WEATHERDATA {
  int  nowyear;
  byte nowmonth;
  byte nowday;
  byte nowhour;
  byte nowmin;
  byte nowsec;
  int  Temp;
  int  Humidity;
  int  Pressure;
  int  Light;
  int  Battery;
};

static unsigned long lasttime;
static unsigned long pretime;
static unsigned long datatime;            // Data Recieve Last Time
static unsigned long timepretime;
static  int       nptTomeCount;

static byte       bSerialEnable;
static byte       lcdsleep;
static byte       bUpdate;
static int        sleeptime;            // Sleep Time(sec)
//--
static  uint16_t  nowtime_year;
static  uint8_t   nowtime_month;
static  uint8_t   nowtime_day;
static  uint8_t   nowtime_week;
static  uint8_t   nowtime_hour;
static  uint8_t   nowtime_min;
static  uint8_t   nowtime_sec;
//--
static int        comptr;               // Command Buf Ptr
static byte       combuf[MAXCOMBUFN];   // Command Buffer

static WEATHERDATA  nowdata;

static IPAddress    myIP;

void setup() {
int    i,j;

//-- Command Buf Clear
  comptr=0;               // Command Buf Ptr
  for (i=0; i<MAXCOMBUFN; i++)  combuf[i]=0;
//--
  bUpdate    = 0;
  lcdsleep   = 0;
  sleeptime  = 60;            // Sleep Time(sec)
//--
  memset(&nowdata,0,sizeof(WEATHERDATA));
//--
  bSerialEnable = 0;
  for (i=0; i<50; i++){
    Serial.begin(115200);
    if (Serial){
      bSerialEnable = 1;
      break;
    }
    delay(20);
  }
  delay(200);
//--
  if (SubWifiStart()){
    SubGetNTPTime();
    delay(200);
    SubWifiEnd();
    nptTomeCount=0;
    delay(200);
  }
//--
  xTaskCreatePinnedToCore(&taskDisplay, "taskDisplay", 2*8192, NULL, 10, &task_handl, 0);
  setupWiFiUDPserver();
  delay(1000); //
  SubDisplayInfo();
  lasttime  = millis()/1000;
  pretime   = lasttime;
  datatime  = lasttime;            // Data Recieve Last Time
}


void loop() {
byte    c,id;
int     i,packetSize;

    packetSize = udp.parsePacket();
    if(packetSize > 0){
      IPAddress remote = udp.remoteIP();
      if (bSerialEnable){
        Serial.print("Recieve UDP iP = ");
        Serial.println(remote);
      }
    //--
      for (i=0; i<packetSize; i++){
        c = udp.read();
        if (c==DELIMITCODE){       // Delimit
          combuf[comptr] = 0;
          c = combuf[0];        // # Check
          switch(c){
            case '#':
              id = combuf[1]-'0';         // 1のみ
              if (SubGetDataValue()>0) bUpdate = id;
              break;
          }
          comptr=0;
        }else{
          if (comptr<MAXCOMBUFN){
            combuf[comptr] = c;       // Command Buffer
            comptr++;
          }else{
            comptr=0;         // Error
          }
        }
      }
    }
}


//******** core 0 task *************
void taskDisplay(void *pvParameters){
byte    sw,row,clm;
float   fVal;
int     i,val,val1,id;
unsigned long nowsec;

  M5.begin();
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setBrightness(ILCDBRIGHT);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.print(VERSIONINFO);
  M5.Lcd.setCursor(0, LCDHEIGHT-16);
  M5.Lcd.print("MyIP ");
  M5.Lcd.println(myIP);
//--
  delay(200);
//--
  while(true){
    nowsec   = millis()/1000;
    if (nowsec>pretime){
      pretime = nowsec;
      nowtime_sec++;
      nptTomeCount++;
      if (nptTomeCount>=NPTTIME){
        nptTomeCount=0;
        if (bSerialEnable) Serial.println("NPT Time Get !");
        SubWifiEnd();
        delay(500);
        if (SubWifiStart()){
          SubGetNTPTime();
          delay(200);
          SubWifiEnd();
        }
      //--
        delay(500);
        setupWiFiUDPserver();
        SubDisplayInfo();
      }
    //--
      if (nowtime_sec>=60){
        nowtime_sec = 0;
        nowtime_min++;
        if (nowtime_min>=60){
          nowtime_min = 0;
          nowtime_hour++;
          if (nowtime_hour>=24){
            nowtime_hour=0;
            nowtime_day++;
          }
        }
      }
    }
  //--
    if (bUpdate>0){
      sleeptime = 60;            // Sleep Time(sec)
      if (lcdsleep>0) SubWakeupLCD();
      SubDisplayInfo();
      bUpdate = 0;
    }
  //--
    M5.update(); //Update M5Stack button state
    sw = SubCheckButton();
    if (sw>0){
      if (lcdsleep>0){
        sleeptime  = 120;            // Sleep Time(sec)
        SubWakeupLCD();
      }
      SubDisplayInfo();
      lasttime = nowsec;
    }
  //--
    if (lcdsleep==0){
      if ((nowsec-lasttime)>=sleeptime){
        lcdsleep = 1;
        M5.Lcd.sleep();
        M5.Lcd.setBrightness(0);
      }
    }
    delay(10);
  }
}


// 0123456789012346789
// #ITTTTHHHHPPPPPBBBB    TTTT+1000
byte  SubGetDataValue(){
byte    c;
byte    bRet=0;
int     temp,Humidity,pressure,Light,Battery;

  temp     = 0;
  Humidity = 0;
  pressure = 0;
  Light    = 0;
  Battery  = 0;
//--
  c  = 2;
  temp = temp + 1000*(combuf[c++]-'0');
  temp = temp +  100*(combuf[c++]-'0');
  temp = temp +   10*(combuf[c++]-'0');
  temp = temp +    1*(combuf[c++]-'0');
//--
  Humidity = Humidity + 1000*(combuf[c++]-'0');
  Humidity = Humidity +  100*(combuf[c++]-'0');
  Humidity = Humidity +   10*(combuf[c++]-'0');
  Humidity = Humidity +    1*(combuf[c++]-'0');
 //--
  pressure = pressure +10000*(combuf[c++]-'0');
  pressure = pressure + 1000*(combuf[c++]-'0');
  pressure = pressure +  100*(combuf[c++]-'0');
  pressure = pressure +   10*(combuf[c++]-'0');
  pressure = pressure +    1*(combuf[c++]-'0');
//--
  Light    = Light + 1000*(combuf[c++]-'0');
  Light    = Light +  100*(combuf[c++]-'0');
  Light    = Light +   10*(combuf[c++]-'0');
  Light    = Light +    1*(combuf[c++]-'0');
//--
  Battery  = Battery + 1000*(combuf[c++]-'0');
  Battery  = Battery +  100*(combuf[c++]-'0');
  Battery  = Battery +   10*(combuf[c++]-'0');
  Battery  = Battery +    1*(combuf[c++]-'0');
//--
  nowdata.nowyear  = nowtime_year;
  nowdata.nowmonth = nowtime_month;
  nowdata.nowday   = nowtime_day;
  nowdata.nowhour  = nowtime_hour;
  nowdata.nowmin   = nowtime_min;
  nowdata.nowsec   = nowtime_sec;
  nowdata.Temp     = temp;
  nowdata.Humidity = Humidity;
  nowdata.Pressure = pressure;
  nowdata.Light    = Light;
  nowdata.Battery  = Battery;
  bRet=1;
  return bRet;
}


//--------------------
//
void SubDisplayInfo(){

  M5.Lcd.fillRect(0, 16, LCDWIDTH, LCDHEIGHT-32, BLACK);
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 16);
  M5.Lcd.print(nowdata.nowyear);
  M5.Lcd.print("/");
  if (nowdata.nowmonth<10) M5.Lcd.print("0");
  M5.Lcd.print(nowdata.nowmonth);
  M5.Lcd.print("/");
  if (nowdata.nowday<10) M5.Lcd.print("0");
  M5.Lcd.print(nowdata.nowday);
  M5.Lcd.print(" ");
//--
  if (nowdata.nowhour<10) M5.Lcd.print("0");
  M5.Lcd.print(nowdata.nowhour);
  M5.Lcd.print(":");
  if (nowdata.nowmin<10) M5.Lcd.print("0");
  M5.Lcd.print(nowdata.nowmin);
  M5.Lcd.print(":");
  if (nowdata.nowsec<10) M5.Lcd.print("0");
  M5.Lcd.print(nowdata.nowsec);
//--
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(0, 36+34*0);
  M5.Lcd.print("Temp");
  M5.Lcd.setCursor(0, 36+34*1);
  M5.Lcd.print("Humi");
  M5.Lcd.setCursor(0, 36+34*2);
  M5.Lcd.print("Pres");
  M5.Lcd.setCursor(0, 36+34*3);
  M5.Lcd.print("Ligt.");
  M5.Lcd.setCursor(0, 36+34*4);
  M5.Lcd.print("Bat.");
  //--
  M5.Lcd.setCursor(120, 36+34*0);
  M5.Lcd.print((float)(nowdata.Temp-1000)/(float)10,1);
  M5.Lcd.setCursor(120, 36+34*1);
  M5.Lcd.print((float)nowdata.Humidity/(float)10,1);
  M5.Lcd.setCursor(120, 36+34*2);
  M5.Lcd.print(nowdata.Pressure/10);
  M5.Lcd.setCursor(120, 36+34*3);
  M5.Lcd.print(nowdata.Light);
  M5.Lcd.setCursor(120, 36+34*4);
  M5.Lcd.print(nowdata.Battery);
}


byte SubCheckButton(){
  if (M5.BtnA.wasReleased()) return 1;
  if (M5.BtnB.wasReleased()) return 2;
  if (M5.BtnC.wasReleased()) return 3;
  return 0;
}


void SubWakeupLCD(){
  M5.Lcd.wakeup();
  M5.Lcd.setBrightness(ILCDBRIGHT);
  lasttime = millis()/1000;
  lcdsleep = 0;
}


void setupWiFiUDPserver(){
  WiFi.disconnect(true, true); //WiFi config設定リセット
  WiFi.softAP(udp_ssid, udp_pass);
  delay(100);
/*Wifi.softAPConfig(IPAddress(<ローカルIP>),IPAddress(<ゲートウェイ>),IPAddress(<サブネット>)); */
  WiFi.softAPConfig(my_ip,my_ip,my_subnet);       // #2020/08/18
  myIP = WiFi.softAPIP();
  if (bSerialEnable) Serial.println("WiFi(UDP) connected!");
  if (bSerialEnable) Serial.print("My IP address: ");
  if (bSerialEnable) Serial.println(myIP);
  udp.begin(my_server_udp_port);
  delay(1000);
}

//-------------------------
//  Wifi Check Start
//
bool SubWifiStart()
{
byte  cnt;
bool  bRet=false;

  WiFi.mode(WIFI_STA);
//-- SSID1
  if (!bRet){
    if (wifi_ssid1[0]!=' '){
      WiFi.begin(wifi_ssid1, wifi_pass1);
      for (cnt=0; cnt<10; cnt++){        // ReTry Count
        if (bSerialEnable) Serial.println("Wifi_ssid1 ... ");
        if (WiFi.status() == WL_CONNECTED) {
          if (bSerialEnable) Serial.println("Wifi_ssid1 OK! ");
          bRet=true;
          break;
        }
        delay(400);
      }
    }
  }
  if (!bRet){
    if (wifi_ssid2[0]!=' '){
      WiFi.begin(wifi_ssid2, wifi_pass2);
      for (cnt=0; cnt<10; cnt++){        // ReTry Count
        if (bSerialEnable) Serial.println("Wifi_ssid2 ... ");
        if (WiFi.status() == WL_CONNECTED) {
          if (bSerialEnable) Serial.println("Wifi_ssid2 OK! ");
          bRet=true;
          break;
        }
        delay(400);
      }
    }
  }
  if (!bRet){
    if (wifi_ssid3[0]!=' '){
      WiFi.begin(wifi_ssid3, wifi_pass3);
      for (cnt=0; cnt<10; cnt++){        // ReTry Count
        if (bSerialEnable) Serial.println("Wifi_ssid3 ... ");
        if (WiFi.status() == WL_CONNECTED) {
          if (bSerialEnable) Serial.println("Wifi_ssid3 OK! ");
          bRet=true;
          break;
        }
        delay(400);
      }
    }
  }
//--
  if (bRet){
    IPAddress myIP = WiFi.localIP();
    if (bSerialEnable) Serial.print("My IP1 address: ");
    if (bSerialEnable) Serial.println(myIP);
  }
  return bRet;
}


//-------------------------
//  Wifi Check End
//
void SubWifiEnd()
{
  WiFi.disconnect();    //WiFi config設定リセット
  WiFi.mode(WIFI_OFF);
}


//-------
//  Get NTP Time Information
bool  SubGetNTPTime(){
struct tm timeInfo;
bool  bRet=false;

  // Set ntp time to local
  configTime(9 * 3600, 0, ntpServer);
  if (getLocalTime(&timeInfo)) {
  // Set RTC time
    nowtime_year  = timeInfo.tm_year + 1900;
    nowtime_month = timeInfo.tm_mon + 1;
    nowtime_day   = timeInfo.tm_mday;
    nowtime_hour  = timeInfo.tm_hour;
    nowtime_min   = timeInfo.tm_min;
    nowtime_sec   = timeInfo.tm_sec;
  //--
    nowdata.nowyear  = nowtime_year;
    nowdata.nowmonth = nowtime_month;
    nowdata.nowday   = nowtime_day;
    nowdata.nowhour  = nowtime_hour;
    nowdata.nowmin   = nowtime_min;
    nowdata.nowsec   = nowtime_sec;
  //--
    bRet=true;
    if (bSerialEnable){
      Serial.print("NTP Time: ");
      Serial.print(nowtime_year);
      Serial.print("/");
      Serial.print(nowtime_month);
      Serial.print("/");
      Serial.print(nowtime_day);
      Serial.print(",");
      Serial.print(nowtime_hour);
      Serial.print(":");
      Serial.print(nowtime_min);
      Serial.print(":");
      Serial.println(nowtime_sec);
    }
  }
  return bRet;
}


//--  END  --//
