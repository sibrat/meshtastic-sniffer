#define HELTECV2	//supported boards: DIYV1, HELTECV2, TTGO
#define MQTT  // enable sending to mqtt server
#define UDPENABLED   // enable sending via UDP broadcast
const uint16_t syncWord = 0x2b;
const int udpPort = 3303; // udp port for good packets
const int udpPortBad = 3302; // udp port for bad crc packets.

const int LoRaPsz = 240;
#ifdef TTGO
  #define SX1276chip
  #define SCK 5
  #define MISO 19
  #define MOSI 27
  #define CS 18
  #define RST 23
  #define DIO0 26
#endif

#ifdef HELTECV2
  #define SX1276chip
  #define SCK 5
  #define MISO 19
  #define MOSI 27
  #define CS 18
  #define RST 14
  #define DIO0 26
  #endif

#ifdef DIYV1
  #define SX1262chip
  #define LORA_RX_PIN 14
  #define LORA_TX_PIN 13
  #define CS 18
  #define SCK 5
  #define MOSI 27
  #define MISO 19
  #define BUSY 32
  #define RST 23
  #define DIO1 33
#endif

#ifdef SX1262chip
  #include <SX126x.h> // https://github.com/chandrawi/LoRaRF-Arduino
  SX126x LoRa;
#endif

#ifdef SX1276chip
  #include <SX127x.h> // https://github.com/chandrawi/LoRaRF-Arduino
  SX127x LoRa;
#endif

#include <WiFi.h>
#include <NetworkUdp.h>
#include "base64.hpp"
#include "pb_common.h"
#include "pb.h"
#include "pb_encode.h"
#include "meshpacket.pb.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_mac.h"

Preferences preferences;
IPAddress udpAddress;
boolean connected = false;
NetworkUDP udp;

char packetBuffer[LoRaPsz + 1];  // buffer to hold incoming packet,
char read_line_buf[1024];
int read_line_buf_pos;
uint32_t seen[64];    //array of already seen packets ids
int seen_pos;
char nodeid[9];
#ifdef MQTT
  #include <MQTT.h>
  WiFiClient net;
  MQTTClient mqtt;
  char mqttTopic[64];
  char mqttChannel[16];
  char mqttId[10];
#endif

void setup() {
  Serial.begin(115200);
  Serial.print("Meshtastic receiver ");
  uint8_t mac[6];
  if (ESP_OK == esp_efuse_mac_get_default(mac)){
    sprintf(nodeid, "!%02x%02x%02x%02x", mac[2], mac[3], mac[4], mac[5]);
  }
  Serial.println(nodeid);
  preferences.begin("wifi", true); // namespace, readonly
  bool doesExist = preferences.isKey("ssid");
  if (doesExist == false) {
    Serial.println("No WiFi settings!");
    Serial.println("Please set WiFi credentials:");
    Serial.println("{\"jsonrpc\": \"2.0\", \"method\": \"set_wifi_creds\", \"params\": {\"ssid\": \"your_ssid\", \"pass\": \"your_password\"}, \"id\":1}");
  }else{
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    if(ssid != ""){
      connectToWiFi(ssid.c_str(), password.c_str());
    }
  }
  preferences.end();

  preferences.begin("lora", true); // namespace, readonly 
  uint32_t loraFreq = preferences.getUInt("freq", 868825000);
  uint8_t loraSF = preferences.getUChar("SF", 11);
  uint32_t loraBW = preferences.getUInt("BW", 250000);
  uint8_t loraCR = preferences.getUChar("CR", 5);
  preferences.end();
  Serial.println("LoRa Settings:");
  Serial.println("Frequency: " + String(loraFreq) + " Hz");
  Serial.println("Spreading factor: " + String(loraSF));
  Serial.println("Bandwidth: " + String(loraBW) + " Hz");
  Serial.println("Coding rate: " + String(loraCR));
  Serial.println();
  Serial.println("mac address: " + WiFi.macAddress());
  SPI.begin(SCK,MISO,MOSI,CS);
  LoRa.setSPI(SPI,8000000);
#ifdef MQTT
  preferences.begin("mqtt", true);
  String host = preferences.getString("host", "");
  mqtt.begin(host.c_str(), net);
  String root = preferences.getString("rootTopic", "");
  preferences.getString("channel", "").toCharArray(mqttChannel, sizeof(mqttChannel));
  strcpy(mqttTopic,root.c_str());
  strcat(mqttTopic, "/e/");
  strcat(mqttTopic, mqttChannel);
  strcat(mqttTopic, "/");
  strcat(mqttTopic, mqttId);
  preferences.end();
#endif
#ifdef SX1276chip
  LoRa.begin(CS,RST,DIO0,-1,-1);
  LoRa.setRxGain(LORA_RX_GAIN_BOOSTED,true);
#endif
#ifdef SX1262chip
  if (!LoRa.begin(CS,RST,BUSY,DIO1,LORA_TX_PIN,LORA_RX_PIN)){
    Serial.println("Error: Can't init LoRa radio");
    while(1);
  }
  LoRa.setRxGain(SX126X_RX_GAIN_BOOSTED);
//  LoRa.setRxGain(LORA_RX_GAIN_POWER_SAVING);
  LoRa.setDio2RfSwitch(true);
  LoRa.setDio3TcxoCtrl(SX126X_DIO3_OUTPUT_1_8,SX126X_TCXO_DELAY_10);
//  LoRa.setRegulator(SX126X_REGULATOR_LDO);
#endif
  LoRa.setFrequency(loraFreq);
  LoRa.setLoRaModulation(loraSF,loraBW,loraCR,false);
  LoRa.setLoRaPacket(LORA_HEADER_EXPLICIT,16,LoRaPsz,true,false);
  LoRa.setSyncWord(syncWord);
#ifdef SX1276chip
  LoRa.request(SX127X_RX_CONTINUOUS);
#endif
#ifdef SX1262chip
  LoRa.request(SX126X_RX_CONTINUOUS);
#endif
  seen_pos = 0;
}

void loop() {
  handleJsonRpc();
#ifdef MQTT
  mqtt.loop();
    if (!mqtt.connected()) {
      MQTTconnect();
    }
#endif
  uint8_t length=LoRa.available();
  if (length) {
    LoRa.wait();
    length=LoRa.available();
    char message[length];
    LoRa.read(message,length);

    uint8_t protobuffer[256];
    pb_ostream_t stream = pb_ostream_from_buffer(protobuffer, sizeof(protobuffer));
    MeshPacket packet = MeshPacket_init_zero;
    packet.to=(message[3]<<24)+(message[2]<<16)+(message[1]<<8)+(message[0]);
    packet.from=(message[7]<<24)+(message[6]<<16)+(message[5]<<8)+(message[4]);
    uint32_t pid = (message[11]<<24)+(message[10]<<16)+(message[9]<<8)+(message[8]);
    packet.id = pid;
    packet.hop_limit=message[12]&0b00000111;
    packet.want_ack=(message[12]&0b00001000)>>3;
    packet.via_mqtt=(message[12]&0b00010000)>>4;
    packet.hop_start=(message[12]&0b11100000)>>5;
    packet.channel=message[13];
    packet.next_hop=message[14];
    packet.relay_node=message[15];
    packet.rx_rssi = LoRa.packetRssi();
    packet.rx_snr = LoRa.snr();
    packet.transport_mechanism = 1;
    packet.encrypted.size = length-16;
    for (int i = 16; i <= length; i++) {
        packet.encrypted.bytes[i-16] = message[i];
    }
    uint8_t status = LoRa.status();
//
    bool is_seen = false;
    for (int i = 0; i < sizeof(seen) / sizeof(seen[0]); i++) {
        if (pid == seen[i]){
            is_seen = true;
        }
    }
    if (!is_seen){
      seen[seen_pos] = pid;
      seen_pos++;
      if (seen_pos >= sizeof(seen) / sizeof(seen[0])){
        seen_pos = 0;
      }
    }

// encode meshpacket protobuf:
    bool s = pb_encode(&stream, MeshPacket_fields, &packet);
    if (!s){
      Serial.println("Error: Failed to encode meshpacket protobuf");
      return;
    }
// dump encoded protobuf to console as json:
    unsigned char base64[512];
#ifdef MQTT
    uint8_t protobufferMQTT[sizeof(protobuffer)+sizeof(mqttTopic)+sizeof(mqttId)];
    pb_ostream_t streamMQTT = pb_ostream_from_buffer(protobufferMQTT, sizeof(protobufferMQTT));
    ServiceEnvelope MQTTmsg = ServiceEnvelope_init_zero;
    strcpy(MQTTmsg.channel_id,mqttChannel);
    strcpy(MQTTmsg.gateway_id,mqttId);
    MQTTmsg.packet = packet;
    MQTTmsg.has_packet = 1;
    s = pb_encode(&streamMQTT, ServiceEnvelope_fields, &MQTTmsg);
    if (!s){
      Serial.println("Error: Failed to encode MQTT protobuf");
      Serial.println(String (MQTTmsg.gateway_id));
      return;
    }
    unsigned int base64_length = encode_base64(protobufferMQTT,streamMQTT.bytes_written, base64);
#else
    unsigned int base64_length = encode_base64(protobuffer,stream.bytes_written, base64);
#endif
    JsonDocument json;
    json["status"] = status;
    json["packet"] = (char *) base64;
    char buf[1024] = {0};
    serializeJson(json, buf, sizeof(buf));
    Serial.println(buf);
//
    if (connected) {
#ifdef MQTT
      if (mqtt.connected() and !is_seen and status == 7) {
        mqtt.publish(mqttTopic, (char *)protobufferMQTT, streamMQTT.bytes_written, true, 0);
      }
#endif
#ifdef UDPENABLED
      if (status==7){
        udp.beginPacket(udpAddress,udpPort);
      }else{ // status==9, bad crc
        udp.beginPacket(udpAddress,udpPortBad);
      }
      udp.write(protobuffer,stream.bytes_written); //send meshtastic packet protobuff 
//      udp.write(protobufferMQTT,streamMQTT.bytes_written); //we can send in mqtt protobuff if we want to...
      udp.endPacket();
#endif
    }
  }
}

void connectToWiFi(const char *ssid, const char *pwd) {
  Serial.println("Connecting to WiFi network: " + String(ssid));
  // delete old config
  WiFi.disconnect(true);
  //register event handler
  WiFi.onEvent(WiFiEvent);  // Will call WiFiEvent() from another thread.
  //Initiate connection
  WiFi.enableIPv6(false);
  WiFi.begin(ssid, pwd);
  Serial.println("Waiting for WIFI connection...");
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("WiFi connected! IP address: ");
      Serial.println(WiFi.localIP());
      udp.begin(WiFi.localIP(), udpPort);
      udpAddress = getBroadcastAddress();
      connected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("WiFi lost connection");
      connected = false;
      break;
    default: break;
  }
}

IPAddress getBroadcastAddress() {
    IPAddress ip = WiFi.localIP();
    IPAddress subnet = WiFi.subnetMask();
    IPAddress broadcast;
    for (int i = 0; i < 4; i++) {
        broadcast[i] = ip[i] | (~subnet[i]);
    }
    return broadcast;
}

void handleJsonRpc(){
	char buf[1024] = {0};
	uint32_t l = readLine(buf);
	if (l == 0){return;}
  JsonDocument resDoc;
  JsonDocument reqDoc;
  resDoc["jsonrpc"] = "2.0";
  DeserializationError error = deserializeJson(reqDoc, buf);
  if (reqDoc["jsonrpc"] == "2.0"){
    resDoc["id"] = reqDoc["id"];
  }else{
    resDoc["id"] = nullptr;
    JsonObject errorObj = resDoc.createNestedObject("error");
    errorObj["code"] = -32600;
    errorObj["message"] = "Invalid Request";
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
    return;
  }
  if (reqDoc["method"] == "setup_wifi"){
    JsonObject params = reqDoc["params"];
    const char *ssid = params["ssid"];
    const char *passwd = params["pass"];
    if (ssid == NULL || passwd == NULL){
      JsonObject errorObj = resDoc.createNestedObject("error");
      errorObj["code"] = -32602;
      errorObj["message"] = "Invalid params";
      serializeJson(resDoc, buf, sizeof(buf));
      Serial.println(buf);
      return;
    }
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", passwd);
    preferences.end();
    resDoc["result"] = "ok";
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
    delay(1000);
    ESP.restart();
  }else if (reqDoc["method"] == "setup_lora"){
    JsonObject params = reqDoc["params"];
    preferences.begin("lora", false);
    if (params["freq"]){
      uint32_t loraFreq = params["freq"];
      preferences.putUInt("freq", loraFreq);
    }
    if (params["SF"]){
      uint8_t loraSF = params["SF"];
      preferences.putUChar("SF", loraSF);
    }
    if (params["BW"]){
      uint32_t loraBW = params["BW"];
      preferences.putUInt("BW", loraBW);
    }
    if (params["CR"]){
      uint8_t loraCR = params["CR"];
      preferences.putUChar("CR", loraCR);
    }
    preferences.end();
    resDoc["result"] = "ok";
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
    delay(1000);
    ESP.restart();
  }else if (reqDoc["method"] == "get_wifi"){
    JsonObject result = resDoc.createNestedObject("result");
    preferences.begin("wifi", true);
    result["ssid"] = preferences.getString("ssid", "");
    result["pass"] = preferences.getString("password", "");
    preferences.end();
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
  }else if (reqDoc["method"] == "get_ip"){
    JsonObject result = resDoc.createNestedObject("result");
    if (WiFi.status() == WL_CONNECTED){
      result["ip"] = WiFi.localIP().toString();
    }else{
      result["ip"] = "";
    }
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
  }else if (reqDoc["method"] == "get_mac"){
    JsonObject result = resDoc.createNestedObject("result");
    result["mac_address"] = WiFi.macAddress();
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
  }else if (reqDoc["method"] == "get_lora"){
    JsonObject result = resDoc.createNestedObject("result");
    preferences.begin("lora", true);
    uint32_t loraFreq = preferences.getUInt("freq", 868825000);
    uint8_t loraSF = preferences.getUChar("SF", 11);
    uint32_t loraBW = preferences.getUInt("BW", 250000);
    uint8_t loraCR = preferences.getUChar("CR", 5);
    preferences.end();
    result["freq"] = String(loraFreq);
    result["SF"] = String(loraSF);
    result["BW"] = String(loraBW);
    result["CR"] = String(loraCR);
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
#ifdef MQTT
  }else if (reqDoc["method"] == "setup_mqtt"){
    JsonObject params = reqDoc["params"];
    preferences.begin("mqtt", false);
    if (params["lName"]){
      const char *tempString = params["lName"];
      preferences.putString("lName", tempString);
    }
    if (params["sName"]){
      const char *tempString = params["sName"];
      preferences.putString("sName", tempString);
    }
    if (params["rootTopic"]){
      const char *tempString = params["rootTopic"];
      preferences.putString("rootTopic", tempString);
    }
    if (params["channel"]){
      const char *tempString = params["channel"];
      preferences.putString("channel", tempString);
    }
    if (params["host"]){
      const char *tempString = params["host"];
      preferences.putString("host", tempString);
    }
    if (params["mqttUser"]){
      const char *tempString = params["mqttUser"];
      preferences.putString("mqttUser", tempString);
    }
    if (params["passwrd"]){
      const char *tempString = params["passwrd"];
      preferences.putString("passwrd", tempString);
    }
    preferences.end();
    resDoc["result"] = "ok";
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
    delay(1000);
    ESP.restart();
  }else if (reqDoc["method"] == "get_mqtt"){
    JsonObject result = resDoc.createNestedObject("result");
    preferences.begin("mqtt", true);
    result["lName"] = preferences.getString("lName", "");
    result["sName"] = preferences.getString("sName", "");
    result["rootTopic"] = preferences.getString("rootTopic", "");
    result["channel"] = preferences.getString("channel", "");
    result["host"] = preferences.getString("host", "");
    result["mqttUser"] = preferences.getString("mqttUser", "");
    result["passwrd"] = preferences.getString("passwrd", "");
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
#endif
  }else{
    JsonObject errorObj = resDoc.createNestedObject("error");
    errorObj["code"] = -32601;
    errorObj["message"] = "Method not found";
    serializeJson(resDoc, buf, sizeof(buf));
    Serial.println(buf);
    return;
  }
}

uint32_t readLine(char *buf){
  if (Serial.available() == 0){return 0;}
  int l = Serial.readBytes(&read_line_buf[read_line_buf_pos], sizeof(read_line_buf) - read_line_buf_pos);
  if (l < 0){return 0;}
  for (int i = 0; i < l; i++){
    if (read_line_buf[read_line_buf_pos + i] == '\r' && read_line_buf[read_line_buf_pos + i + 1] == '\n'){
      uint32_t data_len = read_line_buf_pos + i;
      memcpy(buf, read_line_buf, data_len);
      buf[read_line_buf_pos + i] = '\0';
      memcpy(read_line_buf, &read_line_buf[read_line_buf_pos + i + 2], l - i - 2);
      read_line_buf_pos = l - i - 2;
      return data_len;
    }
  }
  read_line_buf_pos += l;
  return 0;
}

#ifdef MQTT
void send2mqtt(char *buf, uint8_t bufsz, uint8_t portnum){
//todo
  uint8_t databuffer[256];
  pb_ostream_t stream = pb_ostream_from_buffer(databuffer, sizeof(databuffer));
  Data data = Data_init_zero;
//  data.portnum=portnum;
//  memcpy(data.payload, buf, bufsz);
}
void MQTTconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  preferences.begin("mqtt", true);
  String host = preferences.getString("host", "");
  Serial.println("connecting to mqtt host " + host);
  String u = preferences.getString("mqttUser", "");
  String p = preferences.getString("passwrd", "");
  preferences.end();
  mqtt.connect(nodeid, u.c_str(), p.c_str());
  delay(1000);
  if (mqtt.connected()){
    Serial.println("connected!");
  }else{
    Serial.println("time out!");
  }
}
#endif