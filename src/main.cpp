#include <Arduino.h>
#include <WiFi.h>
#include <ezTime.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <EEPROM.h>

#include "usersettings.h"
#include "settings.h"

// Globals
BLEClient *pClient = NULL;
BLEScan *pBLEScan = NULL;
BLEAdvertisedDevice *pDiscoveredDevice = NULL;
BLERemoteService *pBodyCompositionService = NULL;
BLERemoteService *pHuamiConfigurationService = NULL;
BLERemoteCharacteristic *pBodyCompositionHistoryCharacteristic = NULL;
BLERemoteCharacteristic *pCurrentTimeCharacteristic = NULL;
BLERemoteCharacteristic *pScaleConfigurationCharacteristic = NULL;
Timezone localTime;
size_t size;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
bool reconfigRequested = false;
bool mqttAck = false;
#define EEPROM_SIZE 27 // size required in EEPROM to store the last valid data
hw_timer_t *timer = NULL;

void blinkThenSleep(blink);
bool scanBle();
bool connectScale();

void IRAM_ATTR resetModule()
{
  ets_printf("reboot due to watchdog timeout\n");
  digitalWrite(ONBOARD_LED, LOW); // that means pull-up low --> LED ON
  esp_restart();
}

class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
    Serial.println("BLE client connected");
  }
  void onDisconnect(BLEClient *pclient)
  {
    Serial.println("BLE client disconnected");
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    Serial.print(".");
    if (DISCOVERY_METHOD == "mac")
    {
      if (advertisedDevice.getAddress().toString() != SCALE_MAC_ADDRESS)
        return;
    }
    else
    {
      if (!advertisedDevice.haveServiceUUID() && !advertisedDevice.isAdvertisingService(BODY_COMPOSITION_SERVICE))
        return;
    }

    // Reach this point only if the advertisedDevice corresponds to the MAC or has the right service UUID
    pDiscoveredDevice = new BLEAdvertisedDevice(advertisedDevice);
    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->stop();
  }
};

void mqttCallback(const char *topic, byte *payload, unsigned int length)
{
  if (length == 0)
    return;

  // Read payload
  char message[length + 1];
  for (int i = 0; i < length; i++)
    message[i] = (char)payload[i];
  message[length] = '\0';

  // Do we have a reconfig request?
  if (strcmp(topic, MQTT_SETTINGS_TOPIC) == 0 &&
      strcmp(message, CONFIG_TRIGGER_STR) == 0)
  {
    Serial.println("Reconfig requested");
    reconfigRequested = true;
  }

  // Do we have an ack from the other side?
  else if (strcmp(topic, MQTT_ACK_TOPIC) == 0 &&
           strcmp(message, ACK_SIGNAL) == 0)
  {
    Serial.println("Ack received");
    mqttAck = true;
  }
}

void reconnectScale()
{
  pClient->disconnect();
  while (pClient->isConnected())
    delay(DEFAULT_DELAY);
  delete (pDiscoveredDevice);
  scanBle();
  connectScale();
}

bool connectScale()
{
  pClient->connect(pDiscoveredDevice);

  // Characteristics under BODY_COMPOSITION_SERVICE
  pBodyCompositionService = pClient->getService(BODY_COMPOSITION_SERVICE);
  if (pBodyCompositionService == nullptr)
  {
    Serial.print("BODY_COMPOSITION_SERVICE failure, ending program");
    pClient->disconnect();
    blinkThenSleep(FAILURE);
  }

  pBodyCompositionHistoryCharacteristic = pBodyCompositionService->getCharacteristic(BODY_COMPOSITION_HISTORY_CHARACTERISTIC);
  if (pBodyCompositionHistoryCharacteristic == nullptr)
  {
    Serial.print("BODY_COMPOSITION_HISTORY_CHARACTERISTIC failure, ending program");
    pClient->disconnect();
    blinkThenSleep(FAILURE);
  }

  // All those are only required if we need to reconfigure the scale
  if (reconfigRequested)
  {
    pCurrentTimeCharacteristic = pBodyCompositionService->getCharacteristic(CURRENT_TIME_CHARACTERISTIC);
    if (pCurrentTimeCharacteristic == nullptr)
    {
      Serial.print("CURRENT_TIME_CHARACTERISTIC failure, ending program");
      pClient->disconnect();
      blinkThenSleep(FAILURE);
    }
  }

  if (reconfigRequested)
  {
    // Characteristics under HUAMI_CONFIGURATION_SERVICE
    pHuamiConfigurationService = pClient->getService(HUAMI_CONFIGURATION_SERVICE);
    if (pHuamiConfigurationService == nullptr)
    {
      Serial.print("HUAMI_CONFIGURATION_SERVICE failure, ending program");
      pClient->disconnect();
      blinkThenSleep(FAILURE);
    }

    pScaleConfigurationCharacteristic = pHuamiConfigurationService->getCharacteristic(SCALE_CONFIGURATION_CHARACTERISTIC);
    if (pScaleConfigurationCharacteristic == nullptr)
    {
      Serial.print("SCALE_CONFIGURATION_CHARACTERISTIC failure, ending program");
      pClient->disconnect();
      blinkThenSleep(FAILURE);
    }
  }
}

bool scanBle()
{
  BLEDevice::init("");
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(0x50);
  pBLEScan->setWindow(0x30);
  pBLEScan->start(MAX_BLE_SCAN_DURATION / 1000);

  Serial.println("");
  if (pDiscoveredDevice)
    return true;
  else
    return false;
}

int16_t stoi(String input, uint16_t index1)
{
  return (int16_t)(strtol(input.substring(index1, index1 + 2).c_str(), NULL, 16));
}

int16_t stoi2(String input, uint16_t index1)
{
  return (int16_t)(strtol((input.substring(index1 + 2, index1 + 4) + input.substring(index1, index1 + 2)).c_str(), NULL, 16));
}

void blinkThenSleep(blink blinkStatus)
{
  // `blinkStatus`
  //   - can be SUCCESS, FAILURE (see settings.h)
  //   - contains .blinkFor, .blinkOn, .blinkOff
  uint64_t untilTime = millis() + blinkStatus.blinkFor;
  while (millis() < untilTime)
  {
    digitalWrite(ONBOARD_LED, LOW); // that means pull-up low --> LED ON
    delay(blinkStatus.blinkOn);
    digitalWrite(ONBOARD_LED, HIGH); // that means pull-up high --> LED OFF
    delay(blinkStatus.blinkOff);
  }
  esp_deep_sleep_start();
}

bool wifiConnect()
{
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &resetModule, true);
  timerAlarmWrite(timer, 1000 * MAX_WIFI_ATTEMPT_DURATION, true);
  timerAlarmEnable(timer);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (!USE_DHCP)
  {
    IPAddress ip, gateway, subnet;
    ip.fromString(IP);
    gateway.fromString(GATEWAY);
    subnet.fromString(SUBNET);

    WiFi.config(ip, gateway, subnet, gateway);
  }

  while (true)
  {
    // We're covered by the watchdog here
    if (WiFi.status() == WL_CONNECTED)
    {
      timerAlarmDisable(timer);
      return true;
    }
    {
      delay(DEFAULT_DELAY);
    }
  }
}

bool weightStabilised(String rawDataFromScale)
{
  // Control bytes are 0 1 2 3, hence the `substring(0, 4)`
  uint16_t controlBytes = strtol(rawDataFromScale.substring(0, 4).c_str(), NULL, 16);
  if ((controlBytes & 0b100000) == 0) // 11th bit out of 16
    return false;
  else
    return true;
}

bool impedanceStabilised(String rawDataFromScale)
{
  // Return `false` for impedance marked as unstable or negative impedance
  float impedance = stoi2(rawDataFromScale, 18) * 0.01f;

  // Control bytes are 0 1 2 3, hence the `substring(0, 4)`
  uint16_t controlBytes = strtol(rawDataFromScale.substring(0, 4).c_str(), NULL, 16);
  if ((controlBytes & 0b10) == 0 || impedance <= 0) // 15th bit out of 16
    return false;
  else
    return true;
}

String processScaleData(String rawDataFromScale)
{
  float weight = stoi2(rawDataFromScale, 22) * 0.01f; // there's a trick
  float impedance = stoi2(rawDataFromScale, 18) * 0.01f;

  // Finding out what unit is used, the info comes from the control bytes.
  // Control bytes are 0 1 2 3, hence the `substring(0, 4)`
  uint16_t controlBytes = strtol(rawDataFromScale.substring(0, 4).c_str(), NULL, 16);
  String strUnits;
  // 8th and 10th off: metric
  // 8th on: catty
  // 10th on: imperial
  if ((controlBytes & 0b100000000) == 0 && (controlBytes & 0b1000000) == 0)
  {
    strUnits = "kg";
    weight /= 2;
  }
  else if ((controlBytes & 0b100000000) == 0)
    strUnits = "jin";
  else if ((controlBytes & 0b1000000) == 0)
    strUnits = "lbs";

  // Prepare return string
  String time = String(String(stoi2(rawDataFromScale, 4)) + " " + String(stoi(rawDataFromScale, 8)) + " " + String(stoi(rawDataFromScale, 10)) + " " + String(stoi(rawDataFromScale, 12)) + " " + String(stoi(rawDataFromScale, 14)) + " " + String(stoi(rawDataFromScale, 16)));
  String parsedData =
      String("{\"Weight\":\"") +
      String(weight) +
      String("\", \"Impedance\":\"") +
      String(impedance) +
      String("\", \"Units\":\"") +
      String(strUnits) +
      String("\", \"Timestamp\":\"") +
      time +
      String("\"}");

  return parsedData;
}

void configureScale()
{
  Serial.println("Configuring scale");

  // Set scale units
  Serial.println("Setting scale units");
  uint8_t setUnitCmd[] = {0x06, 0x04, 0x00, SCALE_UNIT};
  size = 4;
  pScaleConfigurationCharacteristic->writeValue(setUnitCmd, size, true);

  // Set time
  Serial.println("Setting scale time");
  uint16_t year = localTime.year();
  uint8_t month = localTime.month();
  uint8_t day = localTime.day();
  uint8_t hour = localTime.hour();
  uint8_t minute = localTime.minute();
  uint8_t second = localTime.second();
  uint8_t yearLeft = (uint8_t)(year >> 8);
  uint8_t yearRight = (uint8_t)year;

  uint8_t dateTimeByte[] = {yearRight, yearLeft, month, day, hour, minute, second, 0x03, 0x00, 0x00};
  size = 10;
  pCurrentTimeCharacteristic->writeValue(dateTimeByte, size, true);
}

String readScaleData()
{
  // It's unclear to me in which circumstances the scale will remember/report
  // only the latest weigh-in, or a handful of them. In doubt, let's only work
  // with the latest entry.
  int lastEntry = pDiscoveredDevice->getServiceDataCount() - 1;
  std::string md = pDiscoveredDevice->getServiceData(lastEntry);
  uint8_t *mdp = (uint8_t *)pDiscoveredDevice->getServiceData(lastEntry).data();

  return BLEUtils::buildHexData(nullptr, mdp, md.length());
}

void checkReconfigRequested()
{
  // Possibly need to resolve IP address from hostname
  IPAddress MQTT_IP;
  if (!MQTT_IP.fromString(MQTT_SERVER))
  {
    if (!MDNS.begin("ESP32mDNS"))
    {
      Serial.println("Error setting up mDNS responder, ending program");
      blinkThenSleep(FAILURE);
    }
    else
    {
      MQTT_IP = MDNS.queryHost(MQTT_SERVER);
      if (!MQTT_IP)
      {
        Serial.println("Cannot resolve MQTT server address, ending program");
        blinkThenSleep(FAILURE);
      }
      else
      {
        Serial.print("IP address of MQTT server resolved to: ");
        Serial.println(MQTT_IP.toString());
      }
    }
  }

  // Connect to MQTT
  mqttClient.setServer(MQTT_IP, MQTT_PORT);
  if (!mqttClient.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD, 0, 1, 0, 0, 1))
  {
    Serial.println("Cannot connect to MQTT, ending program");
    blinkThenSleep(FAILURE);
  }
  else
    Serial.println("MQTT connected");

  // Subscribe to settings topic
  if (!mqttClient.subscribe(MQTT_SETTINGS_TOPIC))
  {
    Serial.println("Cannot subscribe to MQTT topic, ending program");
    blinkThenSleep(FAILURE);
  }
  else
    Serial.println("MQTT settings topic subscribed");

  // Poll topic for a maximum of MQTT_CONF_POLL_TIME
  uint64_t startTime = millis();
  uint64_t untilTime = startTime + MQTT_CONF_POLL_TIME;
  mqttClient.setCallback(mqttCallback);
  while (millis() < untilTime)
  {
    mqttClient.loop();
    if (reconfigRequested)
    {
      // Clear queue
      mqttClient.publish(MQTT_SETTINGS_TOPIC, NULL, true);
      break;
    }
    delay(DEFAULT_DELAY);
  }

  if (reconfigRequested)
    Serial.println("Reconfig requested");
  else
    Serial.println("Reconfig not requested");

  mqttClient.disconnect();
  Serial.println("MQTT disconnected");
}

//--------------------

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting program");

  digitalWrite(ONBOARD_LED, HIGH);
  pinMode(ONBOARD_LED, OUTPUT);

  EEPROM.begin(EEPROM_SIZE);

  Serial.println("Connecting to WiFi");
  if (!wifiConnect())
  {
    Serial.println("Cannot connect to WiFi, ending program");
    blinkThenSleep(FAILURE);
  }
  else
    Serial.println("WiFi connected");

  checkReconfigRequested();
  if (reconfigRequested)
  {
    Serial.println("Reconfig requested");
    Serial.println("Getting time from NTP");
    waitForSync(); // ezTime setup
    localTime.setLocation(TIMEZONE);
    Serial.println("Local time: " + localTime.dateTime());
  }

  WiFi.disconnect();
  Serial.println("WiFi disconnected");

  // Configure BLE callbacks
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());

  // Look for scale
  Serial.println("Scanning BLE");
  if (scanBle())
    Serial.println("Scale found, now initialising services");
  else
  {
    Serial.println("Cannot find scale, going to sleep");
    blinkThenSleep(FAILURE);
  }

  // Create BLE client and create service/characteristics objects
  connectScale();

  // Configure scale if requested. We keep Xiaomi's way of using local time
  // instead of UTC for the scale, to not break things for users using Mi Fit
  // together with this program. And we end the program after the reconfig, as
  // a weigh-in should not happen in the same "session" as the reconfig,
  // otherwise values reported by the scale are all mixed up.
  if (reconfigRequested)
  {
    configureScale();
    Serial.println("Reconfig done, ending program");
    blinkThenSleep(SUCCESS);
  }
}

void loop()
{
  String scaleReading;
  String reading;
  String inStorage = EEPROM.readString(0);

  for (int i = 0; i < BT_POLL_ATTEMPTS; i++)
  {
    reading = readScaleData();
    if (!weightStabilised(reading))
    {
      Serial.println("Weight not stabilised in last measure");
      if (i == BT_POLL_ATTEMPTS - 1)
      {
        Serial.println("No stabilised weight measurement in scale, ending program");
        blinkThenSleep(FAILURE);
      }
      else
      {
        // Reconnect scale. It seems silly to have to re-scan BLE but unless I
        // do that, I never get a new weigh-in value. Disconnecting/reconnecting
        // client isn't enough.
        delay(TIME_BETWEEN_BT_POLLS);
        reconnectScale();
      }
    }
    else if (reading == inStorage)
    {
      Serial.println("Latest value is identical to the latest processed one");
      if (i == BT_POLL_ATTEMPTS - 1)
      {
        Serial.println("No fresh measurement in scale, ending program");
        blinkThenSleep(FAILURE);
      }
      else
      {
        delay(TIME_BETWEEN_BT_POLLS);
        reconnectScale();
      }
    }
    else if (!impedanceStabilised(reading))
    {
      // We've got a value with a stabilised weight, if no impedance found then
      // we try reading again, and stick to the no-impedance figure if no
      // further success
      Serial.println("Weight stabilised but impedance is not stabilised");
      if (i == BT_POLL_ATTEMPTS - 1)
      {
        Serial.println("Got a stable weight but no stable impedance, will proceed with no impedance");
      }
      else
      {
        delay(TIME_BETWEEN_BT_POLLS);
        reconnectScale();
      }
    }
    else
      break;
  }

  scaleReading = processScaleData(reading);
  Serial.print("Reading (weight stable): ");
  Serial.println(scaleReading.c_str());

  // Ready to transmit data. WiFi and MQTT should succeed, they have already
  // been tested in setup.
  Serial.println("Connecting to WiFi and MQTT");
  wifiConnect();
  mqttClient.connect(MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD);

  Serial.println("Subscribe to MQTT ack topic and set callback");
  mqttClient.subscribe(MQTT_ACK_TOPIC);

  // First (and hopefully only) MQTT message (this is retained)
  Serial.print("Publishing data to MQTT queue: ");
  Serial.println(MQTT_DATA_TOPIC);
  uint64_t startTime = millis();
  mqttClient.publish(MQTT_DATA_TOPIC, scaleReading.c_str(), true);

  // Poll topic for a maximum of MQTT_ACK_POLL_TIME
  uint64_t untilTime = startTime + MQTT_ACK_POLL_TIME;
  mqttClient.setCallback(mqttCallback);

  uint64_t currentAttemptUntilTime = startTime + MQTT_RESEND_AFTER;
  while (millis() < untilTime)
  {
    mqttClient.loop();
    if (mqttAck)
    {
      Serial.println("Transfer successful");
      Serial.println("Storing transmitted data to EEPROM");
      EEPROM.writeString(0, reading);
      EEPROM.commit();
      Serial.println("Ending program");
      blinkThenSleep(SUCCESS);
    }
    else if (millis() <= currentAttemptUntilTime)
    {
      Serial.println("No ack received, waiting a bit");
      delay(DEFAULT_DELAY);
    }
    else if (millis() > currentAttemptUntilTime)
    {
      Serial.println("No ack received, sending MQTT payload again");
      currentAttemptUntilTime = millis() + MQTT_RESEND_AFTER;
      mqttClient.publish(MQTT_DATA_TOPIC, scaleReading.c_str(), true);
    }
  }

  Serial.println("No ack ever received, failure");
  blinkThenSleep(FAILURE);
}