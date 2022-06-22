// The program should work fine without modifying these

// Event durations (milliseconds)
#define DEFAULT_DELAY 150
#define MAX_BLE_SCAN_DURATION 10000
#define TIME_BETWEEN_BT_POLLS 5000 // more will be spent due to BLE re-scan
#define MAX_WIFI_ATTEMPT_DURATION 4000
#define MQTT_CONF_POLL_TIME 1000 // max time spend polling for reconfig
#define MQTT_ACK_POLL_TIME 20000 // max time spend polling confirmation
#define MQTT_RESEND_AFTER 3000

// Number of attempts
#define BT_POLL_ATTEMPTS 5
#define MTQT_POLL_ATTEMPTS 15

// Ack signal
#define ACK_SIGNAL "X"

// Blinking durations (milliseconds)
struct blink
{
  int blinkFor;
  int blinkOn;
  int blinkOff;
};
blink SUCCESS = {
    .blinkFor = 8000,
    .blinkOn = 900,
    .blinkOff = 100};
blink FAILURE = {
    .blinkFor = 8000,
    .blinkOn = 100,
    .blinkOff = 100};

// Message to be sent to MQTT_SETTINGS_TOPIC to trigger scale reconfig
#define CONFIG_TRIGGER_STR "1"

// BLE UUIDs
BLEUUID BODY_COMPOSITION_SERVICE("0000181b-0000-1000-8000-00805f9b34fb");
BLEUUID BODY_COMPOSITION_HISTORY_CHARACTERISTIC("00002a2f-0000-3512-2118-0009af100700");
BLEUUID CURRENT_TIME_CHARACTERISTIC("00002a2b-0000-1000-8000-00805f9b34fb");

BLEUUID HUAMI_CONFIGURATION_SERVICE("00001530-0000-3512-2118-0009af100700");
BLEUUID SCALE_CONFIGURATION_CHARACTERISTIC("00001542-0000-3512-2118-0009af100700");
