/*
  Speaker Watchdog - ATtiny1607
  
  Monitors speaker power LED via LDR + comparator.
  When LED goes OFF → wake → servo presses Fingerbot → wait → sleep.
  
  Target: ATtiny1607
  Core: megaTinyCore (https://github.com/SpenceKonde/megaTinyCore)
  
  Hardware:
    PC2 (pin 19) - Trigger input (comparator + button, shared via R8)
    PA5 (pin 6)  - Servo PWM (TCA0 WO5)
    PB2 (pin 14) - TX (debug serial, TX-only)
  
  Author: Saurabh Datta
  Date: February 2026
  License: MIT
*/

#include <avr/sleep.h>
#include <Servo_megaTinyCore.h>

// ==================== CONFIGURATION ====================
// See README.md for full documentation

// Debug output over serial (TX-only, 115200 baud)
// Comment out for production to save power
// #define DEBUG_ENABLED

// Trigger polarity - depends on which LDR module you're using:
//   true  = off-shelf test module (PC2 HIGH when LED OFF)
//   false = our custom PCB (PC2 LOW when LED OFF)
#define INVERT_TRIGGER false

// Servo positions in degrees - calibrate for your Fingerbot setup
#define SERVO_REST 30   // Resting position (not touching button)
#define SERVO_PRESS 76  // Press position (pushing Fingerbot button)

// Timing in milliseconds
#define DEBOUNCE_MS 50        // Wait after wake before validating trigger
#define PRESS_HOLD_MS 1600    // How long servo holds the press position
#define PRESS_SETTLE_MS 1000  // Wait for servo to return to rest before detach
#define SERVO_INIT_MS 1000    // Initial servo settle time at boot
#define BOOT_WAIT_MS 8000     // Wait for speaker boot (covers LED dance)
#define SERVO_PRESS_AWAIT 2000

// ==================== PIN DEFINITIONS ====================

#define TRIGGER_PIN PIN_PC2
#define SERVO_PIN PIN_PA5

// ==================== GLOBALS ====================

Servo servo;
volatile byte triggered = 0;
bool serialActive = false;  // Track if Serial was started

// ==================== ISR ====================

ISR(PORTC_PORT_vect) {
  byte flags = VPORTC.INTFLAGS;
  PORTC.INTFLAGS = flags;
  triggered = 1;
}

// ==================== HARDWARE HELPERS ====================

void disableUnusedPins() {
  // PORTA: PA0=UPDI(skip), PA5=SERVO(skip), rest unused
  PORTA.PIN1CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN2CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN3CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN4CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN6CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN7CTRL = PORT_PULLUPEN_bm;

  // PORTB: PB0/PB1=TWI (handled by disableTWI), PB2=TX, PB3=RX (handled by disableSerialPins), rest unused
  PORTB.PIN4CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN5CTRL = PORT_PULLUPEN_bm;

  // PORTC: PC2=TRIGGER(skip), rest unused
  PORTC.PIN0CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN1CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN3CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN4CTRL = PORT_PULLUPEN_bm;
  PORTC.PIN5CTRL = PORT_PULLUPEN_bm;
}

void disableSerialPins() {
  PORTB.DIRSET = PIN2_bm;
  PORTB.DIRSET = PIN3_bm;
  cli();
  PORTB.OUT &= ~PIN2_bm;
  PORTB.OUT &= ~PIN3_bm;
  sei();
}

void disableTWI() {
  // Set TWI pins (PB0, PB1) as OUTPUT LOW for lowest power
  PORTB.DIRSET = PIN0_bm;
  PORTB.DIRSET = PIN1_bm;
  cli();
  PORTB.OUT &= ~PIN0_bm;
  PORTB.OUT &= ~PIN1_bm;
  sei();
}

void disablePeripherals() {
  ADC0.CTRLA &= ~ADC_ENABLE_bm;
  SPI0.CTRLA &= ~SPI_ENABLE_bm;
}

void setupTriggerPin() {
#if INVERT_TRIGGER
  PORTC.PIN2CTRL = PORT_ISC_RISING_gc;
#else
  PORTC.PIN2CTRL = PORT_ISC_FALLING_gc;
#endif
}

void disableTriggerInterrupt() {
  PORTC.PIN2CTRL &= ~PORT_ISC_gm;
}

void enableTriggerInterrupt() {
#if INVERT_TRIGGER
  PORTC.PIN2CTRL = PORT_ISC_RISING_gc;
#else
  PORTC.PIN2CTRL = PORT_ISC_FALLING_gc;
#endif
}

void clearTriggerFlags() {
  PORTC.INTFLAGS = PIN2_bm;
  triggered = 0;
}

// ==================== TRIGGER VALIDATION ====================

bool isValidTrigger() {
  bool pinHigh = (PORTC.IN & PIN2_bm);

#if INVERT_TRIGGER
  return pinHigh;
#else
  return !pinHigh;
#endif
}

// ==================== SERVO ====================

void servoPress() {
#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Servo: attach, move to PRESS ("));
    Serial.print(SERVO_PRESS);
    Serial.println(F(" deg)"));
  }
#endif

  servo.attach(SERVO_PIN);
  servo.write(SERVO_PRESS);

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Servo: hold "));
    Serial.print(PRESS_HOLD_MS);
    Serial.println(F("ms"));
  }
#endif

  delay(PRESS_HOLD_MS);

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Servo: move to REST ("));
    Serial.print(SERVO_REST);
    Serial.println(F(" deg)"));
  }
#endif

  servo.write(SERVO_REST);

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Servo: settle "));
    Serial.print(PRESS_SETTLE_MS);
    Serial.println(F("ms"));
  }
#endif

  delay(PRESS_SETTLE_MS);

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.println(F("Servo: detach"));
  }
#endif

  servo.detach();
  pinMode(SERVO_PIN, INPUT_PULLUP);
}

// ==================== MAIN HANDLER ====================

void handleWakeUp() {
#ifdef DEBUG_ENABLED
  Serial.begin(115200);
  serialActive = true;
  delay(10);
  Serial.println(F(""));
  Serial.println(F("=== WAKE ==="));
#endif

// 2. Debounce
#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Debounce: "));
    Serial.print(DEBOUNCE_MS);
    Serial.println(F("ms"));
  }
#endif

  delay(DEBOUNCE_MS);

#ifdef DEBUG_ENABLED
  if (serialActive) {
    bool pinState = (PORTC.IN & PIN2_bm);
    Serial.print(F("PC2: "));
    Serial.println(pinState ? "HIGH" : "LOW");
  }
#endif

  if (!isValidTrigger()) {
#ifdef DEBUG_ENABLED
    if (serialActive) {
      Serial.println(F("FALSE TRIGGER"));
    }
#endif
    return;
  }

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.println(F("VALID TRIGGER"));
    Serial.println(F("Disable PC2 interrupt"));
  }
#endif

  // 3. Disable interrupt
  disableTriggerInterrupt();

// ** NOTE: Add some await before Servo press
// Or else if we press immediately after the speaker gets OFF (speaker LED OFF),
// then pressing it, to turn it ON doesn't work
#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Waiting for: "));
    Serial.print(SERVO_PRESS_AWAIT);
    Serial.println(F("ms before pressing servo"));
  }
#endif

  delay(SERVO_PRESS_AWAIT);
  //============================= //

  // 4. Servo press
  servoPress();

// 5. Fixed wait
#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.print(F("Boot wait: "));
    Serial.print(BOOT_WAIT_MS);
    Serial.println(F("ms"));
  }
#endif

  delay(BOOT_WAIT_MS);

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.println(F("Boot wait done"));
  }
#endif
}

// ==================== SETUP ====================

void setup() {
  // 1. Disable serial pins
  disableSerialPins();

  // 2. Disable unused pins
  disableUnusedPins();

  // 2b. Disable TWI pins (PB0, PB1 as OUTPUT LOW)
  disableTWI();

  // 3-4. Disable peripherals
  disablePeripherals();

  // 5. Setup trigger pin
  setupTriggerPin();

  // 6-9. Initialize servo to rest, then detach
  servo.attach(SERVO_PIN);
  servo.write(SERVO_REST);
  delay(SERVO_INIT_MS);
  servo.detach();
  pinMode(SERVO_PIN, INPUT_PULLUP);

  // 10. Enable interrupts
  sei();

  // 11-12. Sleep mode setup
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
}

// ==================== LOOP ====================

void loop() {
  // 1. Check if triggered
  if (triggered) {
    triggered = 0;
    handleWakeUp();
  }

  // 6. Re-enable interrupt + cleanup + sleep
  enableTriggerInterrupt();
  clearTriggerFlags();

#ifdef DEBUG_ENABLED
  if (serialActive) {
    Serial.println(F("Enable PC2 interrupt"));
    Serial.println(F("Clear flags"));
    Serial.println(F("Sleep..."));
    Serial.flush();
    Serial.end();
    disableSerialPins();
    serialActive = false;
  }
#endif

  // Sleep
  sleep_cpu();
}
