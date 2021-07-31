/*
 * See Also:
 * https://www.switch-science.com/catalog/6726/
 * https://drive.google.com/file/d/14Jg8LPF2fG9bfNT8qb6fgsKFYBCOuHhy/view
 * https://drive.google.com/drive/folders/18Z01WRK7GBJW6AJOPaPY9rLtQLJ46g5I
 */

/*----------------------------------------------
 *  ESP32-A3 Weather Client Sample
 *    10分間隔でセンサ値を計測してUDPで送る。その後周辺電源をOFFにしてDeepSleep動作し10分タイマーで起動
 *    #2020/08/24 - #2020/10/08
 *    #2020/11/12   Pressureの送信の1桁目の不具合
 *    Board Select -> ESP32 Dev Module
  -----------------------------------------------*/
#include <Wire.h>
#include "time.h"
#include "esp_deep_sleep.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "SparkFunBME280.h"

#define SENSORID     1       // Sensore ID (1-4)

#define ADIN        34      // IO34  Battery AD
#define PDIN        35      // IO35  Photo Sensor AD
#define LEDPIN      27      // IO27  LED
#define PWCPIN       4      // IO4   Power Control

#define DEEPTIME    600     // Deep Timer 10min (sec)
#define DELIMITCODE 0x0a    // Delimit Code

const char* udp_ssid = "WEATHERSERVERDEMO";
const char* udp_pass = "weather123456";

const char * to_udp_address = "192.168.42.1";    //送信先のIPアドレス (UDPサーバー側に合わせる 192.168.XXX.XXX)
const int to_udp_port = 50100;                   //送信相手のポート番号(支障ないポート番号 49152–65535)　

byte setupBME280();
byte getBME280();
void SubSendUDPData();

static byte       sensor_enable;
static float      fTemp;
static float      fHumidity;
static float      fPressure;
static  int       nBattery;
static  int       nLight;
static  unsigned long starttime;
static  unsigned long endtime;

WiFiUDP     udp;
BME280      BME280Sensor;

void setup() {
    pinMode(LEDPIN,OUTPUT);
    pinMode(PWCPIN,OUTPUT);
    digitalWrite(LEDPIN,LOW);
    pinMode(PDIN,INPUT);
    pinMode(ADIN,INPUT);
  //--
    starttime = millis();
    Serial.begin(115200);
    Serial.flush();
    delay(200);
    digitalWrite(PWCPIN,LOW);       // 周辺デバイスのPOWER ON
    digitalWrite(LEDPIN,HIGH);
    Wire.begin(21,22,10000);
    sensor_enable = setupBME280();
}


void loop() {
  if (sensor_enable>0){
    digitalWrite(LEDPIN,HIGH);
    getBME280();
    digitalWrite(LEDPIN,LOW);
  }
  digitalWrite(PWCPIN,HIGH);       // 周辺デバイスのPOWER OFF
// Deep Sleep
  endtime = millis();
//-- Deep Sleep Mode
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
  esp_deep_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
  esp_sleep_enable_timer_wakeup(DEEPTIME*1000000-1000*(endtime-starttime));
  esp_deep_sleep_start();
}


/*----------------------------
*   BME280のI2Cアドレスを調べる
----------------------------- */
byte setupBME280() {
byte  bRet=0;
  if (bRet==0){
    BME280Sensor.setI2CAddress(0x77);
    if (BME280Sensor.beginI2C()){
      bRet=1;
    }
  }
  if (bRet==0){
    BME280Sensor.setI2CAddress(0x76);
    if (BME280Sensor.beginI2C()) {
      bRet=1;
    }
  }
  return bRet;
}


byte  getBME280() {
byte    bRet=0;

  nBattery  = analogRead(ADIN);      // Battery AD
  nLight    = analogRead(PDIN);      // Light AD
  BME280Sensor.setMode(MODE_FORCED);
  while (!BME280Sensor.isMeasuring()) delay(1);  // 計測開始前ならば待ち
  while ( BME280Sensor.isMeasuring()) delay(1);  // 計測中ならば待ち
  fTemp = BME280Sensor.readTempC();
  fHumidity = BME280Sensor.readFloatHumidity();
  fPressure = BME280Sensor.readFloatPressure()/100.0;
  BME280Sensor.setMode(MODE_SLEEP);       // Sleep
/*
Serial.print( fTemp);
Serial.print("  ");
Serial.print( fHumidity);
Serial.print("  ");
Serial.println( fPressure);
Serial.print( nLight);
Serial.print("  ");
Serial.println( nBattery);
*/
//
  if (SubUDPStart()){
    SubSendUDPData();
    bRet=1;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return bRet;
}

//-------------------------
//  Wifi (UDP) Start
//
bool SubUDPStart()
{
byte  cnt;
bool  bRet=false;

//  WiFi.disconnect();
  WiFi.begin(udp_ssid, udp_pass);
  for (cnt=0; cnt<50; cnt++){        // ReTry Count
    if (WiFi.status() == WL_CONNECTED) {
      IPAddress myIP = WiFi.localIP();
      udp.begin(to_udp_port);
      bRet=true;
      break;
    }
    delay(100);
  }
  return bRet;
}



//-------
//  Send UDP Server
// #ITTTTHHHHPPPPPLLLLBBBB    TTTT+1000
//    I <-- デバイス識別(1-4)
//    TTTT <-- 温度 （0.1℃単位で1000をバイアスして常にプラスになるようにする）
//    HHHH <-- 湿度 （0.1%単位）
//    PPPPP<-- 気圧 （0.1hPa単位）
//    LLLL <-- 明るさ （フォトセンサのAD値）
//    BBBB <-- 電池電圧 （AD値）
void SubSendUDPData(){
int   val,val1,val2,val3,val4,val5;

    udp.beginPacket(to_udp_address, to_udp_port);
    udp.write('#');
    udp.write(SENSORID+'0');
  //
    val = (int)(fTemp*(float)10)+1000;
    val1 = val/1000;
    val  = val - 1000*val1;
    val2 = val/100;
    val  = val - 100*val2;
    val3 = val/10;
    val  = val - 10*val3;
    val4 = val;
    udp.write((byte)val1+'0');
    udp.write((byte)val2+'0');
    udp.write((byte)val3+'0');
    udp.write((byte)val4+'0');
  //
    val = (int)(fHumidity*(float)10);
    val1 = val/1000;
    val  = val - 1000*val1;
    val2 = val/100;
    val  = val - 100*val2;
    val3 = val/10;
    val  = val - 10*val3;
    val4 = val;
    udp.write((byte)val1+'0');
    udp.write((byte)val2+'0');
    udp.write((byte)val3+'0');
    udp.write((byte)val4+'0');
  //
    val = (int)(fPressure*(float)10);
    val1 = val/10000;
    val  = val - 10000*val1;
    val2 = val/1000;
    val  = val - 1000*val2;
    val3 = val/100;
    val  = val - 100*val3;
    val4 = val/10;
    val  = val - 10*val4;         // #2020/11/12修正
    val5 = val;
    udp.write((byte)val1+'0');
    udp.write((byte)val2+'0');
    udp.write((byte)val3+'0');
    udp.write((byte)val4+'0');
    udp.write((byte)val5+'0');
  //--
    val  = nLight+1;
    val1 = val/1000;
    val  = val - 1000*val1;
    val2 = val/100;
    val  = val - 100*val2;
    val3 = val/10;
    val  = val - 10*val3;
    val4 = val;
    udp.write((byte)val1+'0');
    udp.write((byte)val2+'0');
    udp.write((byte)val3+'0');
    udp.write((byte)val4+'0');
  //--
    val  = nBattery;
    val1 = val/1000;
    val  = val - 1000*val1;
    val2 = val/100;
    val  = val - 100*val2;
    val3 = val/10;
    val  = val - 10*val3;
    val4 = val;
    udp.write((byte)val1+'0');
    udp.write((byte)val2+'0');
    udp.write((byte)val3+'0');
    udp.write((byte)val4+'0');
  //--
    udp.write(DELIMITCODE);
  //--
    udp.endPacket();
    delay(500);         // Delayが必要
}

//--------- END  ------
