#include <Arduino.h>
#include <CAN.h>

// --- PIN CONFIGURATION ---
#define TX_PIN 21
#define RX_PIN 22
#define PIN_TRIG 18
#define PIN_ECHO 19
#define PIN_STOPURI 13  // Brake Lights
#define PIN_BUZZER 27   // Passive Buzzer

// --- PITCH DEFINITIONS (Frequency in Hz) ---
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988

// ==========================================
// 1. ROCK: SWEET CHILD O' MINE (Guns N' Roses)
// ==========================================
int song1_notes[] = {
  NOTE_CS4, NOTE_CS5, NOTE_GS4, NOTE_FS4, NOTE_FS4, NOTE_GS4, NOTE_F5, NOTE_GS4,
  NOTE_CS4, NOTE_CS5, NOTE_GS4, NOTE_FS4, NOTE_FS4, NOTE_GS4, NOTE_F5, NOTE_GS4,
  NOTE_B3,  NOTE_CS5, NOTE_GS4, NOTE_FS4, NOTE_FS4, NOTE_GS4, NOTE_F5, NOTE_GS4
};
int song1_durations[] = {
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8
};
int song1_len = 24;

// ==========================================
// 2. SYNTH POP: TAKE ON ME (A-ha)
// ==========================================
int song2_notes[] = {
  NOTE_FS5, NOTE_FS5, NOTE_D5, NOTE_B4, 0, NOTE_B4, 0, NOTE_E5, 
  NOTE_E5, NOTE_E5, NOTE_GS5, NOTE_GS5, NOTE_A5, NOTE_B5, NOTE_A5, NOTE_A5,
  NOTE_A5, NOTE_E5, NOTE_D5, NOTE_FS5, NOTE_FS5, NOTE_FS5, NOTE_E5, NOTE_E5,
  NOTE_FS5, NOTE_E5
};
int song2_durations[] = {
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8
};
int song2_len = 26;

// ==========================================
// 3. JAZZ/GROOVE: PINK PANTHER THEME
// ==========================================
int song3_notes[] = {
  NOTE_DS4, NOTE_E4, 0, NOTE_FS4, NOTE_G4, 0, NOTE_DS4, NOTE_E4, NOTE_FS4, NOTE_G4,
  NOTE_C5, NOTE_B4, NOTE_E4, NOTE_G4, NOTE_B4,   
  NOTE_AS4, NOTE_A4, NOTE_G4, NOTE_E4, NOTE_D4, 
  NOTE_E4
};
int song3_durations[] = {
  8, 4, 8, 8, 4, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 2,
  8, 8, 8, 8, 8,
  1
};
int song3_len = 21;

// ==========================================
// 4. TRAP/RAGE: OPIUM STYLE (Ken Carson Vibe)
// ==========================================
int song4_notes[] = {
  NOTE_C3, NOTE_C3, NOTE_DS3, NOTE_C3, 
  NOTE_G3, NOTE_FS3, NOTE_F3, NOTE_DS3,
  NOTE_C3, NOTE_C3, NOTE_DS3, NOTE_C3,
  NOTE_AS3, NOTE_A3, NOTE_GS3, NOTE_G3
};
int song4_durations[] = {
  8, 8, 8, 8,
  16, 16, 16, 16, // Fast triplet roll
  8, 8, 8, 8,
  8, 8, 8, 8
};
int song4_len = 16;


// --- AUDIO ENGINE STATE ---
bool isPlaying = false;
bool isPaused = false;
int noteIndex = 0;
unsigned long nextNoteTime = 0; 
int* currentMelody = nullptr;
int* currentDurations = nullptr;
int currentLength = 0;
float tempoMultiplier = 1.0; 

void setup() {
  Serial.begin(115200);
  pinMode(PIN_STOPURI, OUTPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Initialize CAN Bus at 500kbps
  CAN.setPins(RX_PIN, TX_PIN);
  if (!CAN.begin(500E3)) { 
    Serial.println("CAN Init Failed!");
    while (1); 
  }
  Serial.println("Rear Node: Online.");
}

/**
 * Initializes melody playback based on song ID
 */
void playSong(int songID) {
  noTone(PIN_BUZZER);
  isPlaying = true;
  isPaused = false; 
  noteIndex = 0;
  nextNoteTime = millis(); 
  
  switch(songID) {
    case 0: // Sweet Child O Mine
      currentMelody = song1_notes; currentDurations = song1_durations; currentLength = song1_len;
      tempoMultiplier = 1.0; 
      break;
    case 1: // Take On Me
      currentMelody = song2_notes; currentDurations = song2_durations; currentLength = song2_len;
      tempoMultiplier = 0.9; // Slightly faster
      break;
    case 2: // Pink Panther
      currentMelody = song3_notes; currentDurations = song3_durations; currentLength = song3_len;
      tempoMultiplier = 1.2; // Slower for groove effect
      break;
    case 3: // Ken Carson / Trap
      currentMelody = song4_notes; currentDurations = song4_durations; currentLength = song4_len;
      tempoMultiplier = 0.8; // Very fast
      break;
  }
}

/**
 * Toggles the playback state between Pause and Play
 */
void togglePause() {
  if (!isPlaying) return;
  isPaused = !isPaused;
  if (isPaused) { 
    noTone(PIN_BUZZER); 
  } else { 
    nextNoteTime = millis(); // Resume immediately
  }
}

/**
 * Non-blocking Audio Engine to handle melody timing
 */
void handleAudio() {
  if (!isPlaying || isPaused) return;

  if (millis() >= nextNoteTime) {
    if (noteIndex < currentLength) {
      int note = currentMelody[noteIndex];
      int durationCode = currentDurations[noteIndex];

      // Handle rests (note = 0)
      if (note == 0) {
        int pause = 250 * (4.0 / durationCode) * tempoMultiplier;
        noTone(PIN_BUZZER);
        nextNoteTime = millis() + pause;
      } 
      else {
        // Calculate note duration based on musical notation (4 = quarter, 8 = eighth, etc.)
        int noteDuration = 250 * (4.0 / durationCode) * tempoMultiplier;
        tone(PIN_BUZZER, note, noteDuration);
        
        // Add a slight gap between notes for articulation
        int pauseBetween = noteDuration * 1.05; 
        nextNoteTime = millis() + pauseBetween;
      }

      noteIndex++;
    } else {
      noteIndex = 0; // Infinite loop the current song
    }
  }
}

void loop() {
  handleAudio(); // Process audio tasks

  // --- CAN BUS COMMAND HANDLING ---
  int packetSize = CAN.parsePacket();
  if (packetSize) {
    if (CAN.packetId() == 0x01) { // Master Controller ID
      int comanda = CAN.read();
      
      // Brake Light Commands
      if (comanda == 40) digitalWrite(PIN_STOPURI, HIGH); 
      if (comanda == 41) digitalWrite(PIN_STOPURI, LOW);  
      
      // Global Stop (Emergency/Reset)
      if (comanda == 99) { 
        isPlaying = false; 
        isPaused = false; 
        noTone(PIN_BUZZER); 
      } 

      // Music Control Commands 60-63 (Play Song)
      if (comanda >= 60 && comanda <= 63) {
        playSong(comanda - 60); 
      }

      // Music Control Command 66 (Pause/Resume)
      if (comanda == 66) {
        togglePause(); 
      }
    }
  }

  // --- ULTRASONIC SENSOR & TELEMETRY UPLINK ---
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 100) { 
    // Trigger HC-SR04 pulse
    digitalWrite(PIN_TRIG, LOW); delayMicroseconds(2);
    digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);
    
    // Calculate distance with 30ms timeout
    long duration = pulseIn(PIN_ECHO, HIGH, 30000); 
    int dist = duration * 0.034 / 2;
    if (dist <= 0 || dist > 255) dist = 255; 

    // Send distance data to Master via CAN ID 0x03
    CAN.beginPacket(0x03); 
    CAN.write(dist);       
    CAN.write(0); // Reserved byte         
    CAN.endPacket();
    lastTime = millis();
  }
}
