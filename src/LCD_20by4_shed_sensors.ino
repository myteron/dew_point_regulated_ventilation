// pinout https://escapequotes.net/esp8266-wemos-d1-mini-pins-and-diagram/
// following ruffly https://github.com/MakeMagazinDE/Taupunktluefter/blob/main/Taupunkt_Lueftung.ino
// i2p setup
#include <Wire.h> 

// seeed 64x32 OLED setup
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27,20,4); // LCD: I2C-Addresse und Displaygröße setzen

// i2c ports for wire.h
#define SDA 4 // = D2 on NodeMCU v3 board
#define SCL 5 // = D1 on NodeMCU v3 board

// i2c SI7021 humidity and temperature sensor
// SI7021 I2C address is 0x40(64)
#include "Adafruit_HTU21DF.h"
Adafruit_HTU21DF htu_inside = Adafruit_HTU21DF();
Adafruit_HTU21DF htu_outside = Adafruit_HTU21DF();
// https://github.com/adafruit/Adafruit_HTU21DF_Library

// Air pressure BMP180
#include <Adafruit_BMP085.h>
#define I2P_MX_BMP180_CHANNEL 0
Adafruit_BMP085 bmp;
int pressure = 0;
int seaLevelPressure = 0;
float altitude = 0.0;
float realAltitude = 0.0;


//https://github.com/adafruit/DHT-sensor-library
#define GPIO_RELAY 16 // (D0) Relay for ventilation
#define GPIO_ENV_SENSOR_OUTSIDE 2 // (D4)
#define GPIO_PIR 14 // (D5)
int pirStat = 0; 
int pirStatBefore = 0; // this is mainly to have clean debugging

#define TEMP_MIN_OUTSIDE 8.0 // Minimum outside temperature for venting
#define DEW_POINT_MIN_DELTA 1.0 // Distance between switching on and off

////////////////////////////
// wifi setup
#include <WiFiClient.h>
#include <ESP8266WiFi.h> 
long wifiSignalStrength = 0.0; // wifi signal strength

// store ssid and password in a separate file called 'wifi_access.h'
// const char* ssid = "WifiName";
// const char* password = "WifiPassword";
#include "wifi_access.h"
IPAddress local_IP(192, 168, 1, 160); //TROUBLESHOOTING
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(192, 168, 1, 1); // its mandatory, don't even want this
IPAddress primaryDNS(8, 8, 8, 8);   //optional
//
////////////////////////////

// webserver
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
ESP8266WebServer server(80);


// relay state true=on false=off
bool rel; 
#define ON LOW
#define OFF HIGH

// we want to prevent any flip flopping in case we are on the edge
long rel_loops_until_next_check = 0; // counts the loops since last time we checked 
long rel_min_loops_before_check = 10; // threshhold when we can change state


// global storage for sensor readings
float hum_inside = 0.0;
float temp_inside = 0.0;
float dew_point_inside = 0.0;
float hum_outside = 0.0;
float temp_outside = 0.0;
float dew_point_outside = 0.0;

void TCA9548A(uint8_t bus)
{
  Wire.beginTransmission(0x70);  // TCA9548A address is 0x70
  Wire.write(1 << bus);          // send byte to select bus
  Wire.endTransmission();
}

void connect_wifi(){

  Serial.println("Setting Wifi to only 2.4GHz");
  Serial.println(WiFi.macAddress());
  WiFi.setPhyMode(WIFI_PHY_MODE_11B); 
  Serial.println("Connecting to Wifi ");
  Serial.println(ssid);
  Serial.println();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connecting     ");
  lcd.display();
  // WiFi.mode(WIFI_STA);
  // Configures static IP address
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(ssid, password);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED){

    // need to convert IPAddress to char for OLED
    char IP[] = "xxx.xxx.xxx.xxx";          // buffer
    IPAddress ip = WiFi.localIP();
    ip.toString().toCharArray(IP, 16);
    lcd.setCursor(0, 0);
    lcd.print("WiFi connecting ..OK");
    lcd.setCursor(0, 1);
    lcd.printf("IP=%s", IP);
    Serial.print("Wifi Connected IP=");     
    Serial.println(WiFi.localIP());
  } else {
    Serial.print("Wifi Connection failed!");
    lcd.setCursor(0, 0);
    lcd.print("WiFi connecting ERROR");
  }
  lcd.display();
  delay(1000);
}

void httpd_start() {
  Serial.println("Starting webserver");
  lcd.setCursor(0,2);
  lcd.print("httpd");
  lcd.display();
  delay(200); 

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();
  lcd.setCursor(0,2);
  lcd.print("httpd....OK");
  Serial.println("Webserver started");
  lcd.display();
  delay(200); 
}

void display_setup(){
    // seed 64x32 display
  Serial.println("Starting display");
  
  lcd.init();
  lcd.backlight();                      
  lcd.setCursor(0,0);
  lcd.print(F("Starting display.."));
  lcd.display();
}

void sensor_start(){
  Serial.println("Starting sensors");
  lcd.setCursor(0, 3);
  lcd.print("Sensors ");
  delay(200); 

  // Set multiplexer to channel 2 to initialize inside sensor
  TCA9548A(2);
  Serial.println("Starting HTU21D-F inside sensor");
  if (!htu_inside.begin()) {
    Serial.println("Couldn't find inside sensor!");
    while (100);
  }
  delay(100); 
 
  lcd.setCursor(0, 3);
  lcd.print("Sensor 1             ");
  lcd.display();
  delay(200); 
  lcd.setCursor(0, 3);
  if (isnan(htu_inside.readTemperature())) {
    Serial.println("Error reading inside sensor");
    lcd.print("Sensor 1 .... ERROR  ");
    lcd.display();
    delay(1000); 
  } else {
    Serial.println("Reading HTU21D-F inside sensor OK");
    lcd.print("Sensor 1 .... OK     ");
  }
  lcd.display();
  delay(200); 


  // Set multiplexer to channel 2 to initialize outside sensor
  TCA9548A(7);
  Serial.println("Starting HTU21D-F outside sensor");
  if (!htu_outside.begin()) {
    Serial.println("Couldn't find outside sensor!");
    while (100);
  }
  delay(200); 

  lcd.setCursor(0, 3);
  lcd.print("Sensor 2             ");
  lcd.display();
  delay(200); 
  lcd.setCursor(0, 3);
  if (isnan(htu_outside.readTemperature())) {
    Serial.println("Error reading outside sensor");
    lcd.print("Sensor 2 .... ERROR  ");
    lcd.display();
    delay(1000); 
  } else {
    Serial.println("Reading HTU21D-F outside sensor OK");
    lcd.print("Sensor 2 .... OK     ");
    lcd.display();
    delay(200); 
  }

  lcd.setCursor(0, 3);
  lcd.print("Sensor 3             ");
  lcd.display();
  delay(200); 
  lcd.setCursor(0, 3);
  if (!bmp.begin()) {
    Serial.println("Error with BMP180 pressure sensor");
    lcd.print("Sensor 3 .... ERROR  ");
    lcd.display();
    delay(1000); 
  } else {
    Serial.println("Reading BMP180 pressure sensor OK");
    lcd.print("Sensor 3 .... OK     ");
  }
  lcd.display();
  delay(1000); 
}


void setup() {
  Serial.begin(115200);
  Serial.println("setup");
  Wire.begin(SDA,SCL);
  Serial.println("Configuring i2c 10KHz");
  Wire.setClock(10000); // lower speed on the i2c bus
  pinMode(GPIO_RELAY, OUTPUT);
  digitalWrite(GPIO_RELAY, HIGH); // cycle through on during boot
  pinMode(GPIO_PIR, INPUT);
  delay(100);

  display_setup();
  
  WiFi.setOutputPower(20.5);
  connect_wifi();
  httpd_start();
  sensor_start();
 
  // make sure we check if the relay should be on of off at boot
  rel_loops_until_next_check = rel_min_loops_before_check; 
  delay(2000);
}

void loop() {
  wifiSignalStrength = WiFi.RSSI();
  TCA9548A(2);
  float t1 = htu_inside.readTemperature();
  float h1 = htu_inside.readHumidity();
  TCA9548A(7);
  float t2 = htu_outside.readTemperature();
  float h2 = htu_outside.readHumidity();

  if (isnan(h1) || isnan(t1) || h1 > 100 || h1 < 1 || t1 < -40 || t1  > 80) {
    Serial.println("Error reading inside sensor");
  } else {
    temp_inside = t1;
    hum_inside = h1;
    dew_point_inside = dewpoint(temp_inside, hum_inside);
  }

  if (isnan(h2) || isnan(t2) || h2 > 100 || h2 < 1 || t2 < -40 || t2  > 80) {
    Serial.println("Error reading outside sensor");
  } else {
    temp_outside = t2;
    hum_outside = h2;
    dew_point_outside = dewpoint(temp_outside, hum_outside);
  }

  pressure = bmp.readPressure();
  seaLevelPressure = bmp.readSealevelPressure();
  altitude = bmp.readAltitude();
  realAltitude = bmp.readAltitude(101500);
  //        "                     "
  lcd.setCursor(0, 0);
  if (rel) {
    lcd.print(" inside vent outside ");
  } else {
    lcd.print(" inside  |  outside ");
  }
  lcd.setCursor(0, 1);
  //"  10.10C |  10.10C  "
  String pad1 = "";
  String pad2 = "";
  
  if (temp_outside < 10.0) {
    pad1 = " ";
  }
  if (temp_outside < 10.0) {
    pad2 = " ";
  }
  lcd.printf("  %s%02.2fC |  %s%02.2fC  ", pad1, temp_inside, pad2, temp_outside);
  //"   10%   |   10%    "
  lcd.setCursor(0, 2);
  lcd.printf("   %02.0f%%   |   %02.0f%%   ", hum_inside, hum_outside);
  
  lcd.setCursor(0, 3);
  //"D 10.10C |  10.10C  "
  if (dew_point_inside < 10.0) {
    pad1 = " ";
  } else { 
    pad1 = "";
  }
  if (dew_point_outside < 10.0) {
    pad2 = " ";
  } else {
    pad2 = "";
  }
  lcd.printf("D %s%02.2fC |  %s%02.2fC   ",pad1, dew_point_inside, pad2, dew_point_outside);

  /////////////////////////////////
  // ventilation/relay routine
  rel_loops_until_next_check += 1;
  if (rel_loops_until_next_check > rel_min_loops_before_check ) {
    // Print sensor readings for TS:
    char buff[200];
    sprintf(buff, "inside : %02.2fC %02.2f%% dew:%02.2f", temp_inside, hum_inside, dew_point_inside);
    Serial.println(buff);
    sprintf(buff, "outside: %02.2fC %02.2f%% dew:%02.2f", temp_outside, hum_outside, dew_point_outside);
    Serial.println(buff);
    sprintf(buff, "pressure: %02dPa seaLevelPressure %02d%Pa altitude:%02.2f realAltitude:%02.2f", pressure, seaLevelPressure, altitude, realAltitude);
    Serial.println(buff);
    sprintf(buff, "wifiSignalStrength: %02d", wifiSignalStrength);
    Serial.println(buff);
    rel_loops_until_next_check = 0;
    
    // default condition is off unless we meet one of the following criteria
    rel = false;

    // A lower outside dew point indicates dryer air
    if (dew_point_outside + DEW_POINT_MIN_DELTA < dew_point_inside) rel = true;
    
    // but we don't want to vent if it is to cold outside
    if (temp_outside < TEMP_MIN_OUTSIDE )rel = false;

    // We neither want to vent if the inside is so cold that the outside air condensates
    // or in other words the inside temperature must be higher then the outside dew point.
    if (temp_inside + DEW_POINT_MIN_DELTA < dew_point_outside)rel = false;

    if (rel)
    {
      Serial.println("Ventilation on");
      digitalWrite(GPIO_RELAY, HIGH); // Ventilation on
    } else {
      Serial.println("Ventilation off");
      digitalWrite(GPIO_RELAY, LOW); // Ventilation off
    }
  }


  //////////////////////////////
  // webserver and PIR routine
  // in order for both to stay responsive 
  // and without overloading the cpu
  int subcount = 1;

  for (int i = 0; i< 1000 / 4; i++) {
    subcount += 1;
    server.handleClient();
    if (subcount > 50) {
      subcount = 0;

      //////////////////////////////
      // to avoid to much switching 
      // and for clean serial debug
      // we do this a bit arkwardly
      pirStat = digitalRead(GPIO_PIR);
      if (pirStatBefore < pirStat) {
        Serial.println("PIR: There was movement");
        lcd.backlight();
        pirStatBefore = pirStat;
      }
      if (pirStatBefore > pirStat) {
        Serial.println("PIR: No movement");
        lcd.noBacklight();
        pirStatBefore = pirStat;
      }
    }
    delay(5);
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    connect_wifi();
  }
}

float dewpoint(float t, float r) {
  float a, b;
  
  if (t >= 0) {
    a = 7.5;
    b = 237.3;
  } else if (t < 0) {
    a = 7.6;
    b = 240.7;
  }
  
  // Sättigungsdampfdruck in hPa
  float sdd = 6.1078 * pow(10, (a*t)/(b+t));
  
  // Vaporpressure in hPa
  float dd = sdd * (r/100);
  
  // v-Parameter
  float v = log10(dd/6.1078);
  
  // Duepoint temperature (°C)
  float tt = (b*v) / (a-v);
  return { tt };  
}

void handleRoot() {
  char buff [100];
  String message = "{\n";
  sprintf(buff, "  \"temp_inside\": %02.2f,\n", temp_inside);
  message += String(buff);
  sprintf(buff, "  \"hum_inside\": %02.2f,\n", hum_inside);
  message += String(buff);
  sprintf(buff, "  \"dew_point_inside\": %02.2f,\n", dew_point_inside);
  message += String(buff);
  sprintf(buff, "  \"temp_outside\": %02.2f,\n", temp_outside);
  message += String(buff);
  sprintf(buff, "  \"hum_outside\": %02.2f,\n", hum_outside);
  message += String(buff);
  sprintf(buff, "  \"dew_point_outside\": %02.2f,\n", dew_point_outside);
  message += String(buff);
  sprintf(buff, "  \"ventilation\": %d,\n", rel);
  message += String(buff);
  sprintf(buff, "  \"pirStat\": %d,\n", pirStat);
  message += String(buff);
  sprintf(buff, "  \"wifiSignalStrength\": %d,\n", wifiSignalStrength);
  message += String(buff);
  sprintf(buff, "  \"pressure\": %02d,\n", pressure);
  message += String(buff);
  sprintf(buff, "  \"seaLevelPressure\": %02d,\n", seaLevelPressure);
  message += String(buff);
  sprintf(buff, "  \"altitude\": %02.2f,\n", altitude);
  message += String(buff);
  sprintf(buff, "  \"realAltitude\": %02.2f\n", realAltitude);
  message += String(buff);
  sprintf(buff, "}", message);
  message += String(buff);
  server.send(200, "text/json", message);
  // Serial.println(message); //TROUBLESHOOTING
}

void handleNotFound() {
  server.send(404, "text/plain", "not found");
}


