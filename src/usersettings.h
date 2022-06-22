// Scale discovery method: "mac" for MAC address, anything else for first
// found scale with relevant given BLE UUIDs
#define DISCOVERY_METHOD "mac"

// Scale Mac Address. Letters should be in lower case.
#define SCALE_MAC_ADDRESS "aa:bb:cc:dd:ee:ff"

// Scale unit: 0 for metric, 1 for imperial, 2 for catty
#define SCALE_UNIT 0

// For ESP32 development board LED feedback. On mine the LED is on GPIO 22
#define ONBOARD_LED 22

// Timezone (https://en.wikipedia.org/wiki/List_of_tz_database_time_zones)
#define TIMEZONE "Europe/Paris"

// network details
#define HOSTNAME "esp_xiaomi_scale"
#define USE_DHCP 0
#define WIFI_SSID "MakersHouse"
#define WIFI_PASSWORD "makersmake"
#define IP "192.168.1.42"
#define GATEWAY "192.168.1.254" // it is assumed that the gateway acts as DNS
#define SUBNET "255.255.255.0"

// MQTT Details
#define MQTT_SERVER "192.168.1.234" // IP address or hostname
#define MQTT_PORT 8884
#define MQTT_SETTINGS_TOPIC "scaleSettings"
#define MQTT_DATA_TOPIC "scale"
#define MQTT_ACK_TOPIC "scale_ack"
#define MQTT_USERNAME "mqtt username"
#define MQTT_PASSWORD "mqtt password"
#define MQTT_CLIENTID "scaleEsp"
