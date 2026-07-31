#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define TEST_PIN_COUNT 3

const uint8_t testAnalogPins[TEST_PIN_COUNT] = {A0, A1, A2};
const uint8_t test680Pins[TEST_PIN_COUNT] = {12, 10, 8};
const uint8_t test470kPins[TEST_PIN_COUNT] = {13, 11, 9};
const char testPinNames[TEST_PIN_COUNT] = {'1', '2', '3'};

#define BUTTON A3
#define BUZZER 2

struct JunctionResult
{
  int highAdc;
  int lowAdc;
  bool conducts;
  float vf;
};

struct PairResult
{
  uint8_t pinA;
  uint8_t pinB;
  int highAdc;
  int lowAdc;
  int score;
};

struct CapacitorResult
{
  bool detected;
  uint8_t pinA;
  uint8_t pinB;
  float capacitance;
  unsigned long score;
};

struct ChargeProfile
{
  int startAdc;
  int sample1;
  int sample2;
  int sample3;
};

const int CAP_START_MAX_ADC = 40;
const float CAP_SMALL_R = 470000.0;
const float CAP_LARGE_R = 680.0;
const float CAP_SMALL_CAL = 1.56;
const float CAP_LARGE_CAL = 1.00;

void resetAll()
{
  for (uint8_t i = 0; i < TEST_PIN_COUNT; i++)
  {
    pinMode(test680Pins[i], INPUT);
    pinMode(test470kPins[i], INPUT);
  }
}
v
void showHomeScreen()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INSERT COMPONENT");
  lcd.setCursor(0, 1);
  lcd.print("PRESS BUTTON...");
}

JunctionResult drivePair680(uint8_t highIdx, uint8_t lowIdx)
{
  JunctionResult result;

  resetAll();

  pinMode(test680Pins[highIdx], OUTPUT);
  digitalWrite(test680Pins[highIdx], HIGH);
  pinMode(test680Pins[lowIdx], OUTPUT);
  digitalWrite(test680Pins[lowIdx], LOW);
  delay(25);

  result.highAdc = analogRead(testAnalogPins[highIdx]);
  result.lowAdc = analogRead(testAnalogPins[lowIdx]);

  int delta = result.highAdc - result.lowAdc;
  result.conducts = (result.highAdc > 250 && result.lowAdc < 220 && delta > 120);
  result.vf = (delta * 5.0) / 1023.0;

  resetAll();
  return result;
}

PairResult findBestConnectedPair()
{
  PairResult best;
  best.pinA = 0;
  best.pinB = 1;
  best.highAdc = 1023;
  best.lowAdc = 0;
  best.score = -1;

  for (uint8_t i = 0; i < TEST_PIN_COUNT; i++)
  {
    for (uint8_t j = i + 1; j < TEST_PIN_COUNT; j++)
    {
      JunctionResult result = drivePair680(i, j);
      int score = (1023 - result.highAdc) + result.lowAdc;

      if (score > best.score)
      {
        best.pinA = i;
        best.pinB = j;
        best.highAdc = result.highAdc;
        best.lowAdc = result.lowAdc;
        best.score = score;
      }
    }
  }

  return best;
}

void dischargePair(uint8_t pinA, uint8_t pinB)
{
  resetAll();
  pinMode(test680Pins[pinA], OUTPUT);
  digitalWrite(test680Pins[pinA], LOW);
  pinMode(test680Pins[pinB], OUTPUT);
  digitalWrite(test680Pins[pinB], LOW);
  delay(300);
  resetAll();
}

int readStableAnalog(uint8_t analogPin, uint8_t samples = 6)
{
  analogRead(analogPin);
  delayMicroseconds(120);

  long total = 0;
  for (uint8_t i = 0; i < samples; i++)
  {
    total += analogRead(analogPin);
    delayMicroseconds(150);
  }

  return (int)(total / samples);
}

float estimateCapacitanceFromPoint(unsigned long timeMs, int adc, float resistorOhm, float calibration)
{
  float fraction = adc / 1023.0;
  if (fraction <= 0.05 || fraction >= 0.95)
  {
    return 0.0;
  }

  float denom = -log(1.0 - fraction);
  if (denom <= 0.0)
  {
    return 0.0;
  }

  return ((((float)timeMs) / 1000.0) / (resistorOhm * denom)) * calibration;
}

CapacitorResult measureCapacitorOnPair(uint8_t chargePin, uint8_t groundPin)
{
  CapacitorResult result;
  result.detected = false;
  result.pinA = chargePin;
  result.pinB = groundPin;
  result.capacitance = 0.0;
  result.score = 0;

  dischargePair(chargePin, groundPin);
  delay(5);

  ChargeProfile smallProfile;
  ChargeProfile largeProfile;

  smallProfile.startAdc = readStableAnalog(testAnalogPins[chargePin], 8);
  if (smallProfile.startAdc > CAP_START_MAX_ADC)
  {
    return result;
  }

  pinMode(test470kPins[chargePin], OUTPUT);
  digitalWrite(test470kPins[chargePin], HIGH);
  pinMode(test680Pins[groundPin], OUTPUT);
  digitalWrite(test680Pins[groundPin], LOW);
  delay(5);
  smallProfile.sample1 = readStableAnalog(testAnalogPins[chargePin]);
  delay(45);
  smallProfile.sample2 = readStableAnalog(testAnalogPins[chargePin]);
  delay(150);
  smallProfile.sample3 = readStableAnalog(testAnalogPins[chargePin]);
  resetAll();

  dischargePair(chargePin, groundPin);
  delay(5);

  largeProfile.startAdc = readStableAnalog(testAnalogPins[chargePin], 8);
  if (largeProfile.startAdc > CAP_START_MAX_ADC)
  {
    return result;
  }

  pinMode(test680Pins[chargePin], OUTPUT);
  digitalWrite(test680Pins[chargePin], HIGH);
  pinMode(test680Pins[groundPin], OUTPUT);
  digitalWrite(test680Pins[groundPin], LOW);
  delay(2);
  largeProfile.sample1 = readStableAnalog(testAnalogPins[chargePin]);
  delay(10);
  largeProfile.sample2 = readStableAnalog(testAnalogPins[chargePin]);
  delay(108);
  largeProfile.sample3 = readStableAnalog(testAnalogPins[chargePin]);
  resetAll();

  bool looksSmallCap = (smallProfile.sample1 > smallProfile.startAdc + 20) &&
                       (smallProfile.sample2 > smallProfile.sample1 + 80) &&
                       (smallProfile.sample3 > smallProfile.sample2 + 80);

  bool looksLargeCap = (largeProfile.sample1 > largeProfile.startAdc + 20) &&
                       (largeProfile.sample2 > largeProfile.sample1 + 80) &&
                       (largeProfile.sample2 < 980) &&
                       ((largeProfile.sample3 > largeProfile.sample2 + 10) || (largeProfile.sample3 > 1000));

  float bestEstimate = 0.0;
  unsigned long bestScore = 0;

  if (looksSmallCap)
  {
    bestEstimate = estimateCapacitanceFromPoint(50, smallProfile.sample2, CAP_SMALL_R, CAP_SMALL_CAL);
    if (bestEstimate == 0.0)
    {
      bestEstimate = estimateCapacitanceFromPoint(5, smallProfile.sample1, CAP_SMALL_R, CAP_SMALL_CAL);
    }
    if (bestEstimate == 0.0)
    {
      bestEstimate = estimateCapacitanceFromPoint(200, smallProfile.sample3, CAP_SMALL_R, CAP_SMALL_CAL);
    }
    bestScore = smallProfile.sample3 - smallProfile.sample1;
  }

  if (looksLargeCap)
  {
    float largeEstimate = estimateCapacitanceFromPoint(12, largeProfile.sample2, CAP_LARGE_R, CAP_LARGE_CAL);
    if (largeEstimate == 0.0)
    {
      largeEstimate = estimateCapacitanceFromPoint(2, largeProfile.sample1, CAP_LARGE_R, CAP_LARGE_CAL);
    }
    if (largeEstimate == 0.0)
    {
      largeEstimate = estimateCapacitanceFromPoint(120, largeProfile.sample3, CAP_LARGE_R, CAP_LARGE_CAL);
    }

    unsigned long largeScore = largeProfile.sample3 - largeProfile.sample1;
    if (largeEstimate > 0.0 && largeScore > bestScore)
    {
      bestEstimate = largeEstimate;
      bestScore = largeScore;
    }
  }

  result.detected = (bestEstimate >= 1e-8 && bestEstimate <= 0.02);
  result.capacitance = bestEstimate;
  result.score = bestScore;
  return result;
}

bool detectCapacitor()
{
  CapacitorResult best;
  best.detected = false;
  best.capacitance = 0.0;
  best.score = 0;
  best.pinA = 0;
  best.pinB = 1;

  for (uint8_t i = 0; i < TEST_PIN_COUNT; i++)
  {
    for (uint8_t j = i + 1; j < TEST_PIN_COUNT; j++)
    {
      CapacitorResult forward = measureCapacitorOnPair(i, j);
      CapacitorResult reverse = measureCapacitorOnPair(j, i);
      CapacitorResult candidate = forward.score >= reverse.score ? forward : reverse;

      if (candidate.detected && candidate.score > best.score)
      {
        best = candidate;
        best.pinA = i;
        best.pinB = j;
      }
    }
  }

  if (!best.detected)
  {
    return false;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("CAPACITOR GOOD");
  lcd.setCursor(0, 1);

  if (best.capacitance >= 0.000001)
  {
    lcd.print(best.capacitance * 1000000.0, 2);
    lcd.print(" uF");
  }
  else
  {
    lcd.print(best.capacitance * 1000000000.0, 0);
    lcd.print(" nF");
  }

  lcd.print(" P");
  lcd.print(testPinNames[best.pinA]);
  lcd.print(testPinNames[best.pinB]);

  delay(3000);
  return true;
}

bool detectDiodeOrLED(uint8_t pinA, uint8_t pinB)
{
  JunctionResult forwardAB = drivePair680(pinA, pinB);
  JunctionResult reverseAB = drivePair680(pinB, pinA);

  int f1 = forwardAB.highAdc;
  int f2 = forwardAB.lowAdc;
  int forwardValue = max(f1, f2);
  float Vf = (forwardValue * 5.0) / 1023.0;

  int r1 = reverseAB.highAdc;
  int r2 = reverseAB.lowAdc;
  bool reverseBlock = (r1 < 20 && r2 > 1000) || (r2 < 20 && r1 > 1000);

  if (forwardValue > 100 && reverseBlock)
  {
    uint8_t anodePin = pinA;
    uint8_t cathodePin = pinB;

    lcd.clear();

    if (forwardValue > 650)
    {
      lcd.setCursor(0, 0);
      lcd.print("LED DETECTED");

      lcd.setCursor(0, 1);
      lcd.print("+");
      lcd.print(testPinNames[anodePin]);
      lcd.print(" -");
      lcd.print(testPinNames[cathodePin]);
    }
    else
    {
      lcd.setCursor(0, 0);
      lcd.print("DIODE DETECTED");

      lcd.setCursor(0, 1);
      lcd.print("A");
      lcd.print(testPinNames[anodePin]);
      lcd.print(" K");
      lcd.print(testPinNames[cathodePin]);
    }

    delay(2500);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("FORWARD DROP");
    lcd.setCursor(0, 1);
    lcd.print("Vf=");
    lcd.print(Vf, 2);
    lcd.print("V");

    delay(2000);
    return true;
  }

  return false;
}

int transistorConductionScore(bool isNPN, uint8_t basePin, uint8_t collectorPin, uint8_t emitterPin, bool driveBase)
{
  resetAll();

  if (isNPN)
  {
    pinMode(test680Pins[collectorPin], OUTPUT);
    digitalWrite(test680Pins[collectorPin], HIGH);
    pinMode(test680Pins[emitterPin], OUTPUT);
    digitalWrite(test680Pins[emitterPin], LOW);
    if (driveBase)
    {
      pinMode(test470kPins[basePin], OUTPUT);
      digitalWrite(test470kPins[basePin], HIGH);
    }
  }
  else
  {
    pinMode(test680Pins[emitterPin], OUTPUT);
    digitalWrite(test680Pins[emitterPin], HIGH);
    pinMode(test680Pins[collectorPin], OUTPUT);
    digitalWrite(test680Pins[collectorPin], LOW);
    if (driveBase)
    {
      pinMode(test470kPins[basePin], OUTPUT);
      digitalWrite(test470kPins[basePin], LOW);
    }
  }

  delay(35);

  int collectorAdc = analogRead(testAnalogPins[collectorPin]);
  int emitterAdc = analogRead(testAnalogPins[emitterPin]);

  resetAll();
  return (1023 - collectorAdc) + emitterAdc;
}

int transistorBiasScore(bool isNPN, uint8_t basePin, uint8_t collectorPin, uint8_t emitterPin)
{
  int offScore = transistorConductionScore(isNPN, basePin, collectorPin, emitterPin, false);
  int onScore = transistorConductionScore(isNPN, basePin, collectorPin, emitterPin, true);
  return onScore - offScore;
}

bool detectTransistor()
{
  bool junction[TEST_PIN_COUNT][TEST_PIN_COUNT];
  float junctionVf[TEST_PIN_COUNT][TEST_PIN_COUNT];

  for (uint8_t i = 0; i < TEST_PIN_COUNT; i++)
  {
    for (uint8_t j = 0; j < TEST_PIN_COUNT; j++)
    {
      junction[i][j] = false;
      junctionVf[i][j] = 0.0;

      if (i != j)
      {
        JunctionResult result = drivePair680(i, j);
        junction[i][j] = result.conducts;
        junctionVf[i][j] = result.vf;
      }
    }
  }

  for (uint8_t base = 0; base < TEST_PIN_COUNT; base++)
  {
    uint8_t other1 = (base + 1) % TEST_PIN_COUNT;
    uint8_t other2 = (base + 2) % TEST_PIN_COUNT;

    bool npnLike = junction[base][other1] && junction[base][other2] &&
                   !junction[other1][base] && !junction[other2][base];

    bool pnpLike = junction[other1][base] && junction[other2][base] &&
                   !junction[base][other1] && !junction[base][other2];

    if (!npnLike && !pnpLike)
    {
      continue;
    }

    bool isNPN = npnLike;
    int score1 = transistorBiasScore(isNPN, base, other1, other2);
    int score2 = transistorBiasScore(isNPN, base, other2, other1);

    uint8_t collector = (score1 >= score2) ? other1 : other2;
    uint8_t emitter = (score1 >= score2) ? other2 : other1;
    float avgVf = isNPN ? (junctionVf[base][other1] + junctionVf[base][other2]) / 2.0
                        : (junctionVf[other1][base] + junctionVf[other2][base]) / 2.0;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(isNPN ? "PNP TRANSISTOR" : "NPN TRANSISTOR");
    lcd.setCursor(0, 1);
    lcd.print("B");
    lcd.print(testPinNames[base]);
    lcd.print(" C");
    lcd.print(testPinNames[collector]);
    lcd.print(" E");
    lcd.print(testPinNames[emitter]);

    delay(2200);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(isNPN ? "PNP GOOD" : "NPN GOOD");
    lcd.setCursor(0, 1);
    lcd.print("Vbe=");
    lcd.print(avgVf, 2);
    lcd.print("V");

    delay(2500);
    return true;
  }

  return false;
}

void showShortCircuit()
{
  lcd.clear();
  lcd.print("SHORT CIRCUIT");
  tone(BUZZER, 2000);
  delay(1500);
  noTone(BUZZER);
}

void showFault()
{
  lcd.clear();
  lcd.print("FAULT COMPONENT");
  tone(BUZZER, 1500);
  delay(2000);
  noTone(BUZZER);
}

void showFaulty()
{
  lcd.clear();
  lcd.print("FAULTY");
  tone(BUZZER, 2000);
  delay(2000);
  noTone(BUZZER);
}

void showResistorValue(float resistance, uint8_t pinA, uint8_t pinB)
{
  lcd.clear();
  lcd.setCursor(0, 0);

  if (resistance >= 1000000.0)
  {
    lcd.print(resistance / 1000000.0, 2);
    lcd.print(" MOhm");
  }
  else if (resistance >= 1000.0)
  {
    lcd.print(resistance / 1000.0, 2);
    lcd.print(" kOhm");
  }
  else
  {
    lcd.print((int)resistance);
    lcd.print(" Ohm");
  }

  lcd.setCursor(0, 1);
  lcd.print("P");
  lcd.print(testPinNames[pinA]);
  lcd.print("-");
  lcd.print("P");
  lcd.print(testPinNames[pinB]);
  lcd.print(" RES GOOD");

  delay(2500);
}

void setup()
{
  lcd.init();
  lcd.backlight();

  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);

  lcd.setCursor(0, 0);
  lcd.print("AUTOMATED COMP");
  lcd.setCursor(0, 1);
  lcd.print("ID & FAULT DET");
  delay(3000);

  showHomeScreen();
  delay(2000);
  lcd.clear();
}

void loop()
{
  if (digitalRead(BUTTON) == LOW)
  {
    lcd.clear();
    lcd.print("Testing...");
    delay(500);

    if (detectTransistor())
    {
    }
    else if (detectCapacitor())
    {
    }
    else
    {
      PairResult bestPair = findBestConnectedPair();
      int vHigh = bestPair.highAdc;
      int vLow = bestPair.lowAdc;

      if (bestPair.score < 25)
      {
        showFault();
      }
      else if (abs(vHigh - vLow) < 10)
      {
        showShortCircuit();
      }
      else if ((vLow < 5 && vHigh > 1015))
      {
        showFault();
      }
      else if (detectDiodeOrLED(bestPair.pinA, bestPair.pinB))
      {
      }
      else
      {
        float lowValue = (float)min(vHigh, vLow);
        float highValue = (float)max(vHigh, vLow);

        if (lowValue < 1.0 || highValue <= lowValue)
        {
          showFaulty();
        }
        else
        {
          float resistance;
          float ratio = highValue / lowValue;

          if (ratio < 5.0)
          {
            resistance = 680.0 * ((highValue - lowValue) / lowValue);
          }
          else
          {
            resetAll();
            pinMode(test470kPins[bestPair.pinA], OUTPUT);
            digitalWrite(test470kPins[bestPair.pinA], HIGH);
            pinMode(test470kPins[bestPair.pinB], OUTPUT);
            digitalWrite(test470kPins[bestPair.pinB], LOW);
            delay(50);

            int hv1 = analogRead(testAnalogPins[bestPair.pinA]);
            int hv2 = analogRead(testAnalogPins[bestPair.pinB]);
            delay(120);
            int hv1Later = analogRead(testAnalogPins[bestPair.pinA]);
            int hv2Later = analogRead(testAnalogPins[bestPair.pinB]);
            resetAll();

            float high470 = (float)max(hv1, hv2);
            float low470 = (float)min(hv1, hv2);
            float high470Later = (float)max(hv1Later, hv2Later);
            float low470Later = (float)min(hv1Later, hv2Later);

            if (abs((int)(high470Later - high470)) > 15 || abs((int)(low470Later - low470)) > 15)
            {
              if (detectCapacitor())
              {
                while (digitalRead(BUTTON) == LOW)
                {
                }
                showHomeScreen();
                return;
              }
            }

            if (low470 < 1.0 || high470 <= low470)
            {
              showFaulty();
              while (digitalRead(BUTTON) == LOW)
              {
              }
              showHomeScreen();
              return;
            }

            resistance = 470000.0 * ((high470 - low470) / low470);
          }

          showResistorValue(resistance, bestPair.pinA, bestPair.pinB);
        }
      }
    }

    resetAll();
    while (digitalRead(BUTTON) == LOW)
    {
    }
    showHomeScreen();
  }
}
