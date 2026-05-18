#include <HX711.h>

#define HX711_DT  21
#define HX711_SCK 45

HX711 scale;
float calibration_factor = -7050.0; // later afstellen

// void hx711_hard_reset(int sckPin) {
//   pinMode(sckPin, OUTPUT);
//   digitalWrite(sckPin, HIGH);
//   delayMicroseconds(100);   // >60 µs
//   digitalWrite(sckPin, LOW);
//   delay(500);
// }

void setup() {
  Serial.begin(115200);
  delay(300);

  // hx711_hard_reset(HX711_SCK);

  scale.begin(HX711_DT, HX711_SCK);

  // ❗ GEEN is_ready()
  Serial.println("Reading raw data...");

  long raw = scale.read_average(10);
  Serial.print("Raw value: ");
  Serial.println(raw);

  scale.set_scale(calibration_factor);

  Serial.println("Remove all weight");
  delay(2000);
  scale.tare();

  Serial.println("Tare done");
}

void loop() {
  long raw = scale.read();   // lage-level check
  float weight = scale.get_units(5);

  Serial.print("Raw: ");
  Serial.print(raw);
  Serial.print("  Weight: ");
  Serial.print(weight, 2);
  Serial.println(" g");

  delay(500);
}