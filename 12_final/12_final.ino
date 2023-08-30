#include <WiFi.h>
#include "time.h"
#include "ESP32Servo.h"
#include <WiFiClient.h>
#include <PubSubClient.h>           // 請先安裝PubSubClient程式庫 for MQTT
#include <TridentTD_LineNotify.h>
#define LINE_TOKEN "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#include <HTTPClient.h>  // thingspeak 使用

//請修改為你自己的API Key，並將https改為http
String url = "http://api.thingspeak.com/update?api_key=xxxxxxxxxxxxxxx";

String check_time = "14:20:0";    // 設定每日開啟次數歸零時間 "時:分:秒" 例如︰ "15:8:0"  
int open_times = 5;    // 設定每日可開啟次數
int open_sec = 5;      // 設定每次開啟秒數
float trigger_distance = 5;     // 設定觸發距離公分
const char* ssid     = "tsvtc-w(Ruckus)";
const char* password = "1q2w#E$R";

const char* ntpServer = "time.stdtime.gov.tw";  // 中華電信 time server
const long  gmtOffset_sec = 8*60*60;    // 台北時間 GMT+8 = 8*60*60 = 28800
const int   daylightOffset_sec = 0;     // 台北沒有使用日光節約時間
int Trig =12;    //發出聲波腳位
int Echo =14;    //接收聲波腳位
float CMValue = 0;
Servo myservo;  // 建立一個 servo 物件，最多可建立 12個 servo
int pos = 0;    // 設定 Servo 位置的變數
String now_time = "";
int count = 0;
bool open_servo = false;
unsigned long open_time_stamp;
bool count_full = false;
String s_msg = "";


// ------ 以下修改成你MQTT設定 ------
char* MQTTServer = "mqtt.eclipseprojects.io";//免註冊MQTT伺服器
int MQTTPort = 1883;//MQTT Port
char* MQTTUser = "";//不須帳密
char* MQTTPassword = "";//不須帳密

//推播主題1:推播訊息(記得改Topic)
char* MQTTPubTopic1 = "Ken_WDA_Taisan_msg";

//訂閱主題1:接收命令(記得改Topic)
char* MQTTSubTopic1 = "Ken_WDA_Taisan_cmd";

long MQTTLastPublishTime;//此變數用來記錄推播時間
long MQTTPublishInterval = 10000;//每10秒推撥一次
WiFiClient WifiClient;
PubSubClient MQTTClient(WifiClient);



//開始WiFi連線
void WifiConnect_loop() {
  //開始WiFi連線
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi連線成功");
  Serial.print("IP Address:");
  Serial.println(WiFi.localIP());
}

void get_LocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  now_time = String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
}

void get_distance() {
  digitalWrite(Trig, LOW); //先關閉
  delayMicroseconds(5);
  digitalWrite(Trig, HIGH);//啟動超音波
  delayMicroseconds(10);
  digitalWrite(Trig, LOW); //關閉
  float EchoTime = pulseIn(Echo, HIGH); //計算傳回時間
  CMValue = EchoTime / 29.4 / 2; //將時間轉換成距離
}

void open_servo_proc() {
  myservo.write(180);
  open_servo = true;
  open_time_stamp = millis();
  s_msg = String(now_time) + " 開啟";
  send_s_msg();
  thingspeak_send();  
}

void close_servo_proc() {
  myservo.write(0);
  open_servo = false;
  s_msg = String(now_time) + " 關閉";
  send_s_msg();
}


//開始MQTT連線
void MQTTConnect_loop() {
  MQTTClient.setServer(MQTTServer, MQTTPort);
  MQTTClient.setCallback(MQTTCallback);
  while (!MQTTClient.connected()) {
    //以亂數為ClietID
    String MQTTClientid = "esp32-" + String(random(1000000, 9999999));
    if (MQTTClient.connect(MQTTClientid.c_str(), MQTTUser, MQTTPassword)) {
      //連結成功，顯示「已連線」。
      Serial.println("MQTT已連線");
      //訂閱SubTopic1主題
      MQTTClient.subscribe(MQTTSubTopic1);
      } 
    else {
      //若連線不成功，則顯示錯誤訊息，並重新連線
      Serial.print("MQTT連線失敗,狀態碼=");
      Serial.println(MQTTClient.state());
      Serial.println("五秒後重新連線");
      delay(5000);
    }
  }
}

//接收到訂閱時
void MQTTCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print(topic); Serial.print("訂閱通知:");
  String payloadString;//將接收的payload轉成字串
  //顯示訂閱內容
  for (int i = 0; i < length; i++) {
  payloadString = payloadString + (char)payload[i];
  }
  Serial.println(payloadString);
  //比對主題是否為訂閱主題1
  if (strcmp(topic, MQTTSubTopic1) == 0) {
  Serial.println("訂閱訊息：" + payloadString);
    if (payloadString == "on") {
      open_servo_proc();
    }
    if (payloadString == "off") {
      close_servo_proc();
    }
    if (payloadString == "reset") {
      count = 0;
      s_msg = String(now_time) + " 使用 MQTT 設定開啟次數歸零";
      send_s_msg();
      thingspeak_send();
    }
  }
}

void thingspeak_send() {
  //開始傳送到thingspeak
  //Serial.println("啟動網頁連線");
  HTTPClient http;
  //將溫度及濕度以http get參數方式補入網址後方
  String url1 = url + "&field1=" + (int)count;
  //http client取得網頁內容
  http.begin(url1);
  Serial.println(url1);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    //讀取網頁內容到payload
    String payload = http.getString();
    //將內容顯示出來
    //Serial.print("網頁內容=");
    //Serial.println(payload);
  }
  else {
    //讀取失敗
    Serial.println("網路傳送失敗");
  }
  http.end();
}

void send_s_msg() {
  Serial.println(s_msg);
  Serial.println("");
  LINE.notify("\n" + s_msg);
  MQTTClient.publish(MQTTPubTopic1, s_msg.c_str());
}


void setup() {
  Serial.begin(115200);
  pinMode(Trig, OUTPUT);
  pinMode(Echo, INPUT);
  myservo.attach(15);
  myservo.write(0);

  //開始WiFi連線
  WifiConnect_loop();

  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // MQTT連線
	MQTTConnect_loop();

  // LINE
  Serial.println(LINE.getVersion());
  LINE.setToken(LINE_TOKEN);
  Serial.println("");

  get_LocalTime();
  s_msg = String(now_time) + " 開機" ;
  send_s_msg();
  thingspeak_send();
}

void loop() {
  while (true) {    
    if (WiFi.status() != WL_CONNECTED) { WifiConnect_loop(); }  //如果WiFi連線中斷，則重啟WiFi連線    
    if (!MQTTClient.connected()) { MQTTConnect_loop(); }  //如果MQTT連線中斷，則重啟MQTT連線
    MQTTClient.loop();  //更新訂閱狀態

    delay(1000);  // 放慢腳步

    // 只要開啟就檢查 open_sec 秒數關閉
    if (open_servo == true) {
      unsigned long check_time = millis() - open_time_stamp;
      Serial.println("開啟秒數: " + String(int(check_time/1000)));
      if (check_time > (open_sec*1000)) {
        close_servo_proc();
      }
    }

    get_LocalTime();

    if (check_time == now_time) {
      count = 0;
      count_full = false;
      s_msg = String(check_time) + " 時間到，開啟次數歸零";
      send_s_msg();
      thingspeak_send();
      delay(1000);   
    }

    if (count >= open_times) {
      if (count_full == false) {
        s_msg = String(now_time) + " 今天已開啟超過 " + String(open_times) + " 次，等待 " + String(check_time) + " 歸零";
        send_s_msg();
        count_full = true;
      }
      continue;
    }

    get_distance();

    Serial.println("開啟次數: " + String(count));
    Serial.println("歸零時間: " + String(check_time));
    Serial.println("現在時間: " + String(now_time));
    Serial.println("偵測距離: " + String(CMValue));
    
    if (open_servo == 1) {
      Serial.println("開啟中");
    }
    else {
      Serial.println("關閉中");
    }

    if (CMValue < trigger_distance) {
      if (open_servo == false) {
        count += 1;
        open_servo_proc();        
      }
    }

    Serial.println("");    
  }
}
