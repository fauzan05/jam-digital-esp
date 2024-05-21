// webserver
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
// rtc
#include <Wire.h>
#include <RtcDS3231.h>
#include "I2C_ClearBus.h"
// Filesystem
#include <FS.h>
#include <LittleFS.h>
// NeoPixel
#include <Adafruit_NeoPixel.h>
// jadwal sholat
#include "PrayerTimes.h"

// global misc
struct JAM_MENIT {
  int jam, menit;
};
bool rebootESP = false;

// RTC
RtcDS3231<TwoWire> Rtc(Wire);
I2C_ClearBus i2c_clear;

// NeoPixel
const byte
  led_pin     = 2,
  led_perLine = 2;
const int 
  led_total   = 58;
Adafruit_NeoPixel pixels(led_total, led_pin, NEO_GRB + NEO_KHZ800);

// led setup
byte
  led_kecerahan = 100,
  led_kecerahanLast = led_kecerahan,
  led_warnaSolid = 0;
bool refresh_display = false;
byte refresh_display_counter;

// led dot
bool dot_state;
unsigned long mp_dot;
const int dot_interval = 1000;

// led rainbow
bool led_rainbowMode = true;
byte led_rainbowColorVal;
unsigned long mp_rainbow;
const int rainbow_interval = 20;

const uint32_t list_solidColor[8] = {
  pixels.Color(255,  0,  0),     // merah
  pixels.Color(255,255,  0),     // kuning
  pixels.Color(0  ,255,  0),     // hijau
  pixels.Color(0  ,  0,255),     // biru tua
  pixels.Color(255, 20,147),     // pink
  pixels.Color(255, 65,  0),     // orange
  pixels.Color(0  ,255,255),     // cyan / biru muda
  pixels.Color(255,255,255),     // putih
};

//const bool nomor_digit[10][7] = {
//  {1,1,1,1,1,1,0}, // 0
//  {0,1,1,0,0,0,0}, // 1
//  {1,1,0,1,1,0,1}, // 2
//  {1,1,1,1,0,0,1}, // 3
//  {0,1,1,0,0,1,1}, // 4
//  {1,0,1,1,0,1,1}, // 5
//  {1,0,1,1,1,1,1}, // 6
//  {1,1,1,0,0,0,0}, // 7
//  {1,1,1,1,1,1,1}, // 8
//  {1,1,1,1,0,1,1}, // 9
//};
const bool nomor_digit[10][7] = {
  {1,1,1,0,1,1,1}, // 0
  {1,0,0,0,1,0,0}, // 1
  {0,1,1,1,1,1,0}, // 2
  {1,1,0,1,1,1,0}, // 3
  {1,0,0,1,1,0,1}, // 4
  {1,1,0,1,0,1,1}, // 5
  {1,1,1,1,0,1,1}, // 6
  {1,0,0,0,1,1,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,0,1,1,1,1}, // 9
};

uint32_t Wheel(byte WheelPos);
void pixels_animationStart();
bool can_show_pixels();

void pixels_show();
void display_digit(byte segment_pos, byte num);
void dot();
void display_jam();

// Jadwal Sholat & alarm
double times[sizeof(TimeName) / sizeof(char*)];
JAM_MENIT waktu_sholat[6];
byte ihti = 2, ims = 8;

byte GMT = 7;
float c_latitude=-7.62846;
float c_longitude=109.65262;
bool refresh_jadwal_sholat = false;
// Alarm
bool alarm_toggle = false;
JAM_MENIT alarm;

void baca_jadwal_sholat();
void print_jadwal_sholat();
void cek_waktu_sholat();
void cek_waktu_alarm();

// Buzzer
const byte buzzer_pin = 13;
bool buzzer_state = false;

// Jumlah buzzer alarm akan berbunyi
// waktu yang dihabiskan = interval * 2
byte buzzerAlarm_counter = 0;
const byte buzzerAlarm_maxCounter = 30; 
unsigned long mp_buzzerAlarm;
const int buzzerAlarm_interval = 250;
bool buzzerAlarm_buzzing = false;

// jumlah buzzer sholat akan berbunyi
byte buzzerSholat_counter = 0;
const byte buzzerSholat_maxCounter = 3; 
unsigned long mp_buzzerSholat;
const int buzzerSholat_interval = 1000;
bool buzzerSholat_buzzing = false;

void buzzerAlarm_process();
void buzzerSholat_process();

// Filesystem
const char* file_settings = "/json/settings.json";
bool saving_settings = false;

void load_settings();
void save_settings();
void save_wifi();

// webserver
DNSServer dnsServer;
AsyncWebServer server(80);
const char* ssid = "ledstrip_jws";
char pass_wifi[33] = "12345678";

class CaptiveRequestHandler : public AsyncWebHandler {
  public:
    CaptiveRequestHandler (){}
    virtual ~CaptiveRequestHandler(){}

    bool canHandle (AsyncWebServerRequest *request){
      //request->addInterestingHeader("ANY");
      return true;
    }

    void handleRequest (AsyncWebServerRequest *request){
      String RedirectUrl = "http://";
      if (ON_STA_FILTER(request)) {
        RedirectUrl += WiFi.localIP().toString();
      } else {
        RedirectUrl += WiFi.softAPIP().toString();
      }
      request->redirect(RedirectUrl);
    }
};

void handleGetWaktu(AsyncWebServerRequest *request);
void handleGetSettings(AsyncWebServerRequest *request);
void handleGetJadwal(AsyncWebServerRequest *request);
void handleRTCSettings(AsyncWebServerRequest *request);
void handleLEDSettings(AsyncWebServerRequest *request);
void handleAlarmSettings(AsyncWebServerRequest *request);
void handleAlarmOff(AsyncWebServerRequest *request);

void setup() {
  Serial.begin(115200);
  
  int rtn = i2c_clear.clearBus();
  if (rtn == 0) { Wire.begin(); }

  Rtc.Begin();
  RtcDateTime compiled = RtcDateTime(__DATE__,__TIME__);
  if(!Rtc.IsDateTimeValid()){
    if(Rtc.LastError() != 0){
      Serial.print("RTC communications error = ");
      Serial.println(Rtc.LastError());
    }else{
      Serial.println("RTC lost confidence in the DateTime!");
      Rtc.SetDateTime(compiled);
    }
  }
  if(!Rtc.GetIsRunning()) Rtc.SetIsRunning(true);
  RtcDateTime now = Rtc.GetDateTime();
  if(now < compiled) Rtc.SetDateTime(compiled);
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);

  if(!LittleFS.begin()) {
    Serial.println("An error while mounting FS Module");
  }
  load_settings();
  baca_jadwal_sholat();
  print_jadwal_sholat();

  pinMode(buzzer_pin, OUTPUT);
  pixels.begin();
  pixels_animationStart();
  display_jam();

  WiFi.softAP(ssid,pass_wifi);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/api/waktu", HTTP_GET, handleGetWaktu);
  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/jadwal", HTTP_GET, handleGetJadwal);
  server.on("/api/settings/rtc", HTTP_POST, handleRTCSettings);
  server.on("/api/settings/led", HTTP_POST, handleLEDSettings);
  server.on("/api/settings/alarm", HTTP_POST, handleAlarmSettings);
  server.on("/api/matikan_alarm", HTTP_GET, handleAlarmOff);
  server.serveStatic("/", LittleFS, "/www/")
    .setCacheControl("max-age=300")
    .setDefaultFile("index.html");
  server.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);

  server.begin();
}

void loop() {
  if (rebootESP) {
    rebootESP = false;
    WiFi.softAPdisconnect(true);
    delay(100);
    ESP.restart();
  }

  dnsServer.processNextRequest();
  cek_waktu_sholat();
  cek_waktu_alarm();
  buzzerAlarm_process();
  buzzerSholat_process();

  if (led_kecerahanLast != led_kecerahan) {
    led_kecerahanLast = led_kecerahan;
    pixels.setBrightness(led_kecerahan);
  }

  if (can_show_pixels()) {
    refresh_display = false;
    display_jam();
  } else {
    dot();
  }

  if (saving_settings)
    save_settings();
  if (refresh_jadwal_sholat)
    baca_jadwal_sholat();
}

// NeoPixel
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85)
  {
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170)
  {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void pixels_animationStart() {
  pixels.setBrightness(100);
  pixels.clear();
  for (int i=0; i<led_total; i++) {
    if (led_rainbowMode) {
      pixels.setPixelColor(i, Wheel((i+led_rainbowColorVal) &255));
    } else {
      pixels.setPixelColor(i, list_solidColor[led_warnaSolid]);
    }
    pixels_show();
    delay(20);
  }
  for (int i=0; i<led_total; i++) {
    pixels.setPixelColor(i, pixels.Color(0,0,0));
    pixels.show();
    delay(20);
  }
  pixels.setBrightness(led_kecerahan);
}

void display_digit(byte segment_pos, byte num) {
  //pos start dari 0 seperti array
  byte start_pos = segment_pos * led_perLine * 7;
  if (segment_pos > 1) start_pos += 2; //taruh posisi setelah dot

  for (byte i=0; i<7; i++) {
    if (nomor_digit[num][i] == 0) continue;

    for (byte j=0; j<led_perLine; j++) {
      const byte pos = i * led_perLine + start_pos + j;

      if (led_rainbowMode) {
        pixels.setPixelColor(pos, Wheel((pos+led_rainbowColorVal) &255));
      } else {
        pixels.setPixelColor(pos, list_solidColor[led_warnaSolid]);
      }
      yield();
    }
    yield();
  }
}
bool can_show_pixels() {
  bool result = false;
  if (WiFi.softAPgetStationNum() == 0)
    result = true;
  else if (refresh_display)
    result = true;

  if (buzzerAlarm_buzzing || buzzerSholat_buzzing)
    result = false;
  return result;
}

void dot() {
  if ((millis() - mp_dot) >= dot_interval) {
    mp_dot = millis();
    dot_state = !dot_state;

    refresh_display_counter++;
    if (refresh_display_counter == 60) {
      refresh_display_counter = 0;
      refresh_display = true;
    }

    pixels.show();
  }

  const byte pos1 = led_total / 2 - 1;
  const byte pos2 = pos1 + 1;

  if (dot_state) {
    if(led_rainbowMode)
    {
      pixels.setPixelColor(pos1, Wheel((pos1+led_rainbowColorVal) &255));
      pixels.setPixelColor(pos2, Wheel((pos2+led_rainbowColorVal) &255));
    }
    else
    {
      pixels.setPixelColor(pos1, list_solidColor[led_warnaSolid]);
      pixels.setPixelColor(pos2, list_solidColor[led_warnaSolid]);
    }
  } else {
    pixels.setPixelColor(pos1, pixels.Color(0,0,0));
    pixels.setPixelColor(pos2, pixels.Color(0,0,0));
  }
}

void display_jam() {
  RtcDateTime now = Rtc.GetDateTime();
  const byte jam = now.Hour();
  const byte menit = now.Minute();

  pixels.clear();

  display_digit(3, jam / 10);
  display_digit(2, jam % 10);
  display_digit(1, menit / 10);
  display_digit(0, menit % 10);
  dot();

  pixels_show();
}

void pixels_show() {
  if (led_rainbowMode) {
    if ((millis() - mp_rainbow) >= rainbow_interval) {
      mp_rainbow = millis();
      led_rainbowColorVal++;
      // if (led_rainbowColorVal > 255){
      //   led_rainbowColorVal;
      // }
    }
  }
  pixels.show();
}

// Jadwal Sholat
void baca_jadwal_sholat() {
  refresh_jadwal_sholat = false;
  RtcDateTime now = Rtc.GetDateTime();

  set_calc_method(ISNA);
  set_asr_method(Shafii);
  set_high_lats_adjust_method(AngleBased);
  set_fajr_angle(20);
  set_isha_angle(18);

  get_prayer_times(now.Year(), now.Month(), now.Day(), c_latitude, c_longitude, GMT, times);

  for(byte i=0; i<6; i++) {
    byte pos_times = i;
    if (i > 3) pos_times += 1;
    else if(i == 1) pos_times = 0;

    get_float_time_parts(times[pos_times], waktu_sholat[i].jam, waktu_sholat[i].menit);
    if (i == 0) { // imsak
      waktu_sholat[i].menit -= ims;
    } else { // jam sholat
      waktu_sholat[i].menit += ihti;
    }
    if (waktu_sholat[i].menit < 0) {
      waktu_sholat[i].menit += 60;
      waktu_sholat[i].jam -= 1;
    }
    else if (waktu_sholat[i].menit >= 60) {
      waktu_sholat[i].menit -= 60;
      waktu_sholat[i].jam += 1;
    }

    yield();
  }
}

void print_jadwal_sholat() {
  RtcDateTime now = Rtc.GetDateTime();
  Serial.print("Waktu RTC: ");
  Serial.print(now.Hour());
  Serial.print(":");
  Serial.print(now.Minute());
  Serial.print(":");
  Serial.print(now.Second());
  Serial.print(" ");
  Serial.print(now.Day());
  Serial.print("-");
  Serial.print(now.Month());
  Serial.print("-");
  Serial.println(now.Year());

  const char* nama_waktu_sholat[6] = {"imsak", "subuh", "dzuhur", "ashar", "maghrib", "isya"};
  for (byte i=0; i<6; i++) {
    Serial.print(nama_waktu_sholat[i]);
    Serial.print(" -> ");
    Serial.print(waktu_sholat[i].jam);
    Serial.print(":");
    Serial.println(waktu_sholat[i].menit);
  }
}

void cek_waktu_sholat() {
  RtcDateTime now = Rtc.GetDateTime();
  const int
    jam = now.Hour(),
    menit = now.Minute(),
    detik = now.Second();
  for (byte i=0; i<=5; i++) {
    if (detik == 0) {
      if (jam == waktu_sholat[i].jam && menit == waktu_sholat[i].menit) {
        buzzerSholat_buzzing = true;
      }
    }
    yield();
  }
}

void cek_waktu_alarm() {
  if (!alarm_toggle) return;

  RtcDateTime now = Rtc.GetDateTime();
  const int
    jam = now.Hour(),
    menit = now.Minute(),
    detik = now.Second();

  if (jam == alarm.jam && menit == alarm.menit && detik == 0) {
    buzzerAlarm_buzzing = true;
  }
}

// Buzzer
void buzzerAlarm_process() {
  if (!buzzerAlarm_buzzing || buzzerSholat_buzzing) return;

  if ((millis() - mp_buzzerAlarm) >= buzzerAlarm_interval) {
    mp_buzzerAlarm = millis();

    digitalWrite(buzzer_pin, buzzer_state);
    if (!buzzer_state) {
      buzzerAlarm_counter++;
      if (buzzerAlarm_counter > buzzerAlarm_maxCounter) {
        buzzerAlarm_counter = 0;
        buzzerAlarm_buzzing = false;
      }
    }
    buzzer_state = !buzzer_state;
  }
}

void buzzerSholat_process() {
  if (!buzzerSholat_buzzing) return;

  if ((millis() - mp_buzzerSholat) >= buzzerSholat_interval) {
    mp_buzzerSholat = millis();

    digitalWrite(buzzer_pin, buzzer_state);
    if (!buzzer_state) {
      buzzerSholat_counter++;
      if (buzzerSholat_counter > buzzerSholat_maxCounter) {
        buzzerSholat_counter = 0;
        buzzerSholat_buzzing = false;
      }
    }
    buzzer_state = !buzzer_state;
  }
}

// Filesystem
void load_settings() {
  File file = LittleFS.open(file_settings, "r");
  if (!file) {
    Serial.println("Tidak bisa membuka file Settings");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, file);
  if (err) {
    Serial.println(F("deserialize() gagal"));
    return;
  }

  led_rainbowMode = doc["led"]["rainbowMode"].as<bool>();
  led_kecerahan =   doc["led"]["kecerahan"].as<unsigned short>();
  led_warnaSolid =  doc["led"]["warnaSolid"].as<unsigned short>();

  alarm_toggle = doc["alarm"]["toggle"].as<bool>();
  alarm.jam =    doc["alarm"]["jam"].as<short>();
  alarm.menit =  doc["alarm"]["menit"].as<short>();

  file.close();
}

void save_settings() {
  saving_settings = false;
  File file = LittleFS.open(file_settings, "w");
  if (!file) {
    Serial.println("Tidak bisa membuka file Settings");
    return;
  }

  StaticJsonDocument<256> doc;
  JsonObject j_led = doc.createNestedObject("led");
  JsonObject j_alarm = doc.createNestedObject("alarm");

  j_led["rainbowMode"] = led_rainbowMode;
  j_led["kecerahan"] =   led_kecerahan;
  j_led["warnaSolid"] =  led_warnaSolid;

  j_alarm["toggle"] = alarm_toggle;
  j_alarm["jam"] =    alarm.jam;
  j_alarm["menit"] =  alarm.menit;

  serializeJson(doc, file);
  file.close();
}

// webserver
void handleGetWaktu(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  JsonObject root = response->getRoot();

  RtcDateTime now = Rtc.GetDateTime();
  root["tanggal"] = now.Day();
  root["bulan"]   = now.Month();
  root["tahun"]   = now.Year();
  root["jam"]     = now.Hour();
  root["menit"]   = now.Minute();
  root["detik"]   = now.Second();

  response->setLength();
  request->send(response);
}

void handleGetSettings(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  JsonObject root = response->getRoot();
  JsonObject j_led = root.createNestedObject("led");
  JsonObject j_alarm = root.createNestedObject("alarm");

  j_led["rainbowMode"] = led_rainbowMode;
  j_led["kecerahan"] =   led_kecerahan;
  j_led["warnaSolid"] =  led_warnaSolid;

  j_alarm["toggle"] = alarm_toggle;
  j_alarm["jam"] =    alarm.jam;
  j_alarm["menit"] =  alarm.menit;

  response->setLength();
  request->send(response);
}

void handleGetJadwal(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  JsonObject root = response->getRoot();

  root["imsak_jam"] =     waktu_sholat[0].jam;
  root["imsak_menit"] =   waktu_sholat[0].menit;
  root["subuh_jam"] =     waktu_sholat[1].jam;
  root["subuh_menit"] =   waktu_sholat[1].menit;
  root["dzuhur_jam"] =    waktu_sholat[2].jam;
  root["dzuhur_menit"] =  waktu_sholat[2].menit;
  root["ashar_jam"] =     waktu_sholat[3].jam;
  root["ashar_menit"] =   waktu_sholat[3].menit;
  root["maghrib_jam"] =   waktu_sholat[4].jam;
  root["maghrib_menit"] = waktu_sholat[4].menit;
  root["isya_jam"] =      waktu_sholat[5].jam;
  root["isya_menit"] =    waktu_sholat[5].menit;

  response->setLength();
  request->send(response);
}

void handleLEDSettings(AsyncWebServerRequest *request) {
  if (request->hasArg("rainbowMode"))
    led_rainbowMode = (request->arg("rainbowMode") == "true") ? true : false; 
  if (request->hasArg("kecerahan")) {
    led_kecerahan = request->arg("kecerahan").toInt();
  }
  if (request->hasArg("warnaSolid")) {
    led_warnaSolid = request->arg("warnaSolid").toInt();
  }

  saving_settings = true;
  refresh_display = true;
  request->send(200);
}

void handleAlarmSettings(AsyncWebServerRequest *request) {
  if (request->hasArg("toggle"))
    alarm_toggle = (request->arg("toggle") == "true") ? true : false;
  if (request->hasArg("jam"))
    alarm.jam = request->arg("jam").toInt();
  if (request->hasArg("menit"))
    alarm.menit = request->arg("menit").toInt();

  saving_settings = true;
  request->send(200);
}

void handleAlarmOff(AsyncWebServerRequest *request) {
  buzzerAlarm_buzzing = false;
  buzzerAlarm_counter = 0;
  buzzer_state = false;
  digitalWrite(buzzer_pin, LOW);

  request->send(200, "text/plain", "Alarm OFF");
}

void handleRTCSettings(AsyncWebServerRequest *request) {
  RtcDateTime now = Rtc.GetDateTime();
  int  thn = now.Year(),
       bln = now.Month(),
       tgl = now.Day(),
       jam = now.Hour(),
       menit = now.Minute(),
       //detik = now.Second();
       detik = 0;

  if (request->hasArg("tanggal") && request->arg("tanggal") != "") {
    tgl = request->arg("tanggal").toInt();
  }
  if (request->hasArg("bulan") && request->arg("bulan") != "") {
    bln = request->arg("bulan").toInt();
  }
  if (request->hasArg("tahun") && request->arg("tahun") != "") {
    thn = request->arg("tahun").toInt();
  }
  if (request->hasArg("jam") && request->arg("jam") != "") {
    jam = request->arg("jam").toInt();
  }
  if (request->hasArg("menit") && request->arg("menit") != "") {
    menit = request->arg("menit").toInt();
  }

  Rtc.SetDateTime(RtcDateTime(thn, bln, tgl, jam, menit, detik));
  refresh_display = true;
  refresh_jadwal_sholat = true;
  request->send(200);
}
