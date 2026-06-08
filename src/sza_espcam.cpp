
#include "HX711.h"
#include "esp_sleep.h"

#define CAMERA_MODEL_ESPCAM
#include "select_pins.h"

// When using timed sleep, set the sleep time here
#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 30        /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int boot_count = 0; // survives deep sleep

/***************************************
 *  Forward declarations
 **************************************/
void begin_serial_communication();
void print_wakeup_reason();
void setupScale();
bool setupMagneticContactSensor();
void hx711_hard_reset(int sckPin);
bool readMagneticContact();
void setup();
void loop();

HX711 scale;

float calibration_factor = -130; // initiële waarde, na inbouwen definitief calibreren

// Image capture timing
static unsigned long lastCaptureTime = 0;
static const unsigned long CAPTURE_INTERVAL = 5000; // 1 second in milliseconds

void begin_serial_communication() {
    Serial.begin(115200);
    uint32_t start = millis();
    // Wait for serial communication to be established, and avoid hanging when the serial monitor is not open
    while (!Serial && millis() - start < 2000) {
        delay(10);
    }
}

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

void hx711_hard_reset(int sckPin) {
  pinMode(sckPin, OUTPUT);
  digitalWrite(sckPin, HIGH);
  delayMicroseconds(100);   // >60 µs
  digitalWrite(sckPin, LOW);
  delay(500);
}

void setupScale() {
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
    // Option 1: Use internal pull-up
    pinMode(WAKE_PIN, INPUT_PULLDOWN);
    esp_sleep_enable_ext0_wakeup(WAKE_PIN, 0);

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

    begin_serial_communication();

    print_wakeup_reason();

    setupScale();

    bool status;

    status = setupMagneticContactSensor();
    Serial.print("setupMagneticContactSensor status ");
    Serial.println(status);

    //hx711_hard_reset(HX711_SCK);
    
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
    readScale();

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
