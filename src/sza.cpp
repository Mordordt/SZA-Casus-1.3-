
#include <WiFi.h>
#include <Wire.h>
#include "esp_camera.h"
#include "HX711.h"
#include <time.h>
#include <SD.h>
#include "esp_sleep.h"

#define CAMERA_MODEL_TTGO_T_CAM_SIM
#include "select_pins.h"


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
void print_wakeup_reason();
bool setupMagneticContactSensor();
bool deviceProbe(uint8_t addr);
bool setupSDCard();
bool setupCamera();
void setupNetwork();
bool captureAndSaveImage();
void hx711_hard_reset(int sckPin);
bool readMagneticContact();
void setup();
void loop();

#if defined(SOFTAP_MODE)
#endif
String macAddress = "";
String ipAddress = "";

// HX711 circuit wiring
const int LOADCELL_DOUT_PIN = 16;
const int LOADCELL_SCK_PIN = 4;

HX711 scale;

float calibration_factor = -130; // initiële waarde, na inbouwen definitief calibreren

// Image capture timing
static unsigned long lastCaptureTime = 0;
static const unsigned long CAPTURE_INTERVAL = 5000; // 1 second in milliseconds

extern void startCameraServer();

void print_wakeup_reason() {
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

bool deviceProbe(uint8_t addr)
{
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

#if defined(SDCARD_CS_PIN) || defined(SD_CS)
#include <SD.h>
#endif
bool setupSDCard()
{
    /*
        T-CameraPlus Board, SD shares the bus with the LCD screen.
        It does not need to be re-initialized after the screen is initialized.
        If the screen is not initialized, the initialization SPI bus needs to be turned on.
    */
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

bool setupCamera()
{
    camera_config_t config;

#if defined(Y2_GPIO_NUM)
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
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
#endif

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

bool captureAndSaveImage()
{
#if defined(SD_CS)
    // Get current time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    
    // Format filename with timestamp
    char filename[64];
    strftime(filename, sizeof(filename), "/capture_%Y_%m_%d_%H_%M_%S.jpg", timeinfo);
    
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
#else
    Serial.println("SD Card not configured");
    return false;
#endif
}

void hx711_hard_reset(int sckPin) {
  pinMode(sckPin, OUTPUT);
  digitalWrite(sckPin, HIGH);
  delayMicroseconds(100);   // >60 µs
  digitalWrite(sckPin, LOW);
  delay(500);
}

void scale_start() {
    scale.begin(HX711_DT, HX711_SCK);

    // GEEN is_ready()
    Serial.println("Reading raw data...");

    long raw = scale.read_average(10);
    Serial.print("Raw value: ");
    Serial.println(raw);

    scale.set_scale(calibration_factor);

    Serial.println("Remove all weight");
    // delay(2000);
    scale.tare();

    Serial.println("Tare done");

    // if (scale.is_ready()) {
    //     scale.set_scale();    
    //     Serial.println("Tare... remove any weights from the scale.");
    //     delay(5000);
    //     scale.tare();
    //     Serial.println("Tare done...");
    //     Serial.print("Place a known weight on the scale...");
    //     delay(5000);
    //     long reading = scale.get_units(10);
    //     Serial.print("Result: ");
    //     Serial.println(reading);
    // } 
    // else {
    //     Serial.println("HX711 not found.");
    // }
    // delay(1000);
}

void setupTime() {
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
}

void readScale() {
    long raw = scale.read();   // lage-level check
    float weight = scale.get_units(5);

    Serial.print("Raw: ");
    Serial.print(raw);
    Serial.print("  Weight: ");
    Serial.print(weight, 2);
    Serial.println(" g");
}

bool readMagneticContact() {
  // Read the magnetic contact sensor
  // Returns true if contact is closed (magnet present)
  // Returns false if contact is open (magnet absent)
  bool contactState = digitalRead(MAGNETIC_CONTACT_PIN);
  return contactState;
}

void startSleep() {
    Serial.print("Go to sleep: ");
    esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);

    vTaskDelay(pdMS_TO_TICKS(1000)); // wait for serial comm to finish

    esp_deep_sleep_start();
    // esp_light_sleep_start();

    // vTaskDelay(pdMS_TO_TICKS(1000)); // wait for serial to stabilize

    // // Execution resumes HERE after wakeup
    // printf("Woke up!\n");
    // printf("Cause: %d\n", esp_sleep_get_wakeup_cause());
    
}


void setup()
{

#if defined(PWR_ON_PIN)
    pinMode(PWR_ON_PIN, OUTPUT);
    digitalWrite(PWR_ON_PIN, 1);
#endif

    Serial.begin(115200);
    uint32_t start = millis();
    // Wait for serial communication to be established, and avoid hanging when the serial monitor is not open
    // delay(5000);
    while (!Serial && millis() - start < 5000) {
        delay(10);
    }

    print_wakeup_reason();

#if defined(I2C_SDA) && defined(I2C_SCL)
    Wire.begin(I2C_SDA, I2C_SCL);
#endif

    scale_start();

    bool status;

    status = setupMagneticContactSensor();
    Serial.print("setupMagneticContactSensor status ");
    Serial.println(status);

    status = setupCamera();
    Serial.print("setupCamera status ");
    Serial.println(status);

    status = setupSDCard();
    Serial.print("setupSDCard status ");
    Serial.println(status);

    // if (!status) {
    //     delay(10000);
    //     esp_restart();
    // }

    setupNetwork();

    setupTime();

    //startCameraServer();

    // Serial.print("Camera Ready! Use 'http://");
    // Serial.print(ipAddress);
    // Serial.println("' to connect");

    //hx711_hard_reset(HX711_SCK);

    //LED logic test
    // pinMode(LED_PIN, OUTPUT);
    // digitalWrite(LED_PIN, HIGH);
    // delay(1000);
    // digitalWrite(LED_PIN, LOW);
    // delay(1000);
    // digitalWrite(LED_PIN, HIGH);

    //TODO WiFi signal strength check
    int rssi = WiFi.RSSI();
    Serial.print("WiFi Signal Strength: ");
    Serial.println(rssi);
    
}

void loop()
{

    // // Capture image and take scale reading every CAPTURE_INTERVAL milliseconds
    // unsigned long currentTime = millis();
    // if (currentTime - lastCaptureTime >= CAPTURE_INTERVAL) {

    //     long raw = scale.read();   // lage-level check
    //     float weight = scale.get_units(5);

    //     // Serial.print("Raw: ");
    //     // Serial.print(raw);
    //     // Serial.print("  Weight: ");
    //     // Serial.print(weight, 2);
    //     // Serial.println(" g");

    //     // Read magnetic contact sensor
    //     bool contactState = readMagneticContact();
    //     Serial.print("Magnetic Contact: ");
    //     Serial.println(contactState ? "CLOSED" : "OPEN");

    //     lastCaptureTime = currentTime;
    //     //captureAndSaveImage();
    // }

    //Take scale reading directly after wakeup, to check if the scale is still responsive after deep sleep
    void readScale(); 


    // Read magnetic contact sensor
    // Wait for the magnetic contact to be closed, indicating the lid closed again
    while (!readMagneticContact()) {
        Serial.println("Waiting for magnetic contact to close...");
        Serial.print("Magnetic Contact: ");
        Serial.println(readMagneticContact() ? "CLOSED" : "OPEN");

        delay(2000);
    }

    // Once the contact is closed, take a new scale reading and go to sleep
    readScale();

    startSleep();
}
