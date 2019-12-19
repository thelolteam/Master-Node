#include <Arduino.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <vector>
#include <esp_wifi.h>
#include <EEPROM.h>
#include <WebServer.h>
#include <HTTPClient.h>

#define MAX_NODES 10
#define EEPROM_SIZE 48
#define led 2

using namespace std;

String url = "";

const char *default_ssid = "ESP32";
const char *default_password = "12345678";
char ssid[11], password[11];
const char ssidLoc = 0, passLoc = 10; 

String parameter[7];
int parameterCount = 0;
String message;

const int port = 80;
WebServer server(port);

int appNodeID = 0;

class Node{
  public:
    static int nodeCount;
    
    IPAddress ip;
    String nodeName;
    int id, type;
    bool conStat, relayStat;
  
    Node(IPAddress ip, String nodeName, int type, bool relayStat = false){
      this->ip = ip;
      this->nodeName = nodeName;
      this->type = type;
      this->relayStat = relayStat;
      conStat = true;
    }

    Node(){}
};

int Node::nodeCount = 0;
vector<Node> nodes;
vector<Node>::iterator nodeIterator;

void printNodeList(){
  Serial.print("Vector Size: ");
  Serial.println(nodes.size());
  Serial.print("Vector Capacity: ");
  Serial.println(nodes.capacity());
  Serial.print("Vector MAX Size: ");
  Serial.println(nodes.max_size());

  Serial.printf("Node Count: %d", Node::nodeCount);
  Serial.printf("\n%5s%10s%6s%6s%10s%6s\n", "ID", "IP", "CStat", "Rstat", "Name", "Type");
  Serial.println("-----------------------------------------------------------------------");
  for(nodeIterator = nodes.begin(); nodeIterator<nodes.end(); nodeIterator++){
    Serial.printf("%5s%10s%6s%6s%10s%6s\n", nodeIterator->id, nodeIterator->ip, nodeIterator->conStat, nodeIterator->relayStat, nodeIterator->nodeName, nodeIterator->type);
  }
  Serial.println("-----------------------------------------------------------------------");
  Serial.print("App Node Index: ");
  Serial.println(appNodeID);
  Serial.println();
}

void refactorNodeList(){
  Serial.println("Someone Bailed on Us, Refactoring Node List----------------------");
  IPAddress addr;
  wifi_sta_list_t staList;
  tcpip_adapter_sta_list_t adapter;

  memset(&staList, 0, sizeof(staList));
  memset(&adapter, 0, sizeof(adapter));

  esp_wifi_ap_get_sta_list(&staList);
  tcpip_adapter_get_sta_list(&staList, &adapter);

  for(int i=0; i<Node::nodeCount; i++){
    bool active = false;
    Serial.print("Node IP: ");
      Serial.print(nodes[i].ip);
    for(int j=0; j<adapter.num; j++){
      tcpip_adapter_sta_info_t station = adapter.sta[j];
      addr.fromString(ip4addr_ntoa(&station.ip));
      
      if(nodes[i].ip == addr){
        Serial.println(" Present");
        active = true;
        break;
      }
    }
    if(!active){
      Serial.println(" Absent");
      if(nodes[i].type==0)
        appNodeID = 0;
      nodes[i].conStat = false;
      //if(appNodeIndex!=-1)
        //sendNodeStat(i+1, 0);
    }
  }
  Serial.println("\nNode List Refactored!!!");
  printNodeList();
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

void WiFiEvent(WiFiEvent_t event){
    switch (event) {
        case SYSTEM_EVENT_AP_STACONNECTED:
            Serial.println("\nEvent: Client connected");
            break;
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            Serial.println("\nEvent: Client disconnected");
            refactorNodeList();
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

void sendPacket(IPAddress ip, int port, String &message){
  HTTPClient client;
  url = "http://";
  url.concat(ip);
  url.concat("/message?data=");
  url.concat(message);

  Serial.print("URL: ");
  Serial.println(url);
  client.begin(url);

  int httpCode = client.GET();
  if(httpCode > 0){
    if(httpCode == HTTP_CODE_OK){
      Serial.println("Request Sent");
    }
  }else{
    Serial.println("HTTP GET Error");
  }
  client.end();
}

void sendReply(String &message){
  Serial.println("Replying: ");
  Serial.println(message);
  server.send(200, "text/plain", message);
}

void sendNodeStat(int nodeID, int toType){
  //toType = 0, send to APP
  //toType = 2, send to Node
  IPAddress toIP;
  if(toType == 0){
    toIP = nodes[appNodeID-1].ip;
  }
  else if(toType == 2){
    toIP = nodes[nodeID].ip;
  }

  message = "client@esp#action@stat#1#";
  message.concat(nodeID);
  message.concat("#");
  message.concat(nodes[nodeID].nodeName);
  message.concat("#");
  message.concat(nodes[nodeID].conStat);
  message.concat("#");
  message.concat(nodes[nodeID].relayStat);
  message.concat("#");

  sendPacket(toIP, port, message);
}

void setNodeStat(){
  int nodeId = parameter[3].toInt();
  int index = nodeId - 1;
  
  nodes[index].relayStat = parameter[6].toInt();
  nodes[index].conStat = parameter[5].toInt();
  nodes[index].nodeName = parameter[4];

  if(parameter[2].toInt() == 0){
    sendNodeStat(nodeId, 2);
  }else if(parameter[2].toInt() == 2)
    sendNodeStat(nodeId, 0);
}

void nodeConfig(IPAddress clientIP){
  bool added = false;
  int type = parameter[2].toInt();
  Node newNode(clientIP, parameter[4], type, parameter[5].toInt());

  for(int i=0; i<Node::nodeCount; i++){
    if(!nodes[i].conStat){
      Serial.printf("Assigning ID: %d", i+1);
      newNode.id = i+1;
      nodes[i] = newNode;
      Node::nodeCount++;
      added = true;
      break;
    }
  }
  if(!added){
    Node::nodeCount++;
    Serial.printf("Assigning ID: %d", Node::nodeCount);
    newNode.id = Node::nodeCount;
    nodes.push_back(newNode);
    added = true;
  }

  message = "client@esp#action@config#1#";
  //message = "HTTP/1.1 200 OK\n\nclient@esp#client@config#1#";
  message.concat(newNode.id);
  message.concat("#ESP#");
  message.concat(newNode.conStat);
  message.concat("#");
  message.concat(newNode.relayStat);
  message.concat("#");

  sendReply(message);

  if(type == 2){
    sendNodeStat(newNode.id, 0);
  }else if(type == 0){
    appNodeID = newNode.id;
    //sendNodeListToApp();
  }
  printNodeList();
}

void restartDevice(){
  Serial.println("Restarting....");
  delay(1000);
  esp_restart();
}

void resetDevice(){
  strcpy(ssid, default_ssid);
  strcpy(password, default_password);
  setMetaData();
  restartDevice();
}

void parameterDecode(){
  if(parameter[1].equals("action@stat")){
      setNodeStat();
    }
    else if(parameter[1].equals("action@config")){
      WiFiClient client = server.client();
      IPAddress clientIP = client.remoteIP();
      nodeConfig(clientIP);
    }
    else if(parameter[1].equals("action@reset")){
      resetDevice();
    }else if(parameter[1].equals("action@apconfig")){

    }
}

void handleRoot(){
  Serial.println("Root page accessed by a client!");
  server.send ( 200, "text/plain", "Hello, you are at root!");
}


void handleNotFound(){
  server.send ( 404, "text/plain", "404, No resource found");
}

void handleMessage(){
  if(server.hasArg("data")){
    message = server.arg("data");
    separateParameters(message);
    parameterDecode();
  }else{
    server.send(200, "text/plain", "Message Without Body");
  }
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
  server.on("/", handleRoot);
  server.on("/message", handleMessage);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.printf("Server Started: %d\n\n", port);
}


void loop() {
  server.handleClient();
}