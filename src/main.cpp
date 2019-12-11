#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <vector>
#include <esp_wifi.h>
#include <EEPROM.h>

using namespace std;

#define EEPROM_SIZE 32
#define led 2

const char *default_ssid = "ESP32";
const char *default_password = "12345678";
char ssid[11], password[11];
const char ssidLoc = 0, passLoc = 10; 

String parameter[7];
int parameterCount = 0;

const int port = 9999;
WiFiServer server(port);

void writeMemory(char addr, char *data){
  int i;
  for(i=0; data[i]!='\0' && i<10; i++){
    EEPROM.write(addr+i, data[i]);
  }
  EEPROM.write(addr+i, '\0');
  EEPROM.commit();
}

void readMemory(char addr, char *data){
  int l = 0;
  char k = EEPROM.read(addr);
  while(k!='\0' && l<10){
    k = EEPROM.read(addr+l);
    data[l] = k;
    l++;
  }
  data[l] = '\0';
}

void getMetaData(){
  readMemory(ssidLoc, ssid);
  readMemory(passLoc, password);
}

void setMetaData(){
  writeMemory(ssidLoc, ssid);
  writeMemory(passLoc, password);
}

void WiFiEvent(WiFiEvent_t event){
    switch (event) {
        case SYSTEM_EVENT_AP_STACONNECTED:
            Serial.println("\nEvent: Client connected");
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            Serial.println("\nEvent: Client disconnected");
            //refactorNodeList();
            break;
    }
}

void separateParameters(String &body){
  int startI = 0, endI = 0, i;
  Serial.println();
  for(i=0; i<7; i++){
    parameter[i] = "";
    if(startI<body.length()){
      endI = body.indexOf('#', startI);
      parameter[i] = body.substring(startI, endI);
      Serial.println(parameter[i]);
      startI = endI+1;
      parameterCount++;
    }
  }
  Serial.print("PC: ");
  Serial.println(parameterCount);
}

void readPacket(WiFiClient client){
  String packetData = "", bodyLine = "", curLine = "";
  char c;
  int m = client.available();
  while(m!=0){
    //c = client.read();
    packetData.concat(client.read());
    m--;
  }

  Serial.println("Packet: ");
  Serial.println(packetData);
  int n, i;
  for(i=0; i<packetData.length(); i++){
    if(packetData[i] == '\n'){
      if(curLine.length() == 0){
        n = ++i;
        break;
      }else{
        curLine = "";
      }
    }else if(packetData[i] == '\r'){
      curLine += c;
    }
  }
  bodyLine = packetData.substring(n, packetData.length());
  Serial.print("Body Line: ");
  Serial.print(bodyLine);
  Serial.println("|");
  separateParameters(bodyLine);
}

void setup() {
  pinMode(led, OUTPUT);
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  delay(1000);
  getMetaData();
  
  WiFi.onEvent(WiFiEvent);

  Serial.println("Configuring AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 1, 0, 8);
  delay(100);
  IPAddress myIP(192, 168, 1, 1);
  IPAddress mask(255, 255, 255, 0);
  WiFi.softAPConfig(myIP, myIP, mask);

  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.softAPIP());
  server.begin();
  Serial.printf("Server Started: %d\n\n", port);
}

void loop() {

}