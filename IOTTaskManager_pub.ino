#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_BME280.h>
#include <Wire.h>


const char* ssid = "Your ssid";
const char* pass = "your password";
const char* mqtt_server = "your broker"; // MQTT Broker

void callback(char* topic, byte* message, unsigned int len);

WiFiClient espClient;
PubSubClient client(mqtt_server, 1883, callback, espClient);
char msgbuf[50];
char mqttname[30];
char* temptune = "esp8266/tempsync";
Adafruit_BME280 bme;
float Temperature = 0;
float adjTemp = 0;
float TempSync = 0;
float Pressure = 0;
float Altitude = 0;
float Humidity = 0;


#define SEALEVELPRESSURE_HPA (1013.25)


enum TASK { Comm, Wifi, Mqttstup, Mqttrcn, Sensor, PubSen, Heartbeat, Pubhb, Reccn, Recchk, Cmpl };
enum Action { BEGIN, CONT };
bool InitState = false;

typedef struct {
  TASK task;
  long tick;
} TKSTR;

long prevtims = 0;
long currtims = 0;
long interval = 10; // Smallest time to decrement task delay time

TKSTR Aque[25];
int HEAD = 0;
int Qsize = 0;
long Prgmcnt = 0;
int Wifireset = 0;
int Mgttreset = 0;
const int BLOCK = 10;
const int NOBLOCK = 0;
const long HBTIME = 20000;
const long SNTIME = 30000;

#define PRGMMRK Prgmcnt = (++Prgmcnt) % 100 // Restrict the progress markers from 0 to 99
#define WIFIRESET Wifireset++ // Count the number of attempts to connect to Wifi
#define WIFICLEAR Wifireset = 0 // Clear Wifi reset counter
#define MGTTRESET Mgttreset++ // Count the number of attempts to connect to MGTT
#define MGTTCLEAR Mgttreset = 0 // Clear Mgtt reset counter

/**************************************************************************************
  Work task

  1) WifiSetup(Action)
  2) MqttSetup(Action)
  3) callback(topic, message, length)
  4) Getmac(mac)- Convert MAC address to string
  5) SndMQTT(topic, message) - Publish MQTT message
  6) RdHbeat(Action) - Check and reconnect MQTT if needed before publishing heartbeat
  7) hbeat()
  8) RdSensor(Action) - Check and reconnect MQTT if needed before publishing sensor
  9) BMEmgr(Action)
  10) wktask()



***************************************************************************************/

void WifiSetup(Action act) {

  PRGMMRK;

  switch (act) {
    case BEGIN:
      Serial.println();
      Serial.println("Connecting to WiFi");
      WiFi.disconnect(); // Disconnect from current WiFi Network
      WiFi.begin(ssid, pass); // Connect to Wifi specified by ssid
    // Fall Through
    case CONT:
      if (WiFi.status() != WL_CONNECTED) {
        qinsert(Wifi, 500);
        WIFIRESET;
        Serial.print(".");
        break;
      }
      Serial.println();
      Serial.print("WiFi Connected to: ");
      Serial.println(WiFi.SSID());

      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      Serial.print("MAC address: ");
      Serial.println(WiFi.macAddress());
      //WiFi.printDiag(Serial);
      break;
    default:
      break;
  }

}



void MqttSetup(Action act) {

  PRGMMRK;

  String clientId = "ESP-";
  uint8_t mac[6];

  if (WiFi.status() != WL_CONNECTED) {

    // WiFi not connected - reschedule MQTT Setup
    qinsert(Mqttstup, 5000); // Try again in 5 sec
    Serial.print("Rescheduling MQTT: ");
    Serial.println(client.state());
    MGTTRESET;
  }
  else {

    WiFi.macAddress(mac);
    clientId += Getmac(mac);

    switch (act) {
      case BEGIN:

        Serial.println("Attempting MQTT connection:");

      // Fall through
      case CONT:

        if (client.connect(clientId.c_str())) {
          Serial.print("MQTT Connected: ");
          Serial.println(clientId.c_str());
          Serial.print("MQTT State: ");
          Serial.println(client.state());
          if (!client.subscribe(temptune)) {
            Serial.println("MQTT Subscription Failed");
          }
          strcpy(mqttname, clientId.c_str());
          InitState = true;

        }
        else {
          qinsert(Mqttrcn, 5000); // Try again in 5 sec
          Serial.print("Retrying MQTT: ");
          Serial.println(client.state());
          MGTTRESET;
        }

        break;
      default:
        break;
    }
  }
}


void callback(char* topic, byte* message, unsigned int len) {

  int i;
  String lmsg;
  PRGMMRK;

  Serial.print("Message Arrived: ");
  Serial.print(topic);
  Serial.print(". Message: ");

  for (i = 0; i < len; i++) {
    Serial.print((char)message[i]);
    lmsg += (char)message[i];
  }
  Serial.println();
  TempSync = lmsg.toFloat() - Temperature;
  if (String(topic) == "esp8266/sync") {
    TempSync = lmsg.toFloat() - Temperature;
  }
}


String Getmac(const uint8_t* mac) {

  String result;
  int i;

  for (i = 0; i < 6; ++i) {
    result += String(mac[i], 16);

    if (i < 5) {
      result += ':';
    }

  }
  return result;
}


void SndMQTT(char* topic, char* msg) {

  strcpy(msgbuf, mqttname); // Store MGTT client name
  strcat(msgbuf, ",");
  strcat(msgbuf, msg); // Store Message
  client.publish(topic, msgbuf);

}



void RdHbeat(Action act) {

  if (client.state() != MQTT_CONNECTED) {
    client.connect(mqttname);
    qinsert(Pubhb, 500);
    MGTTRESET;
  }
  else {
    qinsert(Heartbeat, 1000);
  }
}


void hbeat(Action Act) {

  PRGMMRK;

  char tmsg[8];

  // Capture the progress counter
  dtostrf(Prgmcnt, 3, 0, tmsg);
  SndMQTT("esp8266/heartbeat", tmsg);

  // Capture Wifi reset counter
  dtostrf(Wifireset, 2, 0, tmsg);
  SndMQTT("esp8266/wifi", tmsg);

  // Capture MQTT reset counter
  dtostrf(Mgttreset, 2, 0, tmsg);
  SndMQTT("esp8266/mgtt", tmsg);

  // Capture Queue size counter
  dtostrf(Qsize, 2, 0, tmsg);
  SndMQTT("esp8266/queue", tmsg);

  qinsert(Pubhb, HBTIME); // Reschedule heartbeat task

  WIFICLEAR;
  MGTTCLEAR;

}

void RdSensor(Action act) {

  if (client.state() != MQTT_CONNECTED) {
    client.connect(mqttname);
    qinsert(PubSen, 500);
    MGTTRESET;
  }
  else {
    qinsert(Sensor, 1000);
  }
}


void BMEmgr(Action act) {

  char tmsg[8];
  int i;

  PRGMMRK;

  switch (act) {
    case BEGIN:

      if (bme.begin()) {
        Serial.println("BME280 Sensor Connected: ");
      }
      else {
        Serial.println("No BME280 Sensor detected: ");
      }

      break;
    case CONT:

      // Capture temperature from the BME
      Temperature = (bme.readTemperature() * 1.8) + 32;
      dtostrf(Temperature, 4, 2, tmsg);
      SndMQTT("esp8266/temperature", tmsg);
      Serial.print("Temperature = ");
      Serial.print(Temperature);
      Serial.println(" *F");


      //Publish Adjusted temperature
      adjTemp = Temperature + TempSync;
      dtostrf(adjTemp, 4, 2, tmsg);
      SndMQTT("esp8266/adjtemp", tmsg);
      Serial.print("adjTemp = ");
      Serial.print(adjTemp);
      Serial.println(" *F");

      // Capture pressure from the BME
      Pressure = bme.readPressure() / 100.0F;
      dtostrf(Pressure, 4, 2, tmsg);
      SndMQTT("esp8266/pressure", tmsg);

      // Capture altitude from the BME
      Altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);
      dtostrf(Altitude, 4, 0, tmsg);
      SndMQTT("esp8266/altitude", tmsg);

      // Capture humidity from the BME
      Humidity = bme.readHumidity();
      dtostrf(Humidity, 4, 2, tmsg);
      SndMQTT("esp8266/humidity", tmsg);

      qinsert(PubSen, SNTIME); // reschedule sensor scan

      break;
    default:
      break;
  }
}


void wktask() {

  TASK task;
  int action;

  action = Aque[HEAD].tick;

  switch (action) {
    case 0:
      task = qpop();
      switch (task) {
        case Comm:
          WifiSetup(BEGIN);
          BMEmgr(BEGIN);
          MqttSetup(BEGIN);
          break;
        case Wifi:
          WifiSetup(CONT); // Check Wifi setup status
          break;
        case Mqttstup:
          MqttSetup(BEGIN);
          break;
        case Mqttrcn:
          MqttSetup(CONT); // Try MQTT Setup Again
          break;
        case Pubhb:
          RdHbeat(CONT);
          break;
        case Heartbeat:
          hbeat(CONT);
          break;
        case PubSen:
          RdSensor(CONT);
          break;
        case Sensor:
          BMEmgr(CONT);
          break;
        default:
          break;
      }

      break;
    default:
      break;
  }
}


/**************************************************************************************
  Task Queue Management - Implementation of ordered queue (priority queue).

  1) qinsert(TASK,tick) - O(n^2) - Insertion sort
  2) TASK qpop() - O(n) - Remove highest priority item at head of queue
  3) qinit() - O(n) - Initialize queue to default values
  4) qticktock() - O(n) - Adjust task priorities every 10 milli seconds

***************************************************************************************/


void qinsert(TASK task, long tick) {

  int x;
  int i;

  // Find the correct location for this task
  for (x = HEAD; x < Qsize; x++) {
    if (tick < Aque[x].tick) {
      break;
    }
  }

  // Move task as needed to open up space for the higher priority (shortest delay) task. Task with same
  // delay are given higher priority.
  for (i = Qsize; i > x; i--) {
    Aque[i].task = Aque[i - 1].task;
    Aque[i].tick = Aque[i - 1].tick;
  }

  // Insert new task into queue
  Aque[i].task = task;
  Aque[i].tick = tick;
  Qsize++;

  //Serial.println(Qsize);
}


// Remove the task at the head of the Queue
TASK qpop() {

  TASK result = Aque[HEAD].task;
  int x = HEAD;

  do {

    Aque[x].task = Aque[x + 1].task;
    Aque[x].tick = Aque[x + 1].tick;
    x++;
  } while (x < Qsize);
  Qsize--;

  return result;
}


void qinit() {

  int x;

  for ( x = HEAD; x < 25; x++) {
    Aque[x].task = Cmpl;
    Aque[x].tick = (long)99999;
  }
}


void qticktock(int block) {

  long incr;

  if (block > 0) {
    delay(block); // Blocking step
  }
  currtims = millis();

  if ( currtims - prevtims > interval ) {
    incr = currtims - prevtims;
    prevtims = currtims;

    for (int i = HEAD; i < Qsize; i++) {
      if (Aque[i].tick > incr) {
        Aque[i].tick = Aque[i].tick - incr;
      }
      else {
        Aque[i].tick = 0;
      }
    } // End For
  }
}

/**************************************************************************************
  ESP8266 Control



***************************************************************************************/



void setup() {

  TASK task;

  Serial.begin(115200); //Serial.begin(9600);

  Wire.begin(0, 2); // Set SDA and SCL for the ESP-01S
#if defined(__AVR_ESP8266_Generic__)

#endif

  qinit(); // Initialize the work queue - Set default values
  qinsert(Comm, 300); // Schedule WiFi and MQTT setup task - Delay 300 msec allowing serial port setup to complete

  do {

    qticktock(BLOCK);
    wktask(); // Execute setup task until work is complete

  } while (InitState != true);

  qinsert(Pubhb, HBTIME);
  qinsert(PubSen, SNTIME);
  Serial.println("Init Complete: ");
}


void loop() {
  // Main Loop

  if (!client.connected() && Qsize == 0) {
    Serial.print("MQTT Not Connected: ");
    Serial.println(client.state());
    qinsert(Mqttrcn, 50); // Insert task to restart MQTT at head of queue
    MGTTRESET;
  }

  qticktock(NOBLOCK);
  wktask(); // Execute scheduled task

  client.loop(); // MQTT Loop
}
