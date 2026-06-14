#ifndef MAGNETIC_CONTACT_H
#define MAGNETIC_CONTACT_H

#include <Arduino.h>
#include "select_pins.h"

bool readMagneticContact() {
  // Read the magnetic contact sensor
  // Returns true if contact is closed (magnet present)
  // Returns false if contact is open (magnet absent)
  bool contactState = digitalRead(MAGNETIC_CONTACT_PIN);
  return contactState;
}

#endif // MAGNETIC_CONTACT_H
