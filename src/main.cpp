#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <vector>
#include <esp_wifi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>

#define MAX_NODES 10
#define EEPROM_SIZE 48
#define led 2
#define rx 16
#define tx 17

using namespace std;


int red = 22, green = 23;
byte com = 0, temp = 0;
int nodeSelected = 0;
HardwareSerial voiceSerial(1);
String selectedNodeName = "";
int vrm = 0;

String url = "";

const char *default_ssid = "ESP32";
const char *default_password = "12345678";
char ssid[11], password[11];
const char ssidLoc = 0, passLoc = 10; 

String parameter[7];
String message;

const int port = 8080;
WebServer server(port);

int appNodeID = 0;

class Node{
  public:
    static int nodeCount;
    
    IPAddress ip;
    String nodeName;
    int id, type;
    bool conStat, relayStat, notifiedAppofDisconnect;
  
    Node(IPAddress ip, String nodeName, int type, bool relayStat = false){
      this->ip = ip;
      this->nodeName = nodeName;
      this->type = type;
      this->relayStat = relayStat;
      conStat = true;
      notifiedAppofDisconnect = false;
    }
};

int Node::nodeCount = 0;
vector<Node> nodes;
vector<Node>::iterator nodeIterator;

void blink(int times){
  for(int i=0; i<times; i++){
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(500);
  }
}

void blinkRed(){
  digitalWrite(red, HIGH);
  delay(100);
  digitalWrite(red, LOW);
}

void blinkGreen(){
  digitalWrite(green, HIGH);
  delay(100);
  digitalWrite(green, LOW);
}

void printNodeList(){
  Serial.printf("Node Count: %d", Node::nodeCount);
  Serial.printf("\n%3s%10s%6s%6s%10s%6s\n", "ID", "IP", "CStat", "Rstat", "Name", "Type");
  Serial.println("---------------------------------------------");
  if(Node::nodeCount > 0){
    for(nodeIterator = nodes.begin(); nodeIterator<nodes.end(); nodeIterator++){
      Serial.print(nodeIterator->id);
      Serial.print("  ");
      Serial.print(nodeIterator->ip);
      Serial.print("    ");
      Serial.print(nodeIterator->conStat);
      Serial.print("    ");
      Serial.print(nodeIterator->relayStat);
      Serial.print("  ");
      Serial.printf("%10s", nodeIterator->nodeName.c_str());
      Serial.print("  ");
      Serial.println(nodeIterator->type);
    }
  }
  Serial.println("---------------------------------------------");
  Serial.print("App Node ID: ");
  Serial.println(appNodeID);
}

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

void restartDevice(){
  Serial.println("Restarting....");
  esp_restart();
}

void separateParameters(String &input){
  int startI = 0, endI = 0, i;
  for(i=0; i<7; i++){
    parameter[i] = "";
    if(startI<input.length()){
      endI = input.indexOf('$', startI);
      parameter[i] = input.substring(startI, endI);
      startI = endI+1;
    }
  }
}

void sendReply(String message){
  Serial.print("Replying with: ");
  Serial.println(message);
  server.send(200, "text/plain", message);
}

void sendPacket(IPAddress ip, int port, String &message){
  HTTPClient client;
  url = "http://";
  url.concat(ip.toString());
  url.concat(":8080/message?data=");
  url.concat(message);
  Serial.print("URL: ");
  Serial.println(url);
  client.begin(url);
  
  int httpCode = client.GET();
  if(httpCode > 0){
    if(httpCode == HTTP_CODE_OK){
      Serial.printf("Request Sent, HTTPCODE: %d\n", httpCode);
    }
  }else{
    Serial.print("HTTP GET Error: ");
    Serial.println(client.errorToString(httpCode));
  }
  client.end();
}

void sendAPConfig(){
  message = "client@esp$action@apconfig$";
  message.concat(ssid);
  message.concat("$");
  message.concat(password);
  message.concat("$");

  for(nodeIterator = nodes.begin(); nodeIterator<nodes.end(); nodeIterator++){
    //only send apconfig to slave nodes, type = 2 and 3 in future
    if(nodeIterator->conStat && nodeIterator->type == 2){
      sendPacket(nodeIterator->ip, port, message);
    }
  }
}

void sendNodeStat(int nodeID, int toType){
  //toType = 0, send to APP
  //toType = 2, send to Node
  IPAddress toIP;
  if(toType == 0){
    toIP = nodes[appNodeID-1].ip;
  }
  else if(toType == 2){
    toIP = nodes[nodeID-1].ip;
  }

  message = "client@esp$action@stat$1$";
  message.concat(nodeID);
  message.concat("$");
  message.concat(nodes[nodeID-1].nodeName);
  message.concat("$");
  message.concat(nodes[nodeID-1].conStat);
  message.concat("$");
  message.concat(nodes[nodeID-1].relayStat);
  message.concat("$");

  sendPacket(toIP, port, message);
}

void sendNodeListToApp(){
  for(int i=0; i<Node::nodeCount; i++){
    if(nodes[i].id != appNodeID){
      sendNodeStat(i+1, 0);
      delay(500);
    }
  }
}

void setNodeStat(){
  int nodeId = parameter[3].toInt();
  int index = nodeId - 1;

  if(index < Node::nodeCount && index>=0){
    nodes[index].relayStat = parameter[6].toInt();
    nodes[index].conStat = parameter[5].toInt();
    nodes[index].nodeName = parameter[4];
    sendReply("ESP: NodeStat RCVD");

  printNodeList();
  if(parameter[2].toInt() == 0){
    sendNodeStat(nodeId, 2);
  }else if(parameter[2].toInt() == 2 && appNodeID!=0)
    sendNodeStat(nodeId, 0);
  }else{
    sendReply("ESP: Invalid ID");
  }
  
}

void refactorNodeList(){
  IPAddress addr;
  wifi_sta_list_t staList;
  tcpip_adapter_sta_list_t adapter;

  memset(&staList, 0, sizeof(staList));
  memset(&adapter, 0, sizeof(adapter));

  esp_wifi_ap_get_sta_list(&staList);
  tcpip_adapter_get_sta_list(&staList, &adapter);

  for(int i=0; i<Node::nodeCount; i++){
    bool active = false;
    for(int j=0; j<adapter.num; j++){
      tcpip_adapter_sta_info_t station = adapter.sta[j];
      addr.fromString(ip4addr_ntoa(&station.ip));
      Serial.print(nodes[i].ip);
      if(nodes[i].ip == addr){
        active = true;
        break;
      }
    }
    if(!active){
      if(nodes[i].type==0)
        appNodeID = 0;
      nodes[i].conStat = false;
      if(appNodeID!=0 && !nodes[i].notifiedAppofDisconnect)
      {
        nodes[i].notifiedAppofDisconnect = true;
        sendNodeStat(i+1, 0);
      }
    }
  }
  printNodeList();
}

void WiFiEvent(WiFiEvent_t event){
    switch (event) {
        case SYSTEM_EVENT_AP_STACONNECTED:
            Serial.println("\nEvent: Client connected");
            blink(2);
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            Serial.println("\nEvent: Client disconnected");
            blink(3);
            refactorNodeList();
            break;
    }
}

void nodeConfig(IPAddress clientIP){
  bool added = false;
  int type = parameter[2].toInt();
  int mayBeID = 0;
  Serial.print("New Client IP: ");
  Serial.println(clientIP);
  
  Node newNode(clientIP, parameter[4], type, parameter[6].toInt());

  for(int i=0; i<Node::nodeCount; i++){
    if(clientIP == nodes[i].ip){
      newNode.id = i+1;
      nodes[i].relayStat = parameter[6].toInt();
      nodes[i].conStat = 1;
      nodes[i].nodeName = parameter[4];
      added = true;
      break;
    }
    if(!nodes[i].conStat)
      mayBeID = i+1;
  }
  if(!added){
    added = false;
    if(mayBeID!=0){
      newNode.id = mayBeID;
      newNode.conStat = 1;
      nodes[mayBeID - 1] = newNode;
      Node::nodeCount++;
      added = true;
    }
    if(!added){
      Node::nodeCount++;
      newNode.id = Node::nodeCount;
      newNode.conStat = 1;
      nodes.push_back(newNode);
      added = true;
    }
  }

  message = "client@esp$action@config$1$";
  message.concat(newNode.id);
  message.concat("$ESP$");
  message.concat(newNode.conStat);
  message.concat("$");
  message.concat(newNode.relayStat);
  message.concat("$");

  sendReply("ESP32: Config RQST RCVD");
  delay(500);
  sendPacket(clientIP, port, message);

  if(type == 2 && appNodeID!=0)
    sendNodeStat(newNode.id, 0);
  else if(type == 0)
    appNodeID = newNode.id;

  printNodeList();
}

void resetDevice(){
  sendReply("Resetting Meta");
  strcpy(ssid, default_ssid);
  strcpy(password, default_password);
  setMetaData();
  sendAPConfig();
  restartDevice();
}

void parameterDecode(){

  if(parameter[1].equals("action@stat")){
      setNodeStat();
  }
  else if(parameter[1].equals("action@config")){
    Serial.println("Node Config Request");
    if(Node::nodeCount < MAX_NODES){
      WiFiClient client = server.client();
      IPAddress clientIP = client.remoteIP();
      nodeConfig(clientIP);
    }else{
      Serial.println("Node List Full");
      server.send(200, "text/plain", "ESP: Node List Full");
    } 
  }else if(parameter[1].equals("action@getnodelist")){
    sendReply("ESP: Request for Node List RCVD");
    sendNodeListToApp();
  }
  else if(parameter[1].equals("action@reset") && parameter[0].equals("client@app")){
    resetDevice();
  }else if(parameter[1].equals("action@apconfig") && parameter[0].equals("client@app")){
      sendReply("ESP: APConfig Received");
      strcpy(ssid, parameter[2].c_str());
      strcpy(password, parameter[3].c_str());
      setMetaData();
      sendAPConfig();
      restartDevice();
  }
}

void handleRoot(){
  Serial.println("Root page accessed by a client!");
  server.send ( 200, "text/plain", "ESP: Hello, you are at root!");
}

void handleNotFound(){
  server.send ( 404, "text/plain", "ESP: 404, No resource found");
}

void handleMessage(){
  blink(1);
  if(server.hasArg("data")){
    message = server.arg("data");
    separateParameters(message);
    parameterDecode();
  }else{
    server.send(200, "text/plain", "ESP: Message Without Body");
  }
}

//------------------------------------------

void setNodeStatOf(String name, int rStat){
  for(int i = 0; i<Node::nodeCount; i++){
    if(nodes[i].conStat && nodes[i].nodeName.equals(name)){
      nodes[i].relayStat = rStat;
      sendNodeStat(i+1, 2);
      if(appNodeID!=0)
        sendNodeStat(i+1, 0);
      //break;
    }
  }
}

void waitingState(){
  voiceSerial.write(0xAA);
  voiceSerial.write((byte)0x00);
  while(!voiceSerial.available());
  while(voiceSerial.available()){
    Serial.print("Wait Return: ");
    Serial.println(voiceSerial.read(), HEX);
  }
}

void importG1(){
  int flag = 1;
  unsigned long i = 0;
  waitingState();
  while(flag){
    i = millis();
    
    voiceSerial.write(0xAA);
    voiceSerial.write(0x21);
    
    
    while(!voiceSerial.available() && millis()-i<100);
  
    
    if(voiceSerial.available()){
      temp = voiceSerial.read();
      if(temp == 0xCC)
      {  
        flag = 0;
        i = millis() - i;
        Serial.print("Imported 1: ");
        Serial.print(i);
        Serial.println(" msec");
        blinkGreen();
      }
    }
  }
}

void importG2(){
  int flag = 1;
  unsigned long i = 0;
  
  waitingState();
  
  while(flag){
    i = millis();
    
    voiceSerial.write(0xAA);
    voiceSerial.write(0x22);
    
    while(!voiceSerial.available() && millis()-i<100);
    
    if(voiceSerial.available()){
      temp = voiceSerial.read();
      if(temp == 0xCC)
      {  
        flag = 0;
        i = millis() - i;
        Serial.print("Imported 2: ");
        Serial.print(i);
        Serial.println(" msec");
        blinkGreen();
      }
    }
  }
}

void selectActivity(){
  importG2();

  unsigned long i = millis();
  while(!voiceSerial.available() && (millis()-i<3000));
  if(voiceSerial.available()){
    com = voiceSerial.read();
    Serial.print("Voice Com 2: ");
    Serial.println(com, HEX);

    switch(com){
      case 0x11:
        setNodeStatOf(selectedNodeName, 1);
        break;
      case 0x12:
        setNodeStatOf(selectedNodeName, 0);
        break;
      default:
        break;
    }
  }
  else{
    Serial.println("No Command");
    blinkRed();
  }
}

//------------------------------------------


void setup() {
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(led, OUTPUT);
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  delay(400);
  getMetaData();
  
  voiceSerial.begin(9600, SERIAL_8N1, rx, tx);
  delay(300);
  voiceSerial.write(0xAA);
  voiceSerial.write(0x37);

  Serial.println("Wrote");
  delay(1000);

  if(voiceSerial.available()){
    while(voiceSerial.available())
      voiceSerial.read();

    Serial.println("Importing");
    vrm = 1;
    importG1();
  }

  

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
  server.on("/", handleRoot);
  server.on("/message", handleMessage);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("Server Started: %d\n\n", port);
}

void loop() {
  server.handleClient();
  
  while(voiceSerial.available() && vrm == 1){
    com = voiceSerial.read();
    Serial.print("Voice Com 1: ");
    Serial.println(com, HEX);

    switch(com){
      case 0x11:
        selectedNodeName = "Light";
        nodeSelected = 1;
        break;
      case 0x12:
        selectedNodeName = "Fan";
        nodeSelected = 1;
        break;
      case 0x13:
        selectedNodeName = "Tv";
        nodeSelected = 1;
        break;
      case 0x14:
        selectedNodeName = "AC";
        nodeSelected = 1;
        break;
      case 0x15:
        nodeSelected = 0;
        break;
      default:
        importG1();
        break;
    }

    if(nodeSelected!=0){
      selectActivity();
      importG1();
      nodeSelected = 0;
    }
  }
}

