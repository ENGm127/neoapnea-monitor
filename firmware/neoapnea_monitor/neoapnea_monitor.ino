/*
 * NeoApnea Monitor firmware: an open-source apnea monitor for preterm infants.
 *
 * Reads chest movement from an accelerometer and a pulse (PPG) signal from a
 * pulse oximeter, and streams both over USB serial to the NeoApnea Monitor
 * app, which plots them and runs the apnea, heart-rate and SpO2 alarms.
 *
 * Board: Arduino Mega 2560 (ATmega2560).
 *
 * Wiring: both sensors share the I2C bus, SDA on pin 20 and SCL on pin 21.
 *   MPU-6050 accelerometer (GY-521 breakout), address 0x68, on the chest.
 *   MAX30102 pulse oximeter, address 0x57, on a finger.
 *
 * Libraries (Arduino Library Manager):
 *   "MPU6050" by Electronic Cats                             -> MPU6050.h
 *   "SparkFun MAX3010x Pulse and Proximity Sensor Library"   -> MAX30105.h, spo2_algorithm.h
 *
 * Sampling:
 *   Accelerometer: only the z axis is used. Timer1 sets a flag 10 times per
 *   second (16 MHz / 256 / 6250) and the loop reads the sensor on each flag.
 *   A frame carries the latest two readings, so the stream holds 8
 *   accelerometer samples per second.
 *   Pulse oximeter: sensor configured at 200 Hz with 2-sample on-chip
 *   averaging, yielding a nominal 100 FIFO samples/s. The browser receives
 *   red samples at 100 Hz. For the SparkFun algorithm, firmware averages
 *   non-overlapping groups of four samples to create a 25 Hz stream, keeps
 *   a rolling 100-sample (4 s) window, and updates the estimate once per second.
 *
 * Serial output at 57600 baud:
 *   Buffer: 1, az0, az1, red0, ..., red24, 1    every 0.25 s, 29 values;
 *                                               the first and last are 1,
 *                                               start and end markers
 *   SPO2= <value>, <valid>                      after every 100 PPG samples;
 *                                               valid is 1 or 0
 * Lines that start with anything else are status messages; the app ignores them.
 *
 * Known limitations:
 *   The SparkFun algorithm's 25 Hz input contract is matched by a four-sample
 *   boxcar average/decimator. The estimate remains an unvalidated research
 *   output: correct sample rate does not establish SpO2 accuracy or neonatal
 *   suitability. PPG samples are stored in 32-bit variables to preserve the
 *   sensor's raw range. Actual FIFO timing and loss still require measurement.
 */

#include "Wire.h"
#include "MPU6050.h"
#include "MAX30105.h"
#include "spo2_algorithm.h"

MPU6050 accelerometer;
MAX30105 oximeter;

const int PPG_PER_FRAME = 25;              // red samples in one frame, 0.25 s at 100 Hz
const int ACC_PER_FRAME = 2;               // accelerometer samples in one frame
const int FRAME_LENGTH = 1 + ACC_PER_FRAME + PPG_PER_FRAME + 1;
const uint8_t SPO2_DECIMATION = 4;         // 100 Hz raw FIFO -> 25 Hz algorithm input
const uint8_t SPO2_UPDATE_SAMPLES = 25;    // update every 1 s after the initial 4 s
const int SPO2_WINDOW = 100;                // 4 s at the algorithm's required 25 Hz

volatile bool accelerometerDue = false;    // set by the Timer1 interrupt
bool sensorsReady = false;

int32_t frame[FRAME_LENGTH];               // signed chest counts and full-width PPG values

int accSamples[ACC_PER_FRAME];
int accCount = 0;
bool accReady = false;

uint32_t redSamples[PPG_PER_FRAME];
int ppgCount = 0;
bool ppgReady = false;

// MAX30102 FIFO output is nominally 100 Hz. Keep browser PPG at this rate,
 // but feed the SparkFun routine a separately averaged 25 Hz stream.
uint32_t spo2RedRing[SPO2_WINDOW];
uint32_t spo2IrRing[SPO2_WINDOW];
uint32_t spo2RedLinear[SPO2_WINDOW];
uint32_t spo2IrLinear[SPO2_WINDOW];
uint32_t redAccumulator = 0;
uint32_t irAccumulator = 0;
uint8_t decimationCount = 0;
uint8_t spo2WriteIndex = 0;       // next ring slot; also oldest sample once full
uint8_t spo2SamplesStored = 0;
uint8_t spo2SamplesSinceUpdate = 0;

int32_t spo2;
int8_t spo2Valid;
int32_t heartRate;       // also computed by the SparkFun algorithm; not sent
int8_t heartRateValid;

void setup() {
  Serial.begin(57600);
  Wire.begin();
  accelerometer.initialize();

  const bool accelerometerOk = accelerometer.testConnection();
  Serial.println(accelerometerOk ? "Accelerometer ready" : "ERROR: accelerometer not found");

  const bool oximeterOk = oximeter.begin();
  Serial.println(oximeterOk ? "Pulse oximeter ready" : "ERROR: pulse oximeter not found");

  // Do not start acquisition or emit plausible-looking data if either sensor
  // failed initialization. The app will receive an explicit error status.
  if (!accelerometerOk || !oximeterOk) {
    Serial.println("ERROR: sensor initialization failed; acquisition stopped");
    return;
  }

  byte ledBrightness = 60;   // 0 = off to 255 = 50 mA
  byte sampleAverage = 2;    // 1, 2, 4, 8, 16 or 32
  byte ledMode = 2;          // 1 = red only, 2 = red + IR, 3 = red + IR + green
  byte sampleRate = 200;     // 50, 100, 200, 400, 800, 1000, 1600 or 3200
  int pulseWidth = 69;       // 69, 118, 215 or 411 us
  int adcRange = 4096;       // 2048, 4096, 8192 or 16384
  oximeter.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  // Start the chest sampling timer only after both sensors are initialized.
  // Timer1 in CTC mode: 16 MHz / (256 * 6250) = 10 Hz.
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 6249;
  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS12);
  TIMSK1 |= (1 << OCIE1A);
  sei();
  sensorsReady = true;

  frame[0] = 1;
  frame[FRAME_LENGTH - 1] = 1;
}

void loop() {
  if (!sensorsReady) return;

  if (accelerometerDue) {
    int16_t ax, ay, az;
    accelerometer.getAcceleration(&ax, &ay, &az);
    accSamples[accCount] = az;
    accCount++;
    if (accCount == ACC_PER_FRAME) {
      for (int i = 0; i < ACC_PER_FRAME; i++) {
        frame[1 + i] = accSamples[i];
      }
      accCount = 0;
      accReady = true;
    }
    accelerometerDue = false;
  }

  oximeter.check();
  while (oximeter.available()) {
    const uint32_t red = oximeter.getFIFORed();
    const uint32_t ir = oximeter.getFIFOIR();
    redSamples[ppgCount] = red;
    ppgCount++;
    oximeter.nextSample();

    // Boxcar low-pass followed by decimation: 4 raw 100 Hz samples become
    // one 25 Hz sample, matching FreqS=25 and BUFFER_SIZE=100 in SparkFun.
    redAccumulator += red;
    irAccumulator += ir;
    decimationCount++;
    if (decimationCount == SPO2_DECIMATION) {
      spo2RedRing[spo2WriteIndex] = redAccumulator / SPO2_DECIMATION;
      spo2IrRing[spo2WriteIndex] = irAccumulator / SPO2_DECIMATION;
      spo2WriteIndex = (spo2WriteIndex + 1) % SPO2_WINDOW;
      bool calculateSpo2 = false;
      if (spo2SamplesStored < SPO2_WINDOW) {
        spo2SamplesStored++;
        calculateSpo2 = (spo2SamplesStored == SPO2_WINDOW); // first complete 4 s window
      } else {
        spo2SamplesSinceUpdate++;
        if (spo2SamplesSinceUpdate >= SPO2_UPDATE_SAMPLES) {
          calculateSpo2 = true; // then update once per second
          spo2SamplesSinceUpdate = 0;
        }
      }
      redAccumulator = 0;
      irAccumulator = 0;
      decimationCount = 0;

      if (calculateSpo2) {
        // Ring write index points to the oldest sample. Linearize the last
        // 4 seconds because the Maxim routine expects chronological samples.
        for (int i = 0; i < SPO2_WINDOW; i++) {
          const uint8_t index = (spo2WriteIndex + i) % SPO2_WINDOW;
          spo2RedLinear[i] = spo2RedRing[index];
          spo2IrLinear[i] = spo2IrRing[index];
        }
        maxim_heart_rate_and_oxygen_saturation(
          spo2IrLinear, SPO2_WINDOW, spo2RedLinear,
          &spo2, &spo2Valid, &heartRate, &heartRateValid
        );
        Serial.print(F("SPO2= "));
        Serial.print(spo2, DEC);
        Serial.print(", ");
        Serial.println(spo2Valid, DEC);
      }
    }

    if (ppgCount == PPG_PER_FRAME) {
      for (int i = 0; i < PPG_PER_FRAME; i++) {
        frame[1 + ACC_PER_FRAME + i] = redSamples[i];
      }
      ppgCount = 0;
      ppgReady = true;
    }

  }

  if (accReady && ppgReady) {
    Serial.print("Buffer: ");
    for (int i = 0; i < FRAME_LENGTH; i++) {
      Serial.print(frame[i]);
      if (i < FRAME_LENGTH - 1) {
        Serial.print(", ");
      }
    }
    Serial.println();
    accReady = false;
    ppgReady = false;
  }
}

ISR(TIMER1_COMPA_vect) {
  accelerometerDue = true;
}
