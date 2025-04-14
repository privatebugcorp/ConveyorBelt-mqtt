//SEARCH FOR "!!"", TO adapt/cahnge CODE, 
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHT.h"
#define DHTTYPE DHT11

//  Sensor & PWM Pins 
#define DHTPIN 22 
#define TRIG_PIN 19
#define ECHO_PIN 21
#define pwmPin 18 //pin to light control
DHT dht(DHTPIN, DHTTYPE); //initialize dht sensor (using dht lib)

#define WAKE_UP_pin 23
// Global Variables 
int currentPWM = 0;
bool isManualPWM = false; // True if PWM is set manually via MQTT for LED
bool isManualDC = false; //  True if PWM is set manually via MQTT for DC MOTORS
int pwmValue = 180; //deafult PWM  when using sensor control (if applicable)

unsigned long lastMsg_dht = 0; //for working in loop without delay, which stops all code 
bool objectDetected = false; // Tracks if an object is detected on the conveyor

//  Wifi and MQTT settings  // First Wifi network below
const char* ssid1 = "";
const char* password1 = "";
const char* mqtt_server1 = "";

// Second Wifi network
const char* ssid2 = "";
const char* password2 = "";
const char* mqtt_server2 = "";

// Add Third Wifi network if needed -- change if the router has changed
// or make reading from array in loop 
WiFiClient espClient; //making object "espClient" for Wifi (handles connection-TCP)
PubSubClient client(espClient); //object "espClient" now can sub or pub smthng on topics

// function used for changing wifi if it s not reachable for 5 tries
bool attemptWiFiConnection() {
  int retry_count = 0;
  while (WiFi.status() != WL_CONNECTED && retry_count < 5) {
    delay(1500);
    Serial.println("Retrying Wi-Fi connection...");
    retry_count++;
  }
  return (WiFi.status() == WL_CONNECTED);
}
//  Wifi connection functions 
void connectWiFi() {
  Serial.println("Connecting to Wi-Fi...");
  // Try 1st network (home)
  WiFi.begin(ssid1, password1);

  if (!attemptWiFiConnection()) {

  Serial.println("Failed to connect to first Wi-Fi!"); // commneted other networks!!
    // Try 2nd network if 1st isnt reachable, and below is also 3rd
   // Serial.println("Switching to Automatyk's Wi-Fi...");
   // WiFi.begin(ssid2, password2);
    // if (!attemptWiFiConnection()) {
      // Serial.println("Switching to HUAWEIP10...");
      // WiFi.begin(ssid3, password3);
      // if (!attemptWiFiConnection()) {
      //   Serial.println("Failed to connect to any Wi-Fi, restarting...");
      ESP.restart();
      // } //impelement if u need 3rd wifi 
    }
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP()); 
}

//  MQTT reconnection 4 safety (5 tries)
void reconnectMQTT() {
  int retry_count = 0;
  while (!client.connected() && retry_count < 5) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT connected");
      client.subscribe("/home/control/pwm"); //add another topics if needed
      client.subscribe("/set_control_mode"); //add another topics if needed
      client.subscribe("/home/conveyor/object_status"); // Subscribe to object detection status

    } else
    {
      Serial.print("Failed, rc=");
      Serial.println(client.state()); //client state = rc (type of error)
      delay(5000);
      retry_count++;
    }
  }
  if (!client.connected()) {
    Serial.println("Failed to connect MQTT, restarting...");
    ESP.restart();
  }
}

/*  MQTT callback -- mqtt callback function, byte* payload indicates that payload 
is a pointer to an array of bytes (8-bit values). 
It holds the raw data received, which the function processes 
by iterating over each byte and converting it to a character to build a readable message. */
void callback(char* topic, byte* payload, unsigned int length) {
  String message; //empty string for the incoming message
  
  // build the message string from the payload bytes
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  // print the topic and message to the serial monitor
  Serial.print("message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Handle PWM control messages
  if (String(topic) == "/home/control/pwm") {
    if (message == "default") {
      isManualPWM = false;
      Serial.println("PWM set to default mode (controlled by sensor)");
    } 
    else if (message == "max_light") {
      pwmValue = 0; // Max brightness (inverse logic)
      analogWrite(pwmPin, pwmValue);
      currentPWM = pwmValue;
      isManualPWM = true;
      Serial.println("PWM set to max light (0)");
    } 
    else if (message == "no_light") {
      pwmValue = 255; // Min brightness (inverse logic)
      analogWrite(pwmPin, pwmValue);
      currentPWM = pwmValue;
      isManualPWM = true;
      Serial.println("PWM set to no light (255)");
    } 
    else {
      int pwmValue = message.toInt();
      if (pwmValue >= 0 && pwmValue <= 255) {
        analogWrite(pwmPin, pwmValue);
        currentPWM = pwmValue;
        isManualPWM = true;
        Serial.print("PWM set manually to: ");
        Serial.println(pwmValue);
      } else {
        Serial.println("Invalid PWM value received!");
      }
    }
  // (can be upgraded)- to manually set speed of DC motors
  if (String(topic) == "/set_control_mode") {
    // Accept "manual"/"esp32" or "1"/"0"
    if (message.equalsIgnoreCase("manual") || message == "1") {
      isManualDC = true;
      Serial.println("Manual DC set to TRUE");
    } else if (message.equalsIgnoreCase("esp32") || message == "0") {
      isManualDC = false;
      Serial.println("Manual DC set to FALSE");
      }
    }
  }
}

// MQTT config 
void configureMQTT() {
  String localIP = WiFi.localIP().toString();
  if (localIP.startsWith("192.168.1")) {
    client.setServer(mqtt_server1, 1883);
    
    Serial.println("Using MQTT server: " + String(mqtt_server1));
  } else if (localIP.startsWith("192.168.0")) {
    client.setServer(mqtt_server2, 1883);
    Serial.println("Using MQTT server: " + String(mqtt_server2));
  } /*else { //implement if needed
    client.setServer(mqtt_server3, 1883);
    Serial.println("Using MQTT server: " + String(mqtt_server3));
  } */
  client.setCallback(callback);
}

float getDistance() { //TRIGGERING AND RECIEVING TIME TO CALCULATE DISTANCE
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = (duration * 0.0395) / 2; // change it for model if your sensor works correctly, if u using new hcsr03 just put 0.034/2
  return distance;
}
unsigned long lastPWMUpdate = 0; 
void controlPWMBasedOnDistance(float distance) {
  if (isManualPWM == false) {
    unsigned long currentTime = millis(); // millis=~TIMER
    const unsigned long holdTime = 10000;

    if (currentTime - lastPWMUpdate >= holdTime || lastPWMUpdate == 0) {
      if (!objectDetected && distance <= 34) {
        if (pwmValue != 179) {
          pwmValue = 179; // 30% brightness
          analogWrite(pwmPin, pwmValue);
          lastPWMUpdate = currentTime;
          Serial.println("PWM set to 30% brightness (179) - No object and distance <= 34 cm");
        }
      } 
      else {
        if (pwmValue != 0) {
          pwmValue = 0; // Max brightness
          analogWrite(pwmPin, pwmValue);
          lastPWMUpdate = currentTime;
          Serial.println("PWM set to max brightness (0)");
        }
      }
    } 
    else {
      analogWrite(pwmPin, pwmValue);
      Serial.print("PWM held at: ");
      Serial.print(pwmValue);
      Serial.print(" (");
      Serial.print((holdTime - (currentTime - lastPWMUpdate)) / 1000);
      Serial.println("s remaining)");
    }
  }
}

unsigned long lastMsg = 0;

void readAndPublishDistance(float distance){      // Publish distance as JSON
  if (distance >= 2 && distance <= 400){
  unsigned long now = millis();
  StaticJsonDocument<80> doc;
  char output[80];

  long lastMsg = 0;
  if (now - lastMsg > 1000) {
    lastMsg = now;
    doc["distance"] = distance;
    serializeJson(doc, output);
    client.publish("/home/sensors/distance", output);
    Serial.println("Sent to /home/sensors/distance: ");
    Serial.println(output);
    }
  }
}

// DHT sensor reading & publishing 
void readAndPublishDHT() {
  float h = dht.readHumidity(); //variables transfered by Json
  float t = dht.readTemperature(); 
  //validation
  if (isnan(h) || isnan(t)) { 
     Serial.println("Failed to read from DHT sensor!");
    return;
  }
  unsigned long now_dht = millis(); 
  StaticJsonDocument<128> doc;
  char output[128]; // array for serialized JSON  string

   if (now_dht - lastMsg_dht > 5000) { 
    lastMsg_dht = now_dht;
    doc["t"] = t;
    doc["h"] = h;
    serializeJson(doc, output); // converts the JSON document stored in doc into 
    client.publish("/home/sensor_data", output); //a JSON formatted string 
    Serial.println(output); // and writes that string to the specified output
  }
}

// wake up antother esp32 
void WakeUP(){
  // Send LOW pulse (100 ms) to wake the other esp32
  Serial.println("Sending wake-up pulse (LOW)...");
  digitalWrite(WAKE_UP_pin, LOW);
  delay(100);
  digitalWrite(WAKE_UP_pin, HIGH); // back to high state, keeping esp32 asleep
  Serial.println("Pulse sent!");
}

//  Setup 
void setup() {

  delay(1500); 
  connectWiFi(); //starts on boot connecting wifi
  configureMQTT();
  Serial.begin(115200); 
 // FOR LATER IMPLEMENT
  // pinMode(WAKE_UP_pin, OUTPUT); 
  // digitalWrite(WAKE_UP_pin, HIGH);  
  pinMode(TRIG_PIN, OUTPUT); //distance sensor 
  pinMode(ECHO_PIN, INPUT); //distance sensor
  pinMode(pwmPin, OUTPUT);  // light control

  dht.begin(); //temp&humidity sensor
  client.setCallback(callback); 
  client.subscribe("/home/control/pwm"); //sub to c Qontrol manually light
  client.subscribe("/set_control_mode"); // sub to controll pwm by API (made in app.py)
  client.subscribe("/home/conveyor/object_status"); // Subscribe to object detection status
}
// Main loop 
void loop() {

  if (!client.connected()) {
    reconnectMQTT(); 
  }
  client.loop();   // Process incoming messagesx
  readAndPublishDHT(); //JSON mqtt communication
  float distance = getDistance();
  controlPWMBasedOnDistance(distance);
  readAndPublishDistance(distance);
}
