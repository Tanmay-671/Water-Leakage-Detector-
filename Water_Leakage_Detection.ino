#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "YOUR_BLYNK_TEMPLATE_NAME"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ==========================================================
// WIFI
// ==========================================================

char ssid[] = "YOUR_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// ==========================================================
// TELEGRAM
// ==========================================================

#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
#define CHAT_ID "YOUR_TELEGRAM_CHAT_ID"

// ==========================================================
// LCD
// ==========================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================================================
// PINS
// ==========================================================

#define FLOW1_PIN 32
#define FLOW2_PIN 33

#define PUMP_RELAY 25
#define VALVE_RELAY 26

#define BUZZER_PIN 27

#define WIFI_LED 2

// ==========================================================
// RELAY LOGIC
// ==========================================================

#define RELAY_ON LOW
#define RELAY_OFF HIGH

// ==========================================================
// FLOW SENSOR
// ==========================================================

const float PULSES_PER_LITRE = 450.0;

// ==========================================================
// LEAKAGE SETTINGS
// ==========================================================

// Leakage difference = 0.08 L/min

const float LEAK_DIFFERENCE = 0.08;

// Difference must continue for 4 seconds

const unsigned long LEAK_CONFIRM_TIME = 4000;

// Ignore first 5 seconds after pump starts

const unsigned long STARTUP_TIME = 5000;

// Pump automatically turns OFF 4 seconds after
// leakage is confirmed

const unsigned long AUTO_OFF_TIME = 4000;

// ==========================================================
// VARIABLES
// ==========================================================

volatile unsigned long pulse1 = 0;
volatile unsigned long pulse2 = 0;

float flow1 = 0.0;
float flow2 = 0.0;
float flowDifference = 0.0;

bool pumpRunning = false;
bool valveRunning = false;

bool leakageDetected = false;
bool leakageChecking = false;

bool autoShutdownTimer = false;

unsigned long pumpStartTime = 0;
unsigned long leakageStartTime = 0;
unsigned long leakageConfirmedTime = 0;

unsigned long lastFlowCalculation = 0;
unsigned long lastLCDUpdate = 0;

unsigned long lastWiFiAttempt = 0;
unsigned long lastBlynkAttempt = 0;

String lastLine1 = "";
String lastLine2 = "";

// ==========================================================
// FLOW SENSOR INTERRUPTS
// ==========================================================

void IRAM_ATTR flow1Pulse()
{
  pulse1++;
}

void IRAM_ATTR flow2Pulse()
{
  pulse2++;
}

// ==========================================================
// LCD WRITE
// ==========================================================

void writeLCD(String line1, String line2)
{
  lcd.clear();

  delay(20);

  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(line2);

  lastLine1 = line1;
  lastLine2 = line2;
}

// ==========================================================
// LCD READY SCREEN
// ==========================================================

void showReadyScreen()
{
  lcd.clear();

  delay(20);

  lcd.setCursor(0, 0);
  lcd.print("WATER LEAKAGE");

  lcd.setCursor(0, 1);
  lcd.print("SYSTEM READY");

  lastLine1 = "WATER LEAKAGE";
  lastLine2 = "SYSTEM READY";
}

// ==========================================================
// LCD FLOW SCREEN
// ==========================================================

void showFlowScreen()
{
  String line1;
  String line2;

  line1 = "S1:";
  line1 += String(flow1, 1);

  line1 += " S2:";
  line1 += String(flow2, 1);

  if (leakageDetected)
  {
    line2 = "SYSTEM STOPPED";
  }
  else if (leakageChecking)
  {
    line2 = "CHECKING LEAK";
  }
  else if (!pumpRunning)
  {
    line2 = "SYSTEM READY";
  }
  else if (millis() - pumpStartTime < STARTUP_TIME)
  {
    line2 = "STABILIZING...";
  }
  else
  {
    line2 = "NORMAL";
  }

  while (line1.length() < 16)
    line1 += " ";

  while (line2.length() < 16)
    line2 += " ";

  if (line1.length() > 16)
    line1 = line1.substring(0, 16);

  if (line2.length() > 16)
    line2 = line2.substring(0, 16);

  if (line1 != lastLine1 || line2 != lastLine2)
  {
    lcd.setCursor(0, 0);
    lcd.print(line1);

    lcd.setCursor(0, 1);
    lcd.print(line2);

    lastLine1 = line1;
    lastLine2 = line2;
  }
}

// ==========================================================
// TELEGRAM
// ==========================================================

void sendTelegramMessage(String message)
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient https;

  https.setTimeout(3000);

  String url = "https://api.telegram.org/bot";
  url += BOT_TOKEN;
  url += "/sendMessage";

  if (!https.begin(client, url))
    return;

  https.addHeader("Content-Type", "application/json");

  message.replace("\\", "\\\\");
  message.replace("\"", "\\\"");
  message.replace("\n", "\\n");

  String json = "{";
  json += "\"chat_id\":\"";
  json += CHAT_ID;
  json += "\",";
  json += "\"text\":\"";
  json += message;
  json += "\"";
  json += "}";

  https.POST(json);

  https.end();
}

// ==========================================================
// TELEGRAM LEAKAGE ALERT
// ==========================================================

void sendLeakageTelegram()
{
  String message;

  message += "WATER LEAKAGE DETECTED!\n\n";

  message += "Flow 1: ";
  message += String(flow1, 2);
  message += " L/min\n";

  message += "Flow 2: ";
  message += String(flow2, 2);
  message += " L/min\n";

  message += "Difference: ";
  message += String(flowDifference, 2);
  message += " L/min\n\n";

  message += "Leakage confirmed for 4 seconds.\n";
  message += "Buzzer activated.\n";
  message += "Pump shutdown started.";

  sendTelegramMessage(message);
}

// ==========================================================
// TELEGRAM SHUTDOWN
// ==========================================================

void sendShutdownTelegram()
{
  String message;

  message += "WATER LEAKAGE SYSTEM STOPPED\n\n";

  message += "Pump: OFF\n";
  message += "Valve: OFF\n";
  message += "Buzzer: ON\n\n";

  message += "System ready for next test.";

  sendTelegramMessage(message);
}

// ==========================================================
// WIFI
// ==========================================================

void startWiFi()
{
  WiFi.mode(WIFI_STA);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  WiFi.begin(ssid, pass);

  lastWiFiAttempt = millis();

  Serial.println(">>> WiFi connection started");
}

// ==========================================================
// WIFI HANDLER
// ==========================================================

void handleWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(WIFI_LED, HIGH);
    return;
  }

  digitalWrite(WIFI_LED, LOW);

  if (millis() - lastWiFiAttempt >= 5000)
  {
    Serial.println(">>> WiFi reconnecting...");

    WiFi.disconnect();
    WiFi.begin(ssid, pass);

    lastWiFiAttempt = millis();
  }
}

// ==========================================================
// BLYNK HANDLER
// ==========================================================

void handleBlynk()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (Blynk.connected())
    return;

  if (millis() - lastBlynkAttempt >= 5000)
  {
    Serial.println(">>> Blynk reconnecting...");

    Blynk.connect(1000);

    lastBlynkAttempt = millis();

    if (Blynk.connected())
    {
      Serial.println(">>> BLYNK CONNECTED");

      Blynk.virtualWrite(V2, pumpRunning ? 1 : 0);
      Blynk.virtualWrite(V3, valveRunning ? 1 : 0);
    }
  }
}

// ==========================================================
// PUMP ON
// ==========================================================

void pumpON()
{
  // Start a completely new cycle

  leakageDetected = false;
  leakageChecking = false;
  autoShutdownTimer = false;

  // Buzzer OFF for new cycle

  digitalWrite(BUZZER_PIN, LOW);

  // Valve OFF

  digitalWrite(VALVE_RELAY, RELAY_OFF);
  valveRunning = false;

  // Reset flow counters

  noInterrupts();

  pulse1 = 0;
  pulse2 = 0;

  interrupts();

  // Pump ON

  digitalWrite(PUMP_RELAY, RELAY_ON);

  pumpRunning = true;

  pumpStartTime = millis();

  Serial.println();
  Serial.println("==============================");
  Serial.println(">>> NEW TEST CYCLE");
  Serial.println(">>> PUMP ON");
  Serial.println(">>> LEAK STATE RESET");
  Serial.println("==============================");

  writeLCD(
    "PUMP ON",
    "STABILIZING..."
  );

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V2, 1);
    Blynk.virtualWrite(V3, 0);
    Blynk.virtualWrite(V4, "STABILIZING");
  }
}

// ==========================================================
// PUMP OFF
// ==========================================================

void pumpOFF()
{
  digitalWrite(PUMP_RELAY, RELAY_OFF);

  pumpRunning = false;

  Serial.println(">>> PUMP OFF");

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V2, 0);
  }
}

// ==========================================================
// VALVE ON
// ==========================================================

void valveON()
{
  digitalWrite(VALVE_RELAY, RELAY_ON);

  valveRunning = true;

  Serial.println(">>> VALVE ON");

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V3, 1);
  }

  // IMPORTANT:
  // Buzzer is NOT turned on here.
}

// ==========================================================
// VALVE OFF
// ==========================================================

void valveOFF()
{
  digitalWrite(VALVE_RELAY, RELAY_OFF);

  valveRunning = false;

  Serial.println(">>> VALVE OFF");

  if (Blynk.connected())
  {
    Blynk.virtualWrite(V3, 0);
  }
}

// ==========================================================
// BLYNK PUMP CONTROL
// ==========================================================

BLYNK_WRITE(V2)
{
  if (param.asInt() == 1)
  {
    pumpON();
  }
  else
  {
    pumpOFF();
  }
}

// ==========================================================
// BLYNK VALVE CONTROL
// ==========================================================

BLYNK_WRITE(V3)
{
  if (param.asInt() == 1)
  {
    valveON();
  }
  else
  {
    valveOFF();
  }
}

// ==========================================================
// SETUP
// ==========================================================

void setup()
{
  Serial.begin(115200);

  delay(300);

  // ========================================================
  // LCD
  // ========================================================

  Wire.begin(21, 19);

  lcd.begin();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("WATER LEAKAGE");

  lcd.setCursor(0, 1);
  lcd.print("SYSTEM STARTING");

  delay(1500);

  // ========================================================
  // WIFI LED
  // ========================================================

  pinMode(WIFI_LED, OUTPUT);
  digitalWrite(WIFI_LED, LOW);

  // ========================================================
  // BUZZER
  // ========================================================

  pinMode(BUZZER_PIN, OUTPUT);

  // Buzzer OFF at startup

  digitalWrite(BUZZER_PIN, LOW);

  // ========================================================
  // FLOW SENSORS
  // ========================================================

  pinMode(FLOW1_PIN, INPUT_PULLUP);
  pinMode(FLOW2_PIN, INPUT_PULLUP);

  attachInterrupt(
    digitalPinToInterrupt(FLOW1_PIN),
    flow1Pulse,
    RISING
  );

  attachInterrupt(
    digitalPinToInterrupt(FLOW2_PIN),
    flow2Pulse,
    RISING
  );

  // ========================================================
  // RELAYS
  // ========================================================

  pinMode(PUMP_RELAY, OUTPUT);
  pinMode(VALVE_RELAY, OUTPUT);

  digitalWrite(PUMP_RELAY, RELAY_OFF);
  digitalWrite(VALVE_RELAY, RELAY_OFF);

  pumpRunning = false;
  valveRunning = false;

  // ========================================================
  // BLYNK
  // ========================================================

  Blynk.config(BLYNK_AUTH_TOKEN);

  // ========================================================
  // WIFI
  // ========================================================

  writeLCD(
    "WATER LEAKAGE",
    "CONNECTING WIFI"
  );

  startWiFi();

  // ========================================================
  // TIMERS
  // ========================================================

  lastFlowCalculation = millis();
  lastLCDUpdate = millis();

  Serial.println();
  Serial.println("==============================");
  Serial.println(" WATER LEAKAGE SYSTEM");
  Serial.println("==============================");

  Serial.println("Leak difference = 0.08 L/min");
  Serial.println("Leak confirmation = 4 seconds");
  Serial.println("Auto shutdown = 4 seconds");
  Serial.println("Buzzer pin = GPIO 27");
  Serial.println();
}

// ==========================================================
// LOOP
// ==========================================================

void loop()
{
  // ========================================================
  // WIFI / BLYNK
  // ========================================================

  handleWiFi();

  handleBlynk();

  if (Blynk.connected())
  {
    Blynk.run();
  }

  // ========================================================
  // FLOW CALCULATION
  // ========================================================

  if (millis() - lastFlowCalculation >= 1000)
  {
    noInterrupts();

    unsigned long p1 = pulse1;
    unsigned long p2 = pulse2;

    pulse1 = 0;
    pulse2 = 0;

    interrupts();

    flow1 =
      (p1 * 60.0) /
      PULSES_PER_LITRE;

    flow2 =
      (p2 * 60.0) /
      PULSES_PER_LITRE;

    flowDifference =
      flow1 - flow2;

    lastFlowCalculation = millis();

    // ======================================================
    // SERIAL
    // ======================================================

    Serial.print("Flow 1: ");
    Serial.print(flow1, 2);

    Serial.print(" | Flow 2: ");
    Serial.print(flow2, 2);

    Serial.print(" | Difference: ");
    Serial.print(flowDifference, 2);

    Serial.print(" | ");

    if (leakageDetected)
      Serial.println("LEAKAGE DETECTED");
    else if (leakageChecking)
      Serial.println("CHECKING LEAK");
    else if (pumpRunning)
      Serial.println("NORMAL");
    else
      Serial.println("READY");

    // ======================================================
    // BLYNK
    // ======================================================

    if (Blynk.connected())
    {
      Blynk.virtualWrite(V0, flow1);
      Blynk.virtualWrite(V1, flow2);
      Blynk.virtualWrite(V5, flowDifference);

      if (leakageDetected)
      {
        Blynk.virtualWrite(
          V4,
          "SYSTEM STOPPED"
        );
      }
      else if (leakageChecking)
      {
        Blynk.virtualWrite(
          V4,
          "CHECKING LEAK"
        );
      }
      else if (pumpRunning)
      {
        if (
          millis() - pumpStartTime <
          STARTUP_TIME
        )
        {
          Blynk.virtualWrite(
            V4,
            "STABILIZING"
          );
        }
        else
        {
          Blynk.virtualWrite(
            V4,
            "NORMAL"
          );
        }
      }
      else
      {
        Blynk.virtualWrite(
          V4,
          "SYSTEM READY"
        );
      }
    }
  }

  // ========================================================
  // LEAKAGE DETECTION
  // ========================================================

  if (
    pumpRunning &&
    !leakageDetected &&
    millis() - pumpStartTime >= STARTUP_TIME
  )
  {
    flowDifference =
      flow1 - flow2;

    // ======================================================
    // THRESHOLD = 0.08 L/MIN
    // ======================================================

    if (
      flowDifference >=
      LEAK_DIFFERENCE
    )
    {
      // ====================================================
      // START LEAK CHECKING
      // ====================================================

      if (!leakageChecking)
      {
        leakageChecking = true;

        leakageStartTime = millis();

        Serial.println();
        Serial.println(
          ">>> DIFFERENCE >= 0.08"
        );

        Serial.println(
          ">>> CHECKING FOR 4 SECONDS"
        );

        if (Blynk.connected())
        {
          Blynk.virtualWrite(
            V4,
            "CHECKING LEAK"
          );
        }
      }

      // ====================================================
      // LEAK CONFIRMED
      // ====================================================

      if (
        millis() - leakageStartTime >=
        LEAK_CONFIRM_TIME
      )
      {
        leakageDetected = true;

        leakageChecking = false;

        autoShutdownTimer = true;

        leakageConfirmedTime = millis();

        Serial.println();
        Serial.println(
          "!!!!!!!!!!!!!!!!!!!!!!!!"
        );

        Serial.println(
          "   LEAKAGE DETECTED"
        );

        Serial.println(
          "!!!!!!!!!!!!!!!!!!!!!!!!"
        );

        // ==================================================
        // BUZZER ON
        //
        // THIS IS THE ONLY PLACE WHERE THE BUZZER
        // IS TURNED ON.
        // ==================================================

        digitalWrite(
          BUZZER_PIN,
          HIGH
        );

        Serial.println(
          ">>> BUZZER ON"
        );

        // ==================================================
        // BLYNK
        // ==================================================

        if (Blynk.connected())
        {
          Blynk.virtualWrite(
            V4,
            "LEAKAGE DETECTED"
          );

          Blynk.logEvent(
            "water_leakage",
            "Water leakage detected!"
          );
        }

        // ==================================================
        // TELEGRAM
        // ==================================================

        sendLeakageTelegram();

        // ==================================================
        // LCD
        // ==================================================

        writeLCD(
          "LEAKAGE DETECTED",
          "BUZZER ON"
        );
      }
    }
    else
    {
      leakageChecking = false;
    }
  }

  // ========================================================
  // AUTOMATIC SHUTDOWN
  // ========================================================

  if (autoShutdownTimer)
  {
    if (
      millis() - leakageConfirmedTime >=
      AUTO_OFF_TIME
    )
    {
      Serial.println();
      Serial.println(
        ">>> AUTOMATIC SAFETY SHUTDOWN"
      );

      // ====================================================
      // VALVE OFF
      // ====================================================

      valveOFF();

      // ====================================================
      // PUMP OFF
      // ====================================================

      pumpOFF();

      autoShutdownTimer = false;

      leakageDetected = true;

      // ====================================================
      // IMPORTANT:
      //
      // BUZZER STAYS ON AFTER LEAKAGE.
      //
      // It will remain ON until a NEW pump cycle
      // is started.
      // ====================================================

      Serial.println(
        ">>> BUZZER REMAINS ON"
      );

      // ====================================================
      // LCD
      // ====================================================

      writeLCD(
        "LEAKAGE DETECTED",
        "SYSTEM STOPPED"
      );

      // ====================================================
      // BLYNK
      // ====================================================

      if (Blynk.connected())
      {
        Blynk.virtualWrite(
          V2,
          0
        );

        Blynk.virtualWrite(
          V3,
          0
        );

        Blynk.virtualWrite(
          V4,
          "SYSTEM STOPPED"
        );
      }

      // ====================================================
      // TELEGRAM
      // ====================================================

      sendShutdownTelegram();

      Serial.println();
      Serial.println(
        ">>> READY FOR NEXT DEMONSTRATION"
      );

      Serial.println(
        ">>> PRESS PUMP ON AGAIN"
      );
    }
  }

  // ========================================================
  // LCD NORMAL UPDATE
  // ========================================================

  if (
    millis() - lastLCDUpdate >= 1000
  )
  {
    lastLCDUpdate = millis();

    if (!autoShutdownTimer)
    {
      if (
        leakageDetected &&
        !pumpRunning
      )
      {
        writeLCD(
          "LEAKAGE DETECTED",
          "SYSTEM STOPPED"
        );
      }
      else
      {
        showFlowScreen();
      }
    }
  }
}
