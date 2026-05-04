#include <SoftwareSerial.h>

// -----------------------------------------------------------------------------
// Float mode detector for plow controller
//
// Hardware wiring:
//   RS-485 module RO -> Arduino D2
//   RS-485 module RE -> GND
//   RS-485 module DE -> GND
//   RS-485 module DI -> not used
//
// Outputs:
//   D5  -> external indicator light
//   D13 -> onboard LED mirror
//
// Notes:
// - This sketch watches the incoming controller byte stream and tries to detect
//   float mode based on the observed "floatPairs" and "idlePairs" patterns.
// - This is the best current working version from testing.
// - It works well in normal use, but it can still miss some edge-case
//   transitions if the controller signal is interrupted at exactly the wrong
//   time during float entry.
// -----------------------------------------------------------------------------

SoftwareSerial ctrlSerial(2, 3); // RX, TX

// Output pins
const int floatOutPin = 5;   // D5 powers the external light
const int ledPin      = 13;  // onboard LED mirrors float state

// Rolling buffer for recent bytes
const int WINDOW_SIZE = 80;
uint8_t buf[WINDOW_SIZE];
int bufCount = 0;

// Current detected float state
bool floatMode = false;

// Prevents instant back-to-back switching
unsigned long lastSwitchMs = 0;

// Timing constants
const unsigned long MIN_SWITCH_GAP_MS      = 100;
const unsigned long FLOAT_ENTRY_TIMEOUT_MS = 2000;
const unsigned long FLOAT_ENTRY_STALE_MS   = 1500;

// Float-entry tracking
//
// Observed behavior:
// - During float entry, floatPairs often reaches 77 first
// - Then stronger float-like values continue afterward
//
// Current rule:
// - Accumulate at least 4 hits of exact 77
// - Then accumulate at least 4 confirmations while floatPairs stays >= 70
// - Allow some interruption within the timeout/stale windows
bool floatEntryArmed = false;
unsigned long floatEntryStartMs = 0;
unsigned long lastFloatEntryHitMs = 0;
int count77 = 0;
int count79 = 0;   // "count79" here really means strong float confirmations

// Float-off tracking
int idleConfirmCount = 0;
const int FLOAT_OFF_IDLE_MIN = 2;

// -----------------------------------------------------------------------------
// Push a new byte into the rolling buffer
// -----------------------------------------------------------------------------
void pushByte(uint8_t b) {
  if (bufCount < WINDOW_SIZE) {
    buf[bufCount++] = b;
  } else {
    for (int i = 0; i < WINDOW_SIZE - 1; i++) {
      buf[i] = buf[i + 1];
    }
    buf[WINDOW_SIZE - 1] = b;
  }
}

// -----------------------------------------------------------------------------
// Count "float-like" adjacent byte pairs in the rolling window
//
// A float-like pair is either:
//   FF 00
//   00 FF
// -----------------------------------------------------------------------------
int countFloatPairs() {
  int count = 0;
  for (int i = 0; i < bufCount - 1; i++) {
    if ((buf[i] == 0xFF && buf[i + 1] == 0x00) ||
        (buf[i] == 0x00 && buf[i + 1] == 0xFF)) {
      count++;
    }
  }
  return count;
}

// -----------------------------------------------------------------------------
// Count "idle-like" adjacent byte pairs in the rolling window
//
// An idle-like pair is either:
//   63 00
//   00 63
// -----------------------------------------------------------------------------
int countIdlePairs() {
  int count = 0;
  for (int i = 0; i < bufCount - 1; i++) {
    if ((buf[i] == 0x63 && buf[i + 1] == 0x00) ||
        (buf[i] == 0x00 && buf[i + 1] == 0x63)) {
      count++;
    }
  }
  return count;
}

// -----------------------------------------------------------------------------
// Set detected float state and update outputs
// -----------------------------------------------------------------------------
void setFloatMode(bool on) {
  floatMode = on;
  digitalWrite(floatOutPin, on ? HIGH : LOW);
  digitalWrite(ledPin,      on ? HIGH : LOW);

  Serial.println(on ? "FLOAT ON" : "FLOAT OFF");
}

// -----------------------------------------------------------------------------
// Reset the float-entry state machine
// -----------------------------------------------------------------------------
void resetFloatEntry() {
  floatEntryArmed = false;
  floatEntryStartMs = 0;
  lastFloatEntryHitMs = 0;
  count77 = 0;
  count79 = 0;
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  ctrlSerial.begin(115200);

  pinMode(floatOutPin, OUTPUT);
  pinMode(ledPin, OUTPUT);

  digitalWrite(floatOutPin, LOW);
  digitalWrite(ledPin, LOW);

  Serial.println("Float detector starting...");
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------
void loop() {
  while (ctrlSerial.available()) {
    uint8_t b = ctrlSerial.read();
    pushByte(b);

    // Wait until the rolling window has enough data to be meaningful
    if (bufCount < 20) {
      continue;
    }

    int floatPairs = countFloatPairs();
    int idlePairs  = countIdlePairs();
    unsigned long now = millis();

    // -------------------------------------------------------------------------
    // FLOAT ON detection
    // -------------------------------------------------------------------------
    if (!floatMode) {
      // If we see strong float-like traffic, start or continue float entry
      if (floatPairs >= 77) {
        if (!floatEntryArmed) {
          floatEntryArmed = true;
          floatEntryStartMs = now;
          lastFloatEntryHitMs = now;
          count77 = 0;
          count79 = 0;
        }

        lastFloatEntryHitMs = now;

        // Exact 77 is treated as an early entry signature
        if (floatPairs == 77) {
          count77++;
        }

        // Once enough 77s have been seen, count stronger float confirmations
        if (count77 >= 4 && floatPairs >= 70) {
          count79++;
        }
      }

      // If the entry attempt sits too long or goes stale, reset it
      if (floatEntryArmed) {
        bool timedOut = (now - floatEntryStartMs > FLOAT_ENTRY_TIMEOUT_MS);
        bool stale    = (lastFloatEntryHitMs != 0 &&
                         (now - lastFloatEntryHitMs > FLOAT_ENTRY_STALE_MS));

        if (timedOut || stale) {
          Serial.print("RESET ENTRY: ");
          if (timedOut) Serial.print("timeout ");
          if (stale)    Serial.print("stale ");
          Serial.print(" c77=");
          Serial.print(count77);
          Serial.print(" c79=");
          Serial.println(count79);

          resetFloatEntry();
        }
      }

      // Turn float on once both entry checks have passed
      if (floatEntryArmed &&
          count77 >= 4 &&
          count79 >= 4 &&
          now - lastSwitchMs > MIN_SWITCH_GAP_MS) {
        setFloatMode(true);
        lastSwitchMs = now;
        idleConfirmCount = 0;
        resetFloatEntry();
      }
    }

    // -------------------------------------------------------------------------
    // FLOAT OFF detection
    // -------------------------------------------------------------------------
    if (floatMode) {
      // If idle begins to dominate, start counting confirmations
      if (idlePairs >= 10 && idlePairs > floatPairs) {
        idleConfirmCount++;
      } else {
        idleConfirmCount = 0;
      }

      // Turn float off after enough idle confirmations
      if (idleConfirmCount >= FLOAT_OFF_IDLE_MIN &&
          now - lastSwitchMs > MIN_SWITCH_GAP_MS) {
        setFloatMode(false);
        lastSwitchMs = now;
        idleConfirmCount = 0;
        resetFloatEntry();
      }
    }
  }
}