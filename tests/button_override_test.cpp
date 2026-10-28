#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

const uint8_t LOW = 0;
const uint8_t HIGH = 1;
const uint8_t INPUT_PULLUP = 2;
const uint8_t A0 = 14;
const uint8_t A1 = 15;

static uint32_t fakeTime;
static int brightnessInput;
static int speedInput;
static int buttonInput;
static int configuredPin;
static int configuredMode;
static unsigned int pinModeCalls;

static void check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(1);
  }
}

void pinMode(uint8_t pin, uint8_t mode) {
  configuredPin = pin;
  configuredMode = mode;
  ++pinModeCalls;
}
int digitalRead(uint8_t pin) {
  check(pin == 10, "button must be read from D10");
  return buttonInput;
}
int analogRead(uint8_t pin) {
  check(pin == A0 || pin == A1, "unexpected analog input");
  return pin == A0 ? brightnessInput : speedInput;
}
unsigned long millis() { return fakeTime; }
void delay(unsigned long duration) { fakeTime += uint32_t(duration); }
long map(long value, long inMin, long inMax, long outMin, long outMax) {
  return (value - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

#ifndef SKETCH_PATH
#define SKETCH_PATH "../rocket-led-arduino/rocket-led-arduino.ino"
#endif
#include SKETCH_PATH

static uint32_t expectedWhite(uint8_t brightness) {
  return uint32_t(brightness) * 0x010101;
}

static void resetFixture(int initialButton = HIGH) {
  fakeTime = 0;
  brightnessInput = 512;
  speedInput = 0;
  buttonInput = initialButton;
  configuredPin = configuredMode = -1;
  pinModeCalls = 0;
  chaseStep = phaseIndex = 0;
  lastChase = lastPhase = 0;
  chaseDelayMs = 100;
  phaseDelayMs = 1200;
  pixels.begun = false;
  pixels.showCount = pixels.invalidWrites = 0;
  setup();
}

static void checkWhiteFrame(uint8_t brightness) {
  for (int i = 0; i < 108; ++i) {
    check(pixels.shownColor(i) == expectedWhite(brightness),
          "every LED must be solid white at the selected brightness");
  }
  check(pixels.invalidWrites == 0, "LED writes must stay within bounds");
}

// Expected physical output, independent of the sketch's PHASES/color helpers.
static void checkNormalFrame(int phase, int step, uint8_t brightness) {
  const int starts[] = {33, 42, 46, 58, 63, 74, 80, 88, 102};
  const int ends[] = {41, 45, 57, 62, 73, 79, 87, 101, 107};
  const uint32_t white = expectedWhite(brightness);
  const uint32_t blue = (uint32_t(brightness / 7) << 16) |
                        (uint32_t(brightness / 7) << 8) | brightness;
  for (int i = 0; i <= 32; ++i) {
    int patternIndex = ((i - step) % 4 + 4) % 4;
    check(pixels.shownColor(i) == (patternIndex < 2 ? white : blue),
          "release must restore the white/blue chase across Segment A");
  }
  for (int i = 33; i < 108; ++i) {
    uint32_t expected = 0;
    if (phase < 9 && i >= starts[phase] && i <= ends[phase]) {
      expected = white;
      if (phase == 5 && i == 74) {
        expected = (uint32_t(brightness) << 16) |
                   (uint32_t(brightness / 3) << 8);
      }
    }
    check(pixels.shownColor(i) == expected,
          "release must restore the phase and clear inactive Segment B LEDs");
  }
  check(pixels.invalidWrites == 0, "LED writes must stay within bounds");
}

static void testSetup() {
  resetFixture();
  check(pinModeCalls == 1 && configuredPin == 10 && configuredMode == INPUT_PULLUP,
        "setup must configure D10 as INPUT_PULLUP");
  check(pixels.begun, "NeoPixel output must be initialized");
  for (int i = 0; i < 108; ++i) {
    check(pixels.shownColor(i) == 0, "setup must initialize all LEDs off");
  }
}

static void testReleased() {
  resetFixture();
  loop();
  checkNormalFrame(0, 0, 128);
}

static void testPressedAtStartup() {
  resetFixture(LOW);
  loop();
  checkWhiteFrame(128);
}

static void testPressDuringEveryPhase() {
  for (int phase = 0; phase < 10; ++phase) {
    resetFixture();
    phaseIndex = phase;
    loop();
    buttonInput = LOW;
    unsigned int previousShows = pixels.showCount;
    loop();
    checkWhiteFrame(128);
    check(pixels.showCount == previousShows + 1,
          "a pressed loop must send only one white frame");
  }
}

static void testLongHold() {
  resetFixture(LOW);
  speedInput = 1023;
  for (int frame = 0; frame < 2000; ++frame) {
    unsigned int previousShows = pixels.showCount;
    loop();
    checkWhiteFrame(128);
    check(pixels.showCount == previousShows + 1,
          "holding the button must never send an intermediate pattern frame");
  }
}

static void testBrightnessWhileHeld() {
  resetFixture(LOW);
  const int inputs[] = {0, 3, 39, 40, 512, 1023, 400};
  const uint8_t expected[] = {10, 10, 10, 10, 128, 255, 100};
  for (unsigned int i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
    brightnessInput = inputs[i];
    loop();
    checkWhiteFrame(expected[i]);
  }
}

static void testReleaseDuringEveryPhase() {
  for (int phase = 0; phase < 10; ++phase) {
    resetFixture(LOW);
    brightnessInput = 400;
    phaseIndex = phase;
    loop();
    checkWhiteFrame(100);
    buttonInput = HIGH;
    loop();
    checkNormalFrame(phase, 0, 100);
  }
}

static void testAnimationTimeContinues() {
  resetFixture(LOW);
  loop();
  fakeTime += 5000;
  loop();
  checkWhiteFrame(128);
  buttonInput = HIGH;
  loop();
  checkNormalFrame(1, 1, 128);
}

static void testRepeatedPressRelease() {
  resetFixture();
  for (int cycle = 0; cycle < 20; ++cycle) {
    loop();
    buttonInput = LOW;
    loop();
    checkWhiteFrame(128);
    buttonInput = HIGH;
    loop();
    checkNormalFrame(phaseIndex, chaseStep, 128);
  }
}

int main() {
  const struct { const char* name; void (*run)(); } tests[] = {
    {"D10 INPUT_PULLUP setup", testSetup},
    {"normal pattern while released", testReleased},
    {"button held at startup", testPressedAtStartup},
    {"white override in all ten phases", testPressDuringEveryPhase},
    {"continuous white through a 20-second hold", testLongHold},
    {"live brightness and min/max limits while held", testBrightnessWhileHeld},
    {"release restores all ten phases, including orange/off", testReleaseDuringEveryPhase},
    {"animation time continues underneath white override", testAnimationTimeContinues},
    {"repeated press/release cycles", testRepeatedPressRelease},
  };
  for (unsigned int i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    tests[i].run();
    printf("PASS: %s\n", tests[i].name);
  }
  puts("All 9 behavior tests passed.");
  return 0;
}
