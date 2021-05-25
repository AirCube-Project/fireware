/**
   BasicHTTPClient.ino

    Created on: 24.05.2015

*/

#include <Arduino.h>
#include "ESP8266TimerInterrupt.h"
ESP8266Timer ITimer;
#define TIMER_INTERVAL_MS           5000

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <WiFiClient.h>
#include <WebSockets2_Generic.h>
#include <ArduinoJson.h>

#include <SD.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
//#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

#define TFT_CS         5
#define TFT_RST        16
#define TFT_DC         2
#define SELECT_PIN     16
#define SCREEN_OFF_PIN 4

#define SELECT_SCREEN0 4
#define SELECT_SCREEN1 5
#define SELECT_SCREEN2 6
#define SELECT_SCREEN3 7

Adafruit_ST7735 tft1 = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ST7735 tft2 = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ST7735 tft3 = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Adafruit_ST7735 tft4 = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
ESP8266WiFiMulti WiFiMulti;

//auto timer = timer_create_default();

class ScreenStream : public Stream {
  private:
    Adafruit_ST7735 *tft;
    int width;
    int height;
    bool odd;
    uint8_t temp;
    int closed;
    int count;
  public:
    ScreenStream(Adafruit_ST7735 *_tft, int _width, int _height);
    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buf, size_t size);
    virtual int availableForWrite();
    virtual int read();
    virtual int peek();
    virtual int available();
    virtual void flush();
    void close();
};

ScreenStream::ScreenStream(Adafruit_ST7735 *_tft, int _width, int _height) {
  tft = _tft;
  width = _width;
  height = _height;
  tft->startWrite();
  closed = 0;
  count = 0;
  odd = false;
  tft->setAddrWindow(0, 0, height, width);
}

size_t ScreenStream::write(uint8_t b) {
  count++;
  if (odd) {
    uint32_t data;
    data = temp + (b << 8);
    tft->pushColor(data);
    odd = false;
  } else {
    temp = b;
    odd = true;
  }
}

size_t ScreenStream::write(const uint8_t *buf, size_t size) {
  Serial.printf("Dec = %d\n", size);
  for (int i = 0; i < size; i++) {
    write(*buf);
    buf++;
  }
  return size;
}

void ScreenStream::close() {
  if (!closed) {
    tft->endWrite();
    closed = 1;
  }
  Serial.printf("Total bytes is %d", count);
}

int ScreenStream::availableForWrite() {
  return 1;
}

int ScreenStream::read() {
  return -1;
}

int ScreenStream::peek() {
  return 0;
}

int ScreenStream::available() {
  return 0;
}

void ScreenStream::flush() {
}

using namespace websockets2_generic;

int sending = 0;

WebsocketsClient client;

int hexCharToDec(uint8 chr) {
  if (chr >= '0' && chr <= '9') {
    return chr - '0';
  }
  return chr - 'A' + 10;
}

bool update_screens[4];
bool update_texts[4];


void onMessageCallback(WebsocketsMessage message)
{
  Serial.print("Got Message: ");
  Serial.println(message.data());
  String msg = message.data();
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, msg);
  JsonObject obj = doc.as<JsonObject>();
  bool is_text = obj[String("is_text")];
  int screen = obj[String("screen")];
  String color = obj[String("color")];
  if (color != NULL && color.length() != 0) {
    //    Serial.println("Bottom line color updated");
    String R = color.substring(1, 3);
    R.toUpperCase();
    String G = color.substring(3, 5);
    G.toUpperCase();
    String B = color.substring(5, 7);
    B.toUpperCase();
    int r = hexCharToDec(R.charAt(0)) * 16 + hexCharToDec(R.charAt(1));
    int g = hexCharToDec(G.charAt(0)) * 16 + hexCharToDec(G.charAt(1));
    int b = hexCharToDec(B.charAt(0)) * 16 + hexCharToDec(B.charAt(1));
    //    Serial.printf("%d\n", r);
    //    Serial.printf("%d\n", g);
    //    Serial.printf("%d\n", b);
    byte data[6];
    data[0] = 0xFD;
    data[1] = 4;
    data[2] = 1;
    data[3] = (byte)r / 8;
    data[4] = (byte)g / 8;
    data[5] = (byte)b / 8;
    sending = 1;
    sendToArduino(data, 6);
    sending = 0;
  } else {
    Serial.printf("Screen %d", screen);
    if (screen == 2) {
      if (!is_text) {
        update_screen(screen);
        //        update_screens[screen] = true;
        //        update_texts[screen] = false;
      } else {
        Serial.println("Text accepted");
        //      update_texts[screen] = true;
        //      update_screens[screen] = false;
      }
    }
  }
}

void onEventsCallback(WebsocketsEvent event, String data)
{
  if (event == WebsocketsEvent::ConnectionOpened)
  {
    Serial.println("Connnection Opened");
    const size_t CAPACITY = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<CAPACITY> hello;

    JsonObject obj = hello.to<JsonObject>();
    obj["token"] = "oJnNPGsiuzytMOJPatwtPilfsfykSBGp";
    obj["SN"] = "1";
    String output;
    serializeJson(obj, output);
    client.send(output);
  }
  else if (event == WebsocketsEvent::ConnectionClosed)
  {
    Serial.println("Connnection Closed");
  }
  else if (event == WebsocketsEvent::GotPing)
  {
    Serial.println("Got a Ping!");
  }
  else if (event == WebsocketsEvent::GotPong)
  {
    Serial.println("Got a Pong!");
  }
}

void setup() {

  Serial.begin(115200);
  if (!SD.begin(15)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("initialization done.");

  for (uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] WAIT %d...\n", t);
    Serial.flush();
    delay(1000);
  }
  pinMode(SCREEN_OFF_PIN, OUTPUT);
  pinMode(SELECT_PIN, OUTPUT);
  SPI.begin();

  Serial.println("Hello");

  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP("manhattan", "5BCGxErp");
  Serial.println("Wifi connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  if ((WiFiMulti.run() == WL_CONNECTED)) {
    Serial.println("Wifi connected");

    // run callback when messages are received
    client.onMessage(onMessageCallback);

    // run callback when events are occuring
    client.onEvent(onEventsCallback);

    sending = 1;
    setToArduinoSingleByte(99);

    pinMode(SCREEN_OFF_PIN, OUTPUT);
    //
    digitalWrite(SCREEN_OFF_PIN, 0);
    setToArduinoSingleByte(SELECT_SCREEN0);
    delay(200);
    tft1.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
    tft1.fillScreen(ST77XX_MAGENTA);
    digitalWrite(SCREEN_OFF_PIN, 1);
    delay(200);

    digitalWrite(SCREEN_OFF_PIN, 0);
    setToArduinoSingleByte(SELECT_SCREEN1);
    delay(20);
    tft2.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
    tft2.fillScreen(ST77XX_ORANGE);
    digitalWrite(SCREEN_OFF_PIN, 1);
    delay(200);


    digitalWrite(SCREEN_OFF_PIN, 0);
    setToArduinoSingleByte(SELECT_SCREEN2);
    delay(20);
    tft3.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
    tft3.fillScreen(ST77XX_GREEN);
    digitalWrite(SCREEN_OFF_PIN, 1);
    delay(200);

    digitalWrite(SCREEN_OFF_PIN, 0);
    setToArduinoSingleByte(SELECT_SCREEN3);
    delay(20);
    tft4.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
    tft4.fillScreen(ST77XX_RED);
    digitalWrite(SCREEN_OFF_PIN, 1);
    delay(200);

    // Connect to server
    client.connect("api.aircube.tech", 80, "/ws");

    if (ITimer.attachInterruptInterval(TIMER_INTERVAL_MS * 1000, get_data))
    {
      Serial.print(F("Starting  ITimer OK, millis() = ")); Serial.println(millis());
    }
    else
      Serial.println(F("Can't set ITimer. Select another freq. or timer"));
  }
}


void get_data() {
  if (sending == 0) {
    //get data
    Serial.print("Getting data\n");
    byte req[3];
    req[0] = 0xFD;
    req[1] = 1;
    req[2] = 2;
    byte response[22];
    sendToArduinoAndGetResult(req, 3, response, 22);
    if (response[0] == 255) {
      float temperature = float(response[1] + (response[2] << 8)) / 10;
      int gyrox = response[4] << 8 | response[3];
      int gyroy = response[6] << 8 | response[5];
      int gyroz = response[8] << 8 | response[7];
      int accelx = response[10] << 8 | response[9];
      int accely = response[12] << 8 | response[11];
      int accelz = response[14] << 8 | response[13];
      if (gyrox > 32768) {
        gyrox = gyrox - 65536;
      }
      if (gyroy > 32768) {
        gyroy = gyroy - 65536;
      }
      if (gyroz > 32768) {
        gyroz = gyroz - 65536;
      }

      if (accelx > 32768) {
        accelx = accelx - 65536;
      }
      if (accely > 32768) {
        accely = accely - 65536;
      }
      if (accelz > 32768) {
        accelz = accelz - 65536;
      }

      float xg = float(accelx) * 0.0039;
      float yg = float(accely) * 0.0039;
      float zg = float(accelz) * 0.0039;
      float alpha = 0.7;
      float fXg = xg * alpha + fXg * (1.0 - alpha);
      float fYg = yg * alpha + fYg * (1.0 - alpha);
      float fZg = zg * alpha + fZg * (1.0 - alpha);
      float roll = atan2(yg, zg) * 180 / PI;
      float pitch = atan2(-xg, sqrt(yg * yg + zg * zg)) * 180 / PI;
      Serial.printf("Temperature is %4.1f\n", temperature);
      Serial.printf("AccelX is %d\n", accelx);
      Serial.printf("AccelY is %d\n", accely);
      Serial.printf("AccelZ is %d\n", accelz);
      Serial.printf("GyroX is %d\n", gyrox);
      Serial.printf("GyroY is %d\n", gyroy);
      Serial.printf("GyroZ is %d\n", gyroz);
      Serial.printf("Roll is %f\n", roll);
      Serial.printf("Pitch is %f\n", pitch);
    }
    //check for screen updates
    //    for (int screen = 0; screen < 4; screen++) {
    //      if (update_screens[screen] && !update_texts[screen]) {
    //        update_screens[screen] = false;
    //        update_screen(screen);
    //      }
    //      if (update_texts[screen]) {
    //        update_texts[screen] = false;
    //        update_text(screen);
    //      }
    //    }
  }
}

int cnt = 0;

void loop() {
  if (client.available())
  {
    client.poll();
  }
}

void sendToArduino(byte * data, int size) {
  Serial.print("SA\n");
  digitalWrite(SELECT_PIN, LOW);
  delay(10);
  SPI.beginTransaction(SPISettings(125000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < size; i++) {
    Serial.println(*data);
    SPI.transfer(char(*data));
    data++;
    delay(20);
  }
  SPI.endTransaction();
  digitalWrite(SELECT_PIN, HIGH);
}

void setToArduinoSingleByte(byte b) {
  byte data[3];
  data[0] = 0xFD;
  data[1] = 1;
  data[2] = b;
  sendToArduino(data, 3);
}

void sendToArduinoAndGetResult(byte * data, int size, byte * read, int read_size) {
  Serial.print("SAGR\n");
  sending = 1;
  digitalWrite(SELECT_PIN, LOW);
  SPI.beginTransaction(SPISettings(125000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < size; i++) {
    SPI.transfer(char(*data));
    data++;
  }
  //padding and sync
  delay(10);
  SPI.transfer(0);
  //padding
  for (int i = 0; i < read_size; i++) {
    byte r = SPI.transfer(0);
    //    Serial.printf("D is %d\n", r);
    *read = r;
    read++;
  }
  SPI.endTransaction();
  digitalWrite(SELECT_PIN, HIGH);
  sending = 0;
}




byte arduinoSend(byte b) {
  Serial.printf("Byte sent: %d\n", b);
  byte result = SPI.transfer(b);
  return result;
}

void update_text(int screen) {
  char path[40];
  WiFiClient wifi;

  HTTPClient http;

  sprintf(path, "http://api.aircube.tech/api/v1/list/%d", screen);
  if (http.begin(wifi, path)) { // HTTP

    http.addHeader("Authorization", "bearer oJnNPGsiuzytMOJPatwtPilfsfykSBGp");
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

        String response = http.getString();
        Serial.println(response);
        DynamicJsonDocument doc(10240);

        DeserializationError error = deserializeJson(doc, response);

        if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
          return;
        }

        const char* title = doc["title"]; // "Главное меню"
        bool navigable = doc["navigable"]; // true

        for (JsonObject elem : doc["items"].as<JsonArray>()) {

          int x = elem["x"]; // 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
          int y = elem["y"]; // 8, 20, 40, 52, 64, 76, 88, 100, 112, 124, 136, 148, 160, 172
          const char* text = elem["text"]; // "Привет,", "Ариночка", "Значимость", "этих", "проблем", "настолько", ...
          int number = elem["number"]; // 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
          int icon_width = elem["icon_width"]; // null, 16, null, null, null, null, null, null, null, null, null, ...
          int icon_height = elem["icon_height"]; // null, 16, null, null, null, null, null, null, null, null, ...
          const char* icon = elem["icon"]; // null, ...
          const char* color = elem["color"]; // "#FFFF00", null, null, null, null, null, null, null, null, null, ...

          Serial.println(text);
        }
        //          sending = 1;
      }
    }
  }
  //
}

void update_screen(int screen) {
  //  if (screen == 2) {
  //    WiFiClient wifi;
  //
  //    HTTPClient http;
  //
  //    pinMode(LED_BUILTIN, OUTPUT);
  //    if (http.begin(wifi, "http://api.aircube.tech/api/v1/screen/2")) {  // HTTP
  //
  //      http.addHeader("Authorization", "bearer oJnNPGsiuzytMOJPatwtPilfsfykSBGp");
  //      int httpCode = http.GET();
  //      if (httpCode > 0) {
  //        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  //        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
  //          sending = 1;
  //
  //          digitalWrite(SCREEN_OFF_PIN, 0);
  //
  //          setToArduinoSingleByte(SELECT_SCREEN1);
  //          delay(20);
  //
  //          Serial.println("Drawing screen");
  //
  //          ScreenStream screen1 = ScreenStream(&tft2, 160, 120);
  //          http.writeToStream(&screen1);
  //          screen1.flush();
  //
  //          Serial.println("Drawing is completed");
  //          digitalWrite(SCREEN_OFF_PIN, 1);
  //          delay(20);
  //          sending = 0;
  //        }
  //      } else {
  //        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  //      }
  //      http.end();
  //      // server address, port and URL
  //      //    webSocket.begin("api.dzolotov.tech", 80, "/ws");
  //
  //      // event handler
  //      //    webSocket.onEvent(webSocketEvent);
  //      //    webSocket.setReconnectInterval(5000);
  //    } else {
  //      Serial.printf("[HTTP] Unable to connect\n");
  //    }
  //  } else
  //  if (screen == 3) {
  //    WiFiClient wifi;
  //
  //    HTTPClient http;
  //
  //    char path[40];
  //    //    int real_screen = screen;
  //    //    if (real_screen==0) {
  //    //      real_screen=1;
  //    //    }
  //
  //    sprintf(path, "http://api.aircube.tech/api/v1/screen/%d", screen);
  //    Serial.println(path);
  //    if (http.begin(wifi, path)) { // HTTP
  //
  //      http.addHeader("Authorization", "bearer oJnNPGsiuzytMOJPatwtPilfsfykSBGp");
  //      int httpCode = http.GET();
  //      if (httpCode > 0) {
  //        Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  //        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
  //          sending = 1;
  //
  //          digitalWrite(SCREEN_OFF_PIN, 0);
  //
  //          Adafruit_ST7735 *tft;
  //          int command;
  //
  //          switch (screen) {
  //            case 0:
  //              tft = &tft1;
  //              command = SELECT_SCREEN0;
  //              break;
  //            case 1:
  //              tft = &tft2;
  //              command = SELECT_SCREEN1;
  //              break;
  //            case 2:
  //              tft = &tft3;
  //              command = SELECT_SCREEN2;
  //              break;
  //            case 3:
  //              tft = &tft4;
  //              command = SELECT_SCREEN3;
  //              break;
  //          }
  //
  //          setToArduinoSingleByte(command);
  //          delay(100);
  //
  //          ScreenStream screen = ScreenStream(tft, 160, 120);
  //          http.writeToStream(&screen);
  //          screen.flush();
  //          delay(100);
  //          digitalWrite(SCREEN_OFF_PIN, 1);
  //          delay(100);
  //          sending = 0;
  //        }
  //      } else {
  //        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  //      }
  //      http.end();
  //      // server address, port and URL
  //      //    webSocket.begin("api.dzolotov.tech", 80, "/ws");
  //
  //      // event handler
  //      //    webSocket.onEvent(webSocketEvent);
  //      //    webSocket.setReconnectInterval(5000);
  //    } else {
  //      Serial.printf("[HTTP] Unable to connect\n");
  //    }
  //  }
}
