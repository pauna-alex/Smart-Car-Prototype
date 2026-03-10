#include <Arduino.h>
#include <CAN.h>
#include <IRremote.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// --- PIN CONFIGURATION ---
#define TFT_CS     5
#define TFT_RST    4
#define TFT_DC     25
#define TX_PIN     21
#define RX_PIN     22
#define PIN_IR     14
#define PIN_SERVO  13
#define PIN_LDR    35   // Light Dependent Resistor
#define PIN_TACTIL 34   // Darlington Touch Sensor

// --- IR REMOTE CODES ---
#define IR_BTN_1  0xE916FF00 
#define IR_BTN_2  0xE619FF00 
#define IR_BTN_3  0xF20DFF00 // RADIO MENU
#define IR_BTN_4  0xF30CFF00 // BACK (PREV SONG)
#define IR_BTN_5  0xE718FF00 // NEXT (NEXT SONG)
#define IR_BTN_6  0xA15EFF00 // PAUSE / PLAY
#define IR_OK     0xBF40FF00 
#define IR_UP     0xB946FF00  
#define IR_DOWN   0xEA15FF00  
#define IR_LEFT   0xBB44FF00  
#define IR_RIGHT  0xBC43FF00 

// --- MOTOR SETTINGS ---
int SERVO_STOP = 90;
int SERVO_FATA_MAX = 180;  // Max Forward
int SERVO_SPATE_MAX = 0;    // Max Reverse
#define TIMP_RAMP 50        // Ramping interval (ms)
int PAS_RAMP = 2;           // Speed increment per step

// --- TOUCH SENSOR SETTINGS ---
int pragTactil = 1500;       
bool atingereInCurs = false;

// Custom class to handle specific TFT offsets
class EcranCustom : public Adafruit_ST7735 {
public:
  EcranCustom(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7735(cs, dc, rst) {}
  void regleazaOffset(int8_t col, int8_t row) { setColRowStart(col, row); }
};

EcranCustom tft = EcranCustom(TFT_CS, TFT_DC, TFT_RST);
Servo motorServo;

// --- STATE VARIABLES ---
bool motorPornit = false;     // Engine state
bool eroareActiva = false;    // Safety error state
int vitezaReala = 0;          // Current speed (0-100)
int vitezaTinta = 0;          // Target speed (0-100)
int directie = 0;             // 1=Fwd, -1=Rev, 0=Stop
unsigned long lastRampTime = 0;

int tipEcran = 1;             // 1=Speedo, 2=Info, 3=Radio
unsigned long lastInfoUpdate = 0;
unsigned long lastLDRCheck = 0;

// Sensor Data
int distantaFata = 255; 
int distantaSpate = 255; 
int tempSenzor = 0;
int ultimaVitezaAfisata = -1; 
bool faruriSuntAprinse = false;

// --- RADIO DATA ---
String playlist[] = {"Sweet Child O Mine", "Take On Me", "Pink Panther", "Ken Karson"};
int indexPiesa = 0;

// --- FUNCTION PROTOTYPES ---
void drawCarOFF();
void drawSpeedoBackground();
void drawInfoBackground();
void drawRadioUI();
void updateSpeedometer(int percent);
void updateSensorInfo();
void sendCANCommand(int command);
void stopEverything();
void handleRamping();
void displayError(String msg, int val);
void toggleMotor();



void setup() {
  Serial.begin(115200);

  // Hardware Reset for TFT
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW); delay(100);
  digitalWrite(TFT_RST, HIGH); delay(100);

  tft.initR(INITR_144GREENTAB); 
  delay(200); 
  tft.regleazaOffset(2, 3);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  drawCarOFF(); 

  // Init CAN Bus at 500kbps
  CAN.setPins(RX_PIN, TX_PIN);
  if (!CAN.begin(500E3)) { Serial.println("CAN Error!"); }

  IrReceiver.begin(PIN_IR, ENABLE_LED_FEEDBACK);
  motorServo.attach(PIN_SERVO);
  motorServo.write(SERVO_STOP);
  
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_TACTIL, INPUT);
}

void loop() {
  // --- A. TOUCH SENSOR LOGIC (Darlington) ---
  int citireTactil = analogRead(PIN_TACTIL);
  if (citireTactil > pragTactil) {
    if (!atingereInCurs) {
      atingereInCurs = true;
      toggleMotor(); 
    }
  } else {
    atingereInCurs = false;
  }

  // --- B. IR REMOTE DECODING ---
  if (IrReceiver.decode()) {
    unsigned long cod = IrReceiver.decodedIRData.decodedRawData;

    if (cod != 0) {
      // Clear error state if any button is pressed
      if (eroareActiva) {
        eroareActiva = false;
        motorPornit = true; 
        tipEcran = 1;
        noInterrupts(); drawSpeedoBackground(); interrupts();
      }

      if (cod == IR_BTN_1) {
        toggleMotor();
      }
      
      // LOGIC WHEN ENGINE IS RUNNING
      else if (motorPornit && !eroareActiva) {
        
        // Button 2: Toggle Telemetry View
        if (cod == IR_BTN_2) {
          tipEcran = (tipEcran == 1) ? 2 : 1;
          noInterrupts();
          if (tipEcran == 1) drawSpeedoBackground();
          else drawInfoBackground();
          interrupts();
        }

        // Button 3: Radio Menu
        else if (cod == IR_BTN_3) {
          tipEcran = 3;
          noInterrupts(); drawRadioUI(); interrupts();
        }

        // Button 4: PREV SONG
        else if (cod == IR_BTN_4) {
          indexPiesa--;
          if (indexPiesa < 0) indexPiesa = 3;
          sendCANCommand(60 + indexPiesa);
          if (tipEcran == 3) { noInterrupts(); drawRadioUI(); interrupts(); }
        }

        // Button 5: NEXT SONG
        else if (cod == IR_BTN_5) {
          indexPiesa++;
          if (indexPiesa > 3) indexPiesa = 0;
          sendCANCommand(60 + indexPiesa);
          if (tipEcran == 3) { noInterrupts(); drawRadioUI(); interrupts(); }
        }

        // Button 6: PAUSE / RESUME AUDIO
        else if (cod == IR_BTN_6) {
          sendCANCommand(66);
        }

        // Directional Controls
        else {
          switch (cod) {
            case IR_UP:    if (directie != 1)  { vitezaReala = 0; directie = 1;  } vitezaTinta = 100; sendCANCommand(41); break;
            case IR_DOWN:  if (directie != -1) { vitezaReala = 0; directie = -1; } vitezaTinta = 100; sendCANCommand(41); break;
            case IR_OK:    vitezaTinta = 0; sendCANCommand(40); break; // Braking
            case IR_LEFT:  sendCANCommand(20); break;
            case IR_RIGHT: sendCANCommand(30); break;
          }
        }
      }
    }
    IrReceiver.resume();
  }

  // --- C. MOTOR RAMPING ---
  if (motorPornit && !eroareActiva) {
    handleRamping();
  }

  // --- D. CAN SAFETY MONITORING ---
  int packetSize = CAN.parsePacket();
  if (packetSize) {
    long id = CAN.packetId();
    if (id == 0x02) { distantaFata = CAN.read(); tempSenzor = CAN.read(); }
    else if (id == 0x03) { distantaSpate = CAN.read(); CAN.read(); }

    if (motorPornit && !eroareActiva) {
      bool obstacolFata = (directie == 1 && distantaFata < 15 && distantaFata > 0);
      bool obstacolSpate = (directie == -1 && distantaSpate < 15 && distantaSpate > 0);
      bool supraIncalzire = (tempSenzor > 24); // Overheat threshold

      if (obstacolFata || obstacolSpate || supraIncalzire) {
        eroareActiva = true;
        vitezaTinta = 0; vitezaReala = 0;
        motorServo.write(SERVO_STOP);
        sendCANCommand(99); // Broadcast Emergency Stop
        
        noInterrupts();
        if (supraIncalzire) displayError("TEMP HIGH!", tempSenzor);
        else displayError("COLLISION!", 0);
        interrupts();
      }
    }
  }

  // --- E. SCREEN UPDATES & AUTO-LIGHTS ---
  if (motorPornit && !eroareActiva) {
    // Refresh Telemetry Screen
    if (tipEcran == 2 && (millis() - lastInfoUpdate > 500)) {
      noInterrupts(); updateSensorInfo(); interrupts();
      lastInfoUpdate = millis();
    }

    // LDR Automation (Headlights)
    if (millis() - lastLDRCheck > 300) {
      int darkLevel = map(analogRead(PIN_LDR), 0, 4095, 0, 100);
      if (darkLevel > 75 && !faruriSuntAprinse) { sendCANCommand(10); faruriSuntAprinse = true; } 
      else if (darkLevel < 55 && faruriSuntAprinse) { sendCANCommand(11); faruriSuntAprinse = false; }
      lastLDRCheck = millis();
    }
  }
}

// --- SUPPORT FUNCTIONS ---

void drawRadioUI() {
  tft.fillScreen(ST7735_BLACK);
  tft.drawRect(5, 5, 118, 118, ST7735_BLUE);
  tft.setTextColor(ST7735_CYAN); 
  tft.setTextSize(2);
  tft.setCursor(30, 20); 
  tft.print("RADIO");
  tft.drawFastHLine(10, 50, 108, ST7735_WHITE);
  tft.setTextColor(ST7735_YELLOW); 
  tft.setTextSize(1);
  tft.setCursor(15, 65); 
  tft.print("Now Playing:");
  tft.setTextColor(ST7735_WHITE); 
  tft.setTextSize(1);
  tft.setCursor(15, 85); 
  tft.print(playlist[indexPiesa]);
}

void toggleMotor() {
  if (motorPornit) { 
    motorPornit = false; 
    stopEverything(); 
    noInterrupts(); drawCarOFF(); interrupts(); 
  }
  else { 
    motorPornit = true; 
    eroareActiva = false; 
    tipEcran = 1; 
    sendCANCommand(40); // System Start signal
    noInterrupts(); drawSpeedoBackground(); interrupts(); 
  }
}

void displayError(String msg, int val) {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_RED);
  tft.setTextSize(2);
  tft.setCursor(10, 45);
  tft.print(msg);
  if (val > 0) {
    tft.setCursor(10, 65);
    tft.print(val); tft.print(" C");
  }
}

void handleRamping() {
  if (millis() - lastRampTime > TIMP_RAMP) {
    // Linear interpolation for smooth acceleration/deceleration
    if (vitezaReala < vitezaTinta) vitezaReala += PAS_RAMP;
    else if (vitezaReala > vitezaTinta) vitezaReala -= PAS_RAMP;
    vitezaReala = constrain(vitezaReala, 0, 100);

    if (directie == 0) motorServo.write(SERVO_STOP);
    else if (directie == 1) motorServo.write(map(vitezaReala, 0, 100, SERVO_STOP, SERVO_FATA_MAX));
    else if (directie == -1) motorServo.write(map(vitezaReala, 0, 100, SERVO_STOP, SERVO_SPATE_MAX));

    if (vitezaReala == 0 && vitezaTinta == 0) directie = 0;

    if (tipEcran == 1 && vitezaReala != ultimaVitezaAfisata) {
      noInterrupts(); updateSpeedometer(vitezaReala); interrupts();
      ultimaVitezaAfisata = vitezaReala;
    }
    lastRampTime = millis();
  }
}

void drawCarOFF() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(2); tft.setTextColor(ST7735_RED, ST7735_BLACK);
  tft.setCursor(20, 55); tft.print("ENGINE");
  tft.setCursor(45, 75); tft.print("OFF");
}

void drawSpeedoBackground() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1); tft.setTextColor(ST7735_GREEN, ST7735_BLACK);
  tft.setCursor(35, 10); tft.print("SPEEDOMETER");
  tft.drawRect(10, 90, 108, 20, ST7735_WHITE);
  ultimaVitezaAfisata = -1; 
  updateSpeedometer(vitezaReala);
}

void updateSpeedometer(int percent) {
  tft.fillRect(30, 40, 70, 35, ST7735_BLACK);
  tft.setTextSize(4);
  
  // Color coding based on speed
  if (percent < 50) tft.setTextColor(ST7735_GREEN);
  else if (percent < 80) tft.setTextColor(ST7735_YELLOW);
  else tft.setTextColor(ST7735_RED);
  
  int xPos = (percent < 10) ? 55 : (percent < 100 ? 40 : 30);
  tft.setCursor(xPos, 40);
  tft.print(percent);
  
  // Progress bar update
  int barWidth = map(percent, 0, 100, 0, 104);
  tft.fillRect(12, 92, barWidth, 16, ST7735_CYAN);
  tft.fillRect(12 + barWidth, 92, 104 - barWidth, 16, ST7735_BLACK);
}

void drawInfoBackground() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextSize(1); tft.setTextColor(ST7735_MAGENTA, ST7735_BLACK);
  tft.setCursor(35, 5); tft.print("TELEMETRY");
  tft.drawFastHLine(0, 15, 128, ST7735_WHITE);
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(5, 30); tft.print("Front:");
  tft.setCursor(5, 50); tft.print("Rear:");
  tft.setCursor(5, 70); tft.print("Temp:");
  tft.setCursor(5, 90); tft.print("Light:");
  updateSensorInfo();
}

void updateSensorInfo() {
  int ldrVal = analogRead(PIN_LDR);
  int lightProc = map(ldrVal, 0, 4095, 0, 100);
  tft.setTextSize(1);
  
  // Front Dist Update
  tft.fillRect(50, 30, 70, 10, ST7735_BLACK);
  tft.setTextColor(distantaFata < 20 ? ST7735_RED : ST7735_CYAN);
  tft.setCursor(50, 30); tft.print(distantaFata); tft.print(" cm");

  // Rear Dist Update
  tft.fillRect(50, 50, 70, 10, ST7735_BLACK);
  tft.setTextColor(distantaSpate < 20 ? ST7735_RED : ST7735_CYAN);
  tft.setCursor(50, 50); tft.print(distantaSpate); tft.print(" cm");

  // Temperature Update
  tft.fillRect(50, 70, 70, 10, ST7735_BLACK);
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(50, 70); tft.print(tempSenzor); tft.print(" C");

  // Light Intensity Update
  tft.fillRect(50, 90, 70, 10, ST7735_BLACK);
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(50, 90); tft.print(lightProc); tft.print(" %");
}

void sendCANCommand(int command) {
  CAN.beginPacket(0x01); 
  CAN.write(command); 
  CAN.endPacket();
}

void stopEverything() {
  motorServo.write(SERVO_STOP); 
  vitezaReala = 0; 
  vitezaTinta = 0; 
  directie = 0;
  sendCANCommand(41); // Stop lights/hazards
  sendCANCommand(11); // Headlights OFF
  sendCANCommand(21); // Signal OFF
  sendCANCommand(31); // Signal OFF
}
