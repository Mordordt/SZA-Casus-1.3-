
#include <WiFi.h>
#include <Wire.h>
#include "esp_camera.h"
#include "HX711.h"
#include <time.h>
#include <SD.h>
#include "esp_sleep.h"
#include <esp_now.h>

#define CAMERA_MODEL_TTGO_T_CAM_SIM
#include "select_pins.h"
#include "magnetic_contact.h"


/***************************************
 *  Function
 **************************************/
// #define SOFTAP_MODE       //The comment will be connected to the specified ssid

// When there is BME280, set the reading time here
#define DEFAULT_MEASUR_MILLIS 3000 /* Get sensor time by default (ms)*/

// When using timed sleep, set the sleep time here
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 30        /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int boot_count = 0; // survives deep sleep

volatile bool weightReceived = false;
float receivedWeight = 0;

/***************************************
 *  WiFi
 **************************************/
// #define WIFI_SSID "sza"
// #define WIFI_PASSWD "sza_esp_2026" 
#define WIFI_SSID "moto g 5G plus 2457"
#define WIFI_PASSWD "sjefenben" 

/***************************************
 *  Forward declarations
 **************************************/
void performWakeUpLogic();
bool setupMagneticContactSensor();
bool setupSDCard();
bool setupCommunicationWithCameraESP();
bool setupCamera();
void setupNetwork();
String getTime();
void checkWifiSignalStrength();
void processWeight(float weight, const String& timestamp);
float getLastWeight();
void updateLastWeight(float weight, const String& timestamp);
bool captureAndSaveImage(const String& timestamp);
void hx711_hard_reset(int sckPin);
void setup();
void loop();

String macAddress = "";
String ipAddress = "";

// Image capture timing
static unsigned long lastCaptureTime = 0;
static const unsigned long CAPTURE_INTERVAL = 5000; // 1 second in milliseconds

extern void startCameraServer();

void performWakeUpLogic() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:  printf("Timer wakeup\n"); break;
        case ESP_SLEEP_WAKEUP_EXT0:   printf("EXT0 wakeup\n"); break;
        case ESP_SLEEP_WAKEUP_EXT1:   printf("EXT1 wakeup\n"); break;
        case ESP_SLEEP_WAKEUP_UNDEFINED: printf("Not a wakeup reset\n"); break;
        default: printf("Other cause: %d\n", cause); break;
    }
}

bool setupMagneticContactSensor()
{

    #if defined(MAGNETIC_CONTACT_PIN)
        pinMode(MAGNETIC_CONTACT_PIN, INPUT_PULLDOWN);
    #endif

    return true;
}

bool setupSDCard()
{

    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);

#if defined(SD_CS)
    if (!SD.begin(SD_CS)) {
        Serial.println("SDCard begin failed");
        return false;
    } else {
        String cardInfo = String(((uint32_t)SD.cardSize() / 1024 / 1024));

        Serial.print("SDcardSize=[");
        Serial.print(cardInfo);
        Serial.println("]MB");
    }
#endif

    return true;
}

bool setupCommunicationWithCameraESP() {
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_register_recv_cb(onDataReceived);

    // Print MAC address for debugging
    Serial.println(WiFi.macAddress());

    return true;
}

bool setupCamera()
{
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;
    // init with high specs to pre-allocate larger buffers
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA;
        config.jpeg_quality = 8;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);       // flip it back
        s->set_brightness(s, 1);  // up the blightness just a bit
        s->set_saturation(s, -2); // lower the saturation
    }
    // drop down frame size for higher initial frame rate
    s->set_framesize(s, FRAMESIZE_QVGA);

    return true;
}

void setupNetwork()
{
    // Deinitialize ESP-NOW to free up WiFi resources
    esp_now_deinit();

    macAddress = "LilyGo-CAM-";
#ifdef SOFTAP_MODE
    WiFi.mode(WIFI_AP);
    macAddress += WiFi.softAPmacAddress().substring(0, 5);
    WiFi.softAP(macAddress.c_str());
    ipAddress = WiFi.softAPIP().toString();
#else
    WiFi.begin(WIFI_SSID, WIFI_PASSWD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected ("+WiFi.networkID().toString()+")");
    ipAddress = WiFi.localIP().toString();
    macAddress += WiFi.macAddress().substring(0, 5);
#endif

}

bool captureAndSaveImage(const String& timestamp)
{
    // Format filename with provided timestamp
    char filename[64];
    snprintf(filename, sizeof(filename), "/capture_%s.jpg", timestamp.c_str());

    // Capture frame from camera
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return false;
    }

    // Open file for writing
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.print("Failed to open file: ");
        Serial.println(filename);
        esp_camera_fb_return(fb);
        return false;
    }

    // Write image data to file
    if (file.write(fb->buf, fb->len) != fb->len) {
        Serial.println("Failed to write image to SD card");
        file.close();
        esp_camera_fb_return(fb);
        return false;
    }

    file.close();
    esp_camera_fb_return(fb);

    Serial.print("Image saved: ");
    Serial.println(filename);

    return true;
}

String getTime() {
    // Synchronize time with NTP server
    Serial.println("Syncing time with NTP...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr);
    int attempts = 0;
    while (now < 24 * 3600 && attempts < 20) {
        delay(500);
        now = time(nullptr);
        attempts++;
    }
    Serial.println();
    Serial.print("Current time: ");
    Serial.println(ctime(&now));

    // Return formatted string
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
    Serial.println(timestamp);

    return String(timestamp);
}

void checkWifiSignalStrength() {
    //TODO WiFi signal strength check
    int rssi = WiFi.RSSI();
    Serial.print("WiFi Signal Strength: ");
    Serial.println(rssi);

    const char *quality;

    if      (rssi > -50) quality = "Excellent";
    else if (rssi > -60) quality = "Good";
    else if (rssi > -70) quality = "Fair";
    else if (rssi > -80) quality = "Weak";
    else                 quality = "Very poor";

    Serial.printf("RSSI: %d dBm (%s)\n", rssi, quality);
}

void onDataReceived(const uint8_t *mac, const uint8_t *data, int len) {
    memcpy(&receivedWeight, data, sizeof(float));
    weightReceived = true;
    Serial.print("Received weight: ");
    Serial.println(receivedWeight, 2);
}

void processWeight(float weight, const String& timestamp) {

    float lastWeight = getLastWeight();

    if (lastWeight >= 0 && weight < lastWeight) {
        Serial.println("Container change detected at " + timestamp);
        registerContainerChange(lastWeight, weight, timestamp);
    }

    updateLastWeight(weight, timestamp);
}

float getLastWeight() {
    File file = SD.open("/last_weight.txt", FILE_READ);
    if (!file) {
        return -1.0f;
    }

    float weight = file.readStringUntil('\n').toFloat();
    file.close();
    return weight;
}

void updateLastWeight(float weight, const String& timestamp) {
    // Save to CSV for historical data
    File file = SD.open("/weights.csv", FILE_APPEND);
    if (!file) {
        return;
    }

    // Write header if file is empty
    if (file.size() == 0) {
        file.println("timestamp,weight_g");
    }

    file.print(timestamp);
    file.print(",");
    file.println(weight, 3);

    file.close();

    // Save to txt file for quick access to the latest weight
    File file = SD.open("/last_weight.txt", FILE_WRITE);
    if (!file) {
        return;
    }

    file.println(weight, 3);
    file.close();
}

void registerContainerChange(float lastWeight, float weight, const String& timestamp) {
    // Register the container change in a separate CSV file for easier analysis of changes over time
    File file = SD.open("/container_changes.csv", FILE_APPEND);
    if (!file) {
        return;
    }

    if (file.size() == 0) {
        file.println("timestamp,weight_before_g,weight_after_g,difference_g");
    }

    file.print(timestamp);       file.print(",");
    file.print(lastWeight, 3);   file.print(",");
    file.print(weight, 3);       file.print(",");
    file.println(lastWeight - weight, 3);

    file.close();
}

void startSleep() {
    Serial.print("Go to sleep: ");
    // esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for serial comm to finish

    esp_deep_sleep_start();
    
}


void setup()
{

#if defined(PWR_ON_PIN)
    pinMode(PWR_ON_PIN, OUTPUT);
    digitalWrite(PWR_ON_PIN, 1);
#endif

    Serial.begin(115200);
    // uint32_t start = millis();
    // // Wait for serial communication to be established, and avoid hanging when the serial monitor is not open
    // // delay(5000);
    // while (!Serial && millis() - start < 2000) {
    //     delay(10);
    // }

    performWakeUpLogic();

#if defined(I2C_SDA) && defined(I2C_SCL)
    Wire.begin(I2C_SDA, I2C_SCL);
#endif

    bool status;

    status = setupMagneticContactSensor();
    Serial.print("setupMagneticContactSensor status ");
    Serial.println(status);

    status = setupSDCard();
    Serial.print("setupSDCard status ");
    Serial.println(status);

    status = setupCommunicationWithCameraESP();
    Serial.print("setupCommunicationWithCameraESP status ");
    Serial.println(status);

    status = setupCamera();
    Serial.print("setupCamera status ");
    Serial.println(status);

}

void loop()
{

    // Read magnetic contact sensor
    // Wait for the magnetic contact to be closed, indicating the lid closed again
    while (!readMagneticContact()) {
        Serial.println("Waiting for magnetic contact to close...");
        Serial.print("Magnetic Contact: ");
        Serial.println(readMagneticContact() ? "CLOSED" : "OPEN");

        delay(2000);
    }



    // Wait for weight packet
    uint32_t start = millis();
    while (!weightReceived) {
        if (millis() - start > 600000) break;
        delay(4000);
        Serial.println("Waiting for weight data... ");
    }

    //Change to WiFi for fetching current time only after receiving the weight, to save time in setup
    setupNetwork();
    checkWifiSignalStrength();
    String timestamp = getTime();

    if (weightReceived) {
        processWeight(receivedWeight, timestamp);
    }

    captureAndSaveImage(timestamp);

    startSleep();
}
