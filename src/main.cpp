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

const int port = 8080;
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

void blink(int times){
  for(int i=0; i<times; i++){
    digitalWrite(led, HIGH);
    delay(500);
    digitalWrite(led, LOW);
    delay(300);
  }
}

void printNodeList(){
  Serial.printf("Node Count: %d", Node::nodeCount);
  Serial.printf("\n%3s%3s%6s%6s%10s%6s\n", "ID", "IP", "CStat", "Rstat", "Name", "Type");
  Serial.println("---------------------------------------------");
  if(Node::nodeCount > 0){
    for(nodeIterator = nodes.begin(); nodeIterator<nodes.end(); nodeIterator++){
      Serial.print(nodeIterator->id);
      Serial.print("  ");
      Serial.print(nodeIterator->ip);
      Serial.print("  ");
      Serial.print(nodeIterator->conStat);
      Serial.print("  ");
      Serial.print(nodeIterator->relayStat);
      Serial.print("  ");
      Serial.print(nodeIterator->nodeName);
      Serial.print("  ");
      Serial.println(nodeIterator->type);
    }
  }
  
  Serial.println("---------------------------------------------");
  Serial.print("App Node Index: ");
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
  delay(1000);
  esp_restart();
}

void separateParameters(String &input){
  parameterCount = 0;
  int startI = 0, endI = 0, i;
  Serial.println();
  for(i=0; i<7; i++){
    parameter[i] = "";
    if(startI<input.length()){
      endI = input.indexOf('$', startI);
      parameter[i] = input.substring(startI, endI);
      startI = endI+1;
      parameterCount++;
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
    toIP = nodes[nodeID].ip;
  }

  message = "client@esp$action@stat$1$";
  message.concat(nodeID);
  message.concat("$");
  message.concat(nodes[nodeID].nodeName);
  message.concat("$");
  message.concat(nodes[nodeID].conStat);
  message.concat("$");
  message.concat(nodes[nodeID].relayStat);
  message.concat("$");

  sendPacket(toIP, port, message);
}

void setNodeStat(){
  int nodeId = parameter[3].toInt();
  int index = nodeId - 1;
  
  if(index < Node::nodeCount && index>=0){
    nodes[index].relayStat = parameter[6].toInt();
    nodes[index].conStat = parameter[5].toInt();
    nodes[index].nodeName = parameter[4];

    sendReply("ESP: NodeStat RCVD");
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
    //Serial.print("Node IP: ");
      //Serial.print(nodes[i].ip);
    for(int j=0; j<adapter.num; j++){
      tcpip_adapter_sta_info_t station = adapter.sta[j];
      addr.fromString(ip4addr_ntoa(&station.ip));
      Serial.print(nodes[i].ip);
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
      if(appNodeID!=0)
        sendNodeStat(i+1, 0);
    }
  }
  printNodeList();
}

void WiFiEvent(WiFiEvent_t event){
    switch (event) {
        case SYSTEM_EVENT_AP_STACONNECTED:
            Serial.println("\nEvent: Client connected");
            blink(2);
            //refactorNodeList();
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
  Node newNode(clientIP, parameter[4], type, parameter[6].toInt());

  for(int i=0; i<Node::nodeCount; i++){
    if(!nodes[i].conStat){
      Serial.printf("\nAssigning ID: %d", i+1);
      newNode.id = i+1;
      nodes[i] = newNode;
      Node::nodeCount++;
      added = true;
      break;
    }
  }
  if(!added){
    Node::nodeCount++;
    Serial.printf("\nAssigning ID: %d", Node::nodeCount);
    newNode.id = Node::nodeCount;
    nodes.push_back(newNode);
    added = true;
  }

  message = "client@esp$action@config$1$";
  message.concat(newNode.id);
  message.concat("$ESP$");
  message.concat(newNode.conStat);
  message.concat("$");
  message.concat(newNode.relayStat);
  message.concat("$");

  //Only send okay as reply.
  sendReply("ESP32: Config RQST RCVD");
  delay(1000);
  //Send the config packet as new http request
  sendPacket(clientIP, port, message);

  if(type == 2 && appNodeID!=0){
    sendNodeStat(newNode.id, 0);
  }else if(type == 0){
    appNodeID = newNode.id;
    //sendNodeListToApp();
  }
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
        server.send(200, "text/plain", "Node List Full");
      }
      
    }
    else if(parameter[1].equals("action@reset") && parameter[0].equals("client@app")){
      resetDevice();
    }else if(parameter[1].equals("action@apconfig") && parameter[0].equals("client@app")){
        strcpy(ssid, parameter[2].c_str());
        strcpy(password, parameter[3].c_str());
        setMetaData();
        sendAPConfig();
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
  blink(1);
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