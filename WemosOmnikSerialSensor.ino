#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

// OLED Display
// SCL GPIO5, SDA GPIO4
#define OLED_RESET 0  // GPIO0
Adafruit_SSD1306 display(OLED_RESET);

// OmnikSol WiFi
const long  omnikSerial       = 602123456;
const char* omnikSsid         = "AP_602123456";
const char* omnikPassword     = "PasswdforAP";
byte omnikIp []               = {10, 10, 100, 254};

WiFiClient espClient;
uint8_t screen = 0;
uint8_t line = 0;
unsigned long omniksol_update, screen_update;
uint16_t max_omniksol_power = 1750;
char magicMessage[] = {0x68, 0x02, 0x40, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x16};

// Holds gathered data
typedef struct {
  char ID[20];
  char Status[10];
  uint16_t PVVoltageDC; // divide by 10
  uint16_t PVCurrentDC; // divide by 10
  uint16_t VoltageAC;   // divide by 10
  uint16_t PowerAC;     // don't divide
  uint16_t CurrentAC;   // divide 10
  uint16_t FrequencyAC; // divide by 100
  uint16_t Temperature; // divide by 10
  uint16_t EnergyToday; // divide by 100
  uint16_t TotalEnergy; // divide by 10
  uint16_t TotalHours;
}Omniksol;
Omniksol omniksol;

boolean startWifi (const char* ssid, const char* password) {
  // set station mode
  WiFi.mode(WIFI_STA);

  // attempt to connect to Wifi network:
  displayLog("Connect AP");
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
  }

  int c = 100;
  while (c-- && WiFi.status() != WL_CONNECTED) delay (100);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_OFF);
    return false;
  } else {
    displayLog("Connected");
  }
  return true;
}

void calcMagicMessage() {

  int checksum = 0;
  
  for (uint8_t i=0; i<4; i++) {
    magicMessage[4+i] = magicMessage[8+i] = ((omnikSerial>>(8*i))&0xff);
    checksum += magicMessage[4+i];
  }
  checksum *= 2;
  checksum += 115;
  checksum &= 0xff;
  magicMessage[14] = checksum;
}

// Sensor data on OLED
void drawOLED() {
  
  display.clearDisplay();
  display.setCursor(0,0);

  switch (screen) {
    case 0:
      display.println("CUR.POWER:");
      display.println(String(omniksol.PowerAC) + " Watt");   
      display.println("\nTEMP C:");
      float temp;
      temp = float (omniksol.Temperature) / 10;
      char temperature[4];
      dtostrf(temp, 2, 1, temperature );
      display.println(String(temperature) + " C");
      break;
    case 1:
      display.println("TODAY KWH:");
      float todaypower;
      todaypower = float (omniksol.EnergyToday) / 100;
      display.println(String(todaypower));
      float totalpower;
      totalpower = float (omniksol.TotalEnergy) / 10;
      display.println("\nTOTAL KWH:");
      display.println(String(totalpower));   
      break;
  }
  display.display();    
}

// Log on OLED
void displayLog(const char* txt) {

  switch (line) {
    case 0:
      display.clearDisplay();
      display.setCursor(0, line);
      display.println(txt);
      line = line + 10;
      break;
    case 10:
      //display.setCursor(0, line);
      display.println(txt);
      line = line + 10;
      break;
    case 20:
      //display.setCursor(0, line);
      display.println(txt);
      line = line + 10;
      break;
    case 30:
      //display.setCursor(0, line);
      display.println(txt);
      line = line + 10;
      break;
    case 40:
     //display.clearDisplay();
     //display.setCursor(0, line);
     display.println(txt);
     line = 0;
     break;
  }
  display.display();
}

// Connect to Omniksol and get/parse data
void getOmniksolData() {

  // Connect to AP if not connected, return if failed
  if (WiFi.status() != WL_CONNECTED) {
    if (!startWifi(omnikSsid, omnikPassword)) {
      return;
    }  
  }

  // Open connection to inverter
  if (!espClient.connect (omnikIp, 8899)) {
    displayLog("Cannot Open");
    return;
  }
  delay(100);

  // Send magic
  displayLog("Query AP");
  espClient.write ((const uint8_t*)magicMessage, (uint8_t) 16);

  // Wait 5 seconds for a reply, return if no
  unsigned long timeout = millis();
  while (espClient.available() == 0) {
    if (millis() - timeout > 5000) {
      displayLog("Timeout!");
      espClient.stop();
      return;
    }
  }

  // Read all available data
  uint16_t dataSize = espClient.available();
  char server_reply[dataSize + 1];
  
  if(espClient.available() > 0 && dataSize == 99) {
    for(uint16_t i = 0; i < dataSize - 1; i++) {
      server_reply[i] = espClient.read();
    }
  }
  espClient.stop();
  omnikFillStruct(server_reply);
}

void sendOmniksolData() {

  DynamicJsonBuffer  dataBuffer;
  JsonObject& dataJson = dataBuffer.createObject();

  dataJson[F("ID")]              = omniksol.ID;
  dataJson[F("Status")]          = omniksol.Status;
  dataJson[F("PowerAC")]         = omniksol.PowerAC;
  dataJson[F("VoltageAC")]       = float (omniksol.VoltageAC) / 10;
  dataJson[F("CurrentAC")]       = float (omniksol.CurrentAC) / 10;
  dataJson[F("FrequencyAC")]     = float (omniksol.FrequencyAC) / 100;
  dataJson[F("PVVoltageDC")]     = float (omniksol.PVVoltageDC) / 10;
  dataJson[F("PVCurrentDC")]     = float (omniksol.PVCurrentDC) / 10;
  dataJson[F("EnergyToday")]     = float (omniksol.EnergyToday) / 100;
  dataJson[F("TotalEnergy")]     = float (omniksol.TotalEnergy) / 10;
  dataJson[F("TotalHours")]      = omniksol.TotalHours;
  dataJson[F("Temperature")]     = float (omniksol.Temperature) / 10;

  dataJson.printTo(Serial);
  Serial.println("\n");
}

void sendOmniksolDataOffline() {

  DynamicJsonBuffer  dataBuffer;
  JsonObject& dataJson = dataBuffer.createObject();

  dataJson[F("ID")]              = omniksol.ID;
  dataJson[F("Status")]          = "Offline";
  dataJson[F("PowerAC")]         = "";
  dataJson[F("VoltageAC")]       = "";
  dataJson[F("CurrentAC")]       = "";
  dataJson[F("FrequencyAC")]     = "";
  dataJson[F("PVVoltageDC")]     = "";
  dataJson[F("PVCurrentDC")]     = "";
  dataJson[F("EnergyToday")]     = float (omniksol.EnergyToday) / 100;
  dataJson[F("TotalEnergy")]     = float (omniksol.TotalEnergy) / 10;
  dataJson[F("TotalHours")]      = omniksol.TotalHours;
  dataJson[F("Temperature")]     = "";

  dataJson.printTo(Serial);
  Serial.println("\n");
}

void omnikFillStruct(char *server_reply) {

  // First check if valid (If the power is higher than max_omniksol_power it's invalid)
  if ( (server_reply[60] + (server_reply[59] * 256)) <= max_omniksol_power ) {

    // Get Omniksol Inverter ID
    strncpy(omniksol.ID, &server_reply[15], 16);
    omniksol.ID[16] = 0;

    strncpy(omniksol.Status, "Online", 6);
    omniksol.Status[6] = 0;
    
    // Get Omniksol Inverter DC Voltage
    omniksol.PVVoltageDC = server_reply[34] + (server_reply[33] * 256);
  
    // Get Omniksol Inverter DC Current
    omniksol.PVCurrentDC = server_reply[40] + (server_reply[39] * 256);
  
    // Get Omniksol Inverter AC Voltage
    omniksol.VoltageAC = server_reply[52] + (server_reply[51] * 256);
  
    // Get Omniksol Inverter AC Power
    omniksol.PowerAC = server_reply[60] + (server_reply[59] * 256);
  
    // Get Omniksol Inverter AC Current
    omniksol.CurrentAC = server_reply[46] + (server_reply[45] * 256);
  
    // Get Omniksol Inverter AC Frequency
    omniksol.FrequencyAC = server_reply[58] + (server_reply[57] * 256);
  
    // Get Omniksol Inverter Temperature
    omniksol.Temperature = server_reply[32] + (server_reply[31] * 256);

    // Get Omniksol Inverter Energy Today
    omniksol.EnergyToday = server_reply[70] + (server_reply[69] * 256);

    // Get Omniksol Inverter Total Energy
    omniksol.TotalEnergy = server_reply[74] + (server_reply[73] * 256) + (server_reply[72] * 65536) + (server_reply[71] * 16777216);

    // Get Omniksol Inverter Total Hours
    omniksol.TotalHours = server_reply[78] + (server_reply[77] * 256) + (server_reply[76] * 65536) + (server_reply[75] * 16777216);

    // Log to serial port for debugging
    // logSerial();
    
    // Send JSON to serial port
    sendOmniksolData();

  } else {
    displayLog("Invalid!");
  }
}

void logSerial() {

    Serial.println(F("-----------------------------------------------------------------------"));
    Serial.print(F("ID Inverter: "));
    Serial.println(omniksol.ID);
        
    Serial.print(F("Status Inverter: "));
    Serial.println(omniksol.Status);
    
    Serial.print(F("PV Voltage: "));
    Serial.print(float (omniksol.PVVoltageDC) / 10);
    Serial.println(F(" V"));
    
    Serial.print(F("PV Current: "));
    Serial.print(float (omniksol.PVCurrentDC) / 10);
    Serial.println(F(" A"));
    
    Serial.print(F("AC Voltage: "));
    Serial.print(float (omniksol.VoltageAC) / 10);
    Serial.println(F(" V"));
  
    Serial.print(F("AC Power: "));
    Serial.print(omniksol.PowerAC);
    Serial.println(F(" W"));
  
    Serial.print(F("AC Current: "));
    Serial.print(float (omniksol.CurrentAC) / 10);
    Serial.println(F(" A"));
  
    Serial.print(F("AC Frequency: "));
    Serial.print(float (omniksol.FrequencyAC) / 100);
    Serial.println(F(" Hertz"));
    
    Serial.print(F("Inverter Temperature: "));
    Serial.print(float (omniksol.Temperature) / 10);
    Serial.println(F(" °C"));

    Serial.print(F("Inverter Energy Today: "));
    Serial.print(float (omniksol.EnergyToday) / 100);
    Serial.println(F(" Wh"));

    Serial.print(F("Inverter Total Energy: "));
    Serial.print(float (omniksol.TotalEnergy) / 10);
    Serial.println(F(" kWh"));

    Serial.print(F("Inverter Total Hours: "));
    Serial.print(omniksol.TotalHours);
    Serial.println(F(" Hrs"));
    
    Serial.println(F("-----------------------------------------------------------------------"));
}

void setup(void) {
  
  Serial.begin(115200);
  
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.display();
  delay(1000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  displayLog("OMNIK HASS");
  displayLog("SENSOR  V1");

  // Create magic string from serialno.
  calcMagicMessage();
  
  // Clean WiFi state
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(1000);
}

void loop(void) {

  // Every 5 Seconds
  if ((millis()-screen_update)>5000) {
    screen++;
    if (screen > 1) {
      screen = 0;
    }
    screen_update = millis();
  }
  
  // Every 15 Seconds
  if ((millis()-omniksol_update)>15000) {
    omniksol_update = millis();
    
    getOmniksolData();
    
    // Check if connected to Omnik AP
    if (WiFi.status() == WL_CONNECTED) {
      drawOLED();
    } else {
      displayLog(" .OFFLINE.");
      
      // Send Offline JSON to serial port
      sendOmniksolDataOffline();

      // Test string
      // Serial.println("\{\"ID\"\:\"NLDN152013123456\",\"Status\"\:\"Offline\",\"PowerAC\"\:0,\"VoltageAC\"\:227.5,\"CurrentAC\"\:0.1,\"FrequencyAC\"\:49.99,\"PVVoltageDC\"\:131.7,\"PVCurrentDC\"\:0,\"EnergyToday\"\:0.14,\"TotalEnergy\"\:1875.5,\"TotalHours\"\:4264,\"Temperature\"\:21.3\}\n");
    }
  }
}