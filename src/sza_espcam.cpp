
#include "HX711.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include <esp_now.h>
#include <WiFi.h>

#define CAMERA_MODEL_ESPCAM
#include "select_pins.h"
#include "magnetic_contact.h"

// When using timed sleep, set the sleep time here
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 30        /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int boot_count = 0; // survives deep sleep

// MAC address of the Camera ESP
uint8_t cameraMAC[] = {0x34, 0x85, 0x18, 0x8D, 0x5C, 0x60};
volatile bool sendConfirmed = false;

/***************************************
 *  Forward declarations
 **************************************/
void beginSerialCommunication();
void wakeUpLogic();
void setupScale();
void setupMagneticContactSensor();
void setupCommunicationWithCameraESP();
void resetHX711(int sckPin);
void enableContainerLight();
void disableContainerLight();
float readScale();
void onSent(const uint8_t *mac, esp_now_send_status_t status);
void sendWeightToCameraESP(float weight);
void setup();
void loop();

HX711 scale;

float calibration_factor = -25;//-130; // initiële waarde, na inbouwen definitief calibreren

// Image capture timing
static unsigned long lastCaptureTime = 0;
static const unsigned long CAPTURE_INTERVAL = 5000; // 1 second in milliseconds

void beginSerialCommunication() {
    Serial.begin(115200);
    uint32_t start = millis();
    // Wait for serial communication to be established, and avoid hanging when the serial monitor is not open
    while (!Serial && millis() - start < 200) {
        delay(10);
    }
}

void wakeUpLogic() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:  printf("Timer wakeup\n"); break;
        case ESP_SLEEP_WAKEUP_EXT0:   printf("EXT0 wakeup\n"); break;
        case ESP_SLEEP_WAKEUP_EXT1:   printf("EXT1 wakeup\n"); break;
        case ESP_SLEEP_WAKEUP_UNDEFINED: printf("Not a wakeup reset\n"); break;
        default: printf("Other cause: %d\n", cause); break;
    }
}

void blinkLed(int times, int delayTime) {

    // Disable hold on GPIO 4 to allow it to be used normally after wakeup
    rtc_gpio_hold_dis(GPIO_NUM_4);
    pinMode(4, OUTPUT);

    // Flash the LED the specified number of times for the specified duration
    for (int i = 0; i < times; i++) {
        
        digitalWrite(4, HIGH);
        delay(delayTime);
        digitalWrite(4, LOW);
        delay(delayTime);

    }

    // Re-enable hold on GPIO 4 to keep the LED off during deep sleep
    rtc_gpio_hold_en(GPIO_NUM_4);
}

void setupMagneticContactSensor()
{

    #if defined(MAGNETIC_CONTACT_PIN)
        pinMode(MAGNETIC_CONTACT_PIN, INPUT_PULLDOWN);
    #endif

}

void resetHX711(int sckPin) {
  pinMode(sckPin, OUTPUT);
  digitalWrite(sckPin, HIGH);
  delayMicroseconds(100);   // >60 µs
  digitalWrite(sckPin, LOW);
  delay(500);
}

void testScale() {
    for (int i = 0; i < 10; i++) {

        while (!scale.is_ready()) {
            Serial.println("HX711 not ready");
            delay(1000);
        }

        // Discard first few readings (settling)
        scale.read_average(10);
        
        // Take raw reading and weight reading
        long avg = scale.read_average(20);
        float weight = scale.get_units(20);

        Serial.print("  Avg: ");
        Serial.print(avg);
        Serial.print("  Weight: ");
        Serial.print(weight, 2);
        Serial.println(" g");
        delay(1000);
    }
}

void setupScale() {
    // resetHX711(HX711_SCK);

    scale.begin(HX711_DT, HX711_SCK);

    scale.set_scale(calibration_factor);

    //If tare pin is grounded, perform tare and calibration
    pinMode(TARE_PIN, INPUT_PULLUP);

    if (digitalRead(TARE_PIN) == 0) {
        blinkLed(3, 1000); // Blink the LED 3 times with 200ms delay to indicate tare and calibration
        while (digitalRead(TARE_PIN) == 0) {
            Serial.println("TARE pin still grounded, testing scale readings...");
            testScale();
        }
        long reading = scale.get_units(10);
        Serial.print("Reading before tare: ");
        Serial.println(reading);
        scale.set_scale();    
        Serial.println("Tare... remove any weights from the scale.");
        delay(5000);
        scale.tare();
        Serial.println("Tare done...");
        Serial.print("Place a known weight on the scale...");
        delay(5000);
        reading = scale.get_units(10);
        Serial.print("Reading after tare: ");
        Serial.println(reading);
        delay(1000);
    } 
    
}

void setupCommunicationWithCameraESP() {
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_register_send_cb(onSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, cameraMAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

void enableContainerLight() {

    // Disable hold on GPIO 2 to allow it to be used normally after wakeup
    rtc_gpio_hold_dis(GPIO_NUM_2);
    pinMode(LED_PIN, OUTPUT);

    digitalWrite(LED_PIN, HIGH);
}

void disableContainerLight() {

    // Wait for 5 seconds to make sure the camera ESP has made a picture
    delay(5000);

    digitalWrite(LED_PIN, LOW);

    // Re-enable hold on GPIO 2 to keep the light off during deep sleep
    rtc_gpio_hold_en(GPIO_NUM_2);
}

float readScale() {
    long raw = scale.read();   // lage-level check
    float weight = scale.get_units(20);

    Serial.print("Raw: ");
    Serial.print(raw);
    Serial.print("  Weight: ");
    Serial.print(weight, 2);
    Serial.println(" g");
    return weight;
}

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
    sendConfirmed = true;
    blinkLed(1, 100); // Blink the LED once for 100ms to indicate send confirmation
}

void sendWeightToCameraESP(float weight) {
    // Send the weight reading to the camera ESP
    Serial.print("Sending weight to camera ESP: ");
    Serial.println(weight, 2);
    
    sendConfirmed = false;
    esp_now_send(cameraMAC, (uint8_t*)&weight, sizeof(float));

    // Wait for send confirmation before sleeping
    uint32_t start = millis();
    while (!sendConfirmed) {
        if (millis() - start > 4000) break;
        delay(10);
    }
}

void startSleep() {
    Serial.print("Go to sleep ");
    blinkLed(2, 500); // Blink the LED 2 times with 500ms delay

    //esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    pinMode(MAGNETIC_CONTACT_PIN, INPUT_PULLDOWN);
    esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for serial comm to finish

    esp_deep_sleep_start();
    
}


void setup()
{

#if defined(PWR_ON_PIN)
    pinMode(PWR_ON_PIN, OUTPUT);
    digitalWrite(PWR_ON_PIN, 1);
#endif

    beginSerialCommunication();

    wakeUpLogic();

    setupScale();

    setupMagneticContactSensor();

    setupCommunicationWithCameraESP();
    
}

void loop()
{

    //Take scale reading directly after wakeup, to check if the scale is still responsive after deep sleep
    float initialWeight = readScale();

    // Read magnetic contact sensor
    // Wait for the magnetic contact to be closed, indicating the lid closed again
    while (!readMagneticContact()) {
        Serial.println("Waiting for magnetic contact to close...");
        Serial.print("Magnetic Contact: ");
        Serial.println(readMagneticContact() ? "CLOSED" : "OPEN");

        delay(2000);
    }

    // Once the lid is closed, switch the light in the container ON 
    enableContainerLight();

    // Once the contact is closed, take a new scale reading
    float finalWeight = readScale();

    // Send the reading to the camera ESP 
    sendWeightToCameraESP(finalWeight);

    disableContainerLight();

    startSleep();
}
