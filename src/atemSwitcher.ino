// atemSwitcher.ino

/**
 * ATEM H/W Switcher with auto and manual modes for changing the video output
 * based on signals from audio inputs or buttons.
 */

// Including libraries:
#include <SPI.h>
#include <SD.h>
#include <Ethernet2.h>
#include <EthernetUdp2.h>
#include <Streaming.h>
#include <Button.h>

// Include ATEMbase library and make an instance:
// The port number is chosen randomly among high numbers.
#include <ATEMbase.h>
#include <ATEMstd.h>
ATEMstd AtemSwitcher;

// Constants
#define PULLUP true
#define INVERT true
#define DEBOUNCE_MS 20
#define LONG_PRESS 1000

#define MIC_DEBOUNCE_MS 500
#define MIC_THRESHOLD 512 // 0-1023 5v

enum mode {
    automatic,
    manual
};
enum cutMode {
    cut,
    fade
};

// Pins
uint8_t modeButtonLED = A5;

uint8_t videoSourceLED[6] = {6,7,8,9,11,12};
uint8_t micPin[4] = {A0,A1,A2,A3};

Button vidSource1(0, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource2(1, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource3(2, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource4(3, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource5(4, PULLUP, INVERT, DEBOUNCE_MS);
Button vidSource6(5, PULLUP, INVERT, DEBOUNCE_MS);

Button cutButton(6, PULLUP, INVERT, DEBOUNCE_MS);
Button modeButton(7, PULLUP, INVERT, DEBOUNCE_MS);


// Mapping
uint16_t micToVideoSource[16] = {
    0, // 0000 No mics
    1, // 0001 Presenter Mic
    5, // 0010 Guest 1
    5, // 0011 P & G1
    5, // 0100 G2
    5, // 0101 P & G2
    5, // 0110 G1 & G2
    5, // 0111 P & G1 & G2
    5, // 1000 G3
    5, // 1001 P & G3
    5, // 1010 G1 & G3
    5, // 1011 P & G1 & G3
    5, // 1100 G2 & G3
    5, // 1101 P & G2 & G3
    5, // 1110 G1 & G2 & G3
    5, // 1111 P & G2 & G3
};
uint16_t defaultVideoSource = 5;

// Network Config
byte mac[] = {0x90, 0xA2, 0xDA, 0x0D, 0x6B, 0xB9};
IPAddress clientIP(10,64,160,134);
IPAddress switcherIP(10,64,160,133);

// States
uint32_t debounceButton[8]; // 0 mode, 1-6 source, 7 cut
uint8_t lastButtonState;
uint8_t buttonTrigger;
uint8_t buttonState = B11111111;
uint32_t debounceMic[4];
uint8_t lastMicState;
uint8_t micTrigger;
uint8_t micState = B00000000;

mode modeState = manual;
cutMode cutState = cut;
uint16_t videoSourceState;
uint16_t lastVideoSourceState;
bool doCut = false;
bool longCut = false;
uint32_t lastAutoChange;

void setup() {

    // Start up Ethernet and Serial (debugging)
    Ethernet.begin(mac,clientIP);
    Serial.begin(115200);
    Serial << F("\n- - - - - - - -\nSerial Started\n");

    AtemSwitcher.begin(switcherIP);
    AtemSwitcher.serialOutput(1);
    AtemSwitcher.connect();

}

void loop() {
    // Keep ATEM connection alive and receive any pending data
    AtemSwitcher.runLoop();
    updateFromATEM();

    for(uint8_t i = 0; i < 4; i++) {
        readMic(i);
    }
    updateState();
    updateATEM();
}

void readMic(uint8_t index) {
    uint16_t reading = analogRead(micPin[index]);
    if (reading > micThresholdLevel) {
        debounceTrigger(LOW, index, &lastMicState, &micState, &micTrigger, debounceMic, micBounceDelay);
    } else {
        debounceTrigger(HIGH, index, &lastMicState, &micState, &micTrigger, debounceMic, micBounceDelay);
    }
}

void debounceTrigger(uint8_t reading, uint8_t index, uint8_t *lastState, uint8_t *state, uint8_t *trigger, uint32_t *debounceTime, uint32_t debounceDelay) {
    if (reading != bitRead(*lastState, index)) {
        // State changed: reset de-bounce timer
        debounceTime[index] = millis();
    }

    if ((millis() - debounceTime[index]) > debounceDelay) {
        // State is stable
        if (reading != bitRead(*state, index)) {
            // State changed: update state
            bitWrite(*state, index, reading);
            if (reading == LOW) {
                // Trigger once per input change
                bitWrite(*trigger, index, HIGH);
            }
        }
    }
    // Update last read
    bitWrite(*lastState, index, reading);
}

void updateState() {
    if (modeState == automatic) {
        if (modeButton.wasReleased()) {
            Serial << F("Change to Manual\n");
            modeState = manual;
        } else {
            videoSourceState = updateFromMics();
        }
    } else { // modeState == manual
        if (modeButton.wasReleased()) {
            Serial << F("Change to Auto\n");
            modeState = automatic;
            configureATEM();
        } else if (vidSource1.wasReleased()) {
            Serial << F("Preview Input 1\n");
            videoSourceState = 1;
        } else if (vidSource2.wasReleased()) {
            Serial << F("Preview Input 2\n");
            videoSourceState = 2;
        } else if (vidSource3.wasReleased()) {
            Serial << F("Preview Input 3\n");
            videoSourceState = 3;
        } else if (vidSource4.wasReleased()) {
            Serial << F("Preview Input 4\n");
            videoSourceState = 4;
        } else if (vidSource5.wasReleased()) {
            Serial << F("Preview Input 5\n");
            videoSourceState = 5;
        } else if (vidSource6.wasReleased()) {
            Serial << F("Preview Input 6\n");
            videoSourceState = 6;
        } else if (cutButton.wasReleased()) {
            if (longCut) {
                if (cutState == cut) {
                    Serial << F("Fade Mode\n");
                    cutState = fade;
                } else {
                    Serial << F("Cut Mode\n");
                    cutState = cut;
                }
                longCut = false;
            } else {
                Serial << F("Cut\n");
                doCut = true;
            }
        } else if (cutButton.pressedFor(LONG_PRESS)) {
            Serial << F("Long Press\n");
            longCut = true;
        }
    }
}

uint16_t updateFromMics() {
    uint8_t index = 0x0F & ~micState;
    return micToVideoSource[index];
}

void configureATEM() {
    // Set default output
    AtemSwitcher.changeProgramInput(defaultVideoSource);
    // Set cut to fast mix
    AtemSwitcher.changeTransitionType(0);
    AtemSwitcher.changeTransitionMixTime(10);
}

void updateATEM() {
    if (modeState == automatic) {
        if (millis() - lastAutoChange > 2000 && (videoSourceState != lastVideoSourceState)) {
            Serial << (0x0F & ~micState) << endl;
            Serial << F("Auto change to ") << videoSourceState << endl;
            AtemSwitcher.changePreviewInput(videoSourceState);
            AtemSwitcher.doCut();
            lastAutoChange = millis();
        }
    } else { // modeState == manual
        if (videoSourceState != lastVideoSourceState) {
            AtemSwitcher.changePreviewInput(videoSourceState);
        }
        if (doCut) {
            if (cutState == fade) {
                AtemSwitcher.doAuto();
            } else {
                AtemSwitcher.doCut();
            }
            doCut = false;
        }
    }
    lastVideoSourceState = videoSourceState;
}

void updateFromATEM() {
    // Update mode LED
    if (modeState == automatic) {
        digitalWrite(modeButtonLED, HIGH);
    } else {
        digitalWrite(modeButtonLED, LOW);
    }

    // Update Preview LEDs
    for(int i =0; i < 6; i++) {
        digitalWrite(videoSourceLED[i], AtemSwitcher.getPreviewTally(i+1) ? HIGH : LOW);
    }
}
