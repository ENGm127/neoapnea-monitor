# NeoApnea Monitor — engineering audit (development branch)

Date: 2026-11-20

## Scope and safety status

This is a software/firmware engineering audit of the current prototype. It is not a clinical validation, a medical-device risk assessment, or evidence that the system is safe for patient monitoring. Do not connect this prototype to an infant or use its alarms for clinical decisions. Any infant study requires institutional ethics approval, qualified neonatal supervision, and an independently validated monitor as the reference.

## Changes made on this branch

1. **Breath event edge handling** — the detector now records a breath at the rising edge of the threshold crossing. Samples that remain above threshold no longer keep moving `lastBreath` forward. This corrects event bookkeeping only; it does not make a fixed raw-Z threshold physiologically valid.
2. **Serial EOF handling** — `LineSplitter.flush()` emits a final unterminated line, and the Web Serial reader flushes it when the stream ends.
3. **PPG integer width** — firmware frame storage is now `int32_t`, red samples are `uint32_t`, and IR samples are copied without narrowing through a 16-bit AVR `int`.
4. **Regression tests added** — tests cover sustained threshold crossings and final serial lines. They still need to be run in CI or a local Node 20+ environment before this branch is considered verified.

## High-priority issues still open

### 1. SpO2 sample-rate contract is incorrect

The SparkFun `spo2_algorithm.h` defines `FreqS 25` and `BUFFER_SIZE (FreqS * 4)`, i.e. a 100-sample, four-second window at 25 Hz. The current firmware configures the MAX30102 for 200 Hz with sample averaging of two (nominally 100 output samples/s), then passes 100 consecutive samples to that algorithm. It therefore feeds approximately one second of data while the algorithm interprets it as four seconds. The heart-rate estimate internal to that routine is also time-scaled incorrectly. Do not use its current SpO2/HR outputs.

**Required design decision:** choose a deliberate effective 25 Hz acquisition path and preserve a rolling 100-sample (4 s) window with a 25-sample (1 s) update, or replace the algorithm with one whose documented sample-rate/window contract matches the actual stream. Verify the actual MAX30102 FIFO rate and dropped-sample behavior on hardware. Do not merely change the web app's `fsPpg` constant.

### 2. Chest movement is not airflow

The current detector uses one raw accelerometer axis and a fixed threshold. Its output depends on orientation, gravity projection, strap placement, infant movement, vibration, and sensor drift. A movement threshold is not a validated breath detector. Chest motion alone cannot identify obstructive apnea: respiratory effort may continue despite absent airflow. A more defensible research design needs signal conditioning, orientation/gravity handling, breath-cycle segmentation, hysteresis, signal-quality and motion-artifact states, and validation against an independent respiratory reference. Consider adding an airflow/respiratory-impedance channel if the project intends to distinguish central, obstructive, and mixed events.

### 3. Timing and data integrity in firmware

Timer1 generates 10 Hz interrupts, but `volatile bool accelerometerDue` is a one-bit flag. Multiple timer events can collapse into one if the main loop is delayed. Sensor reads and Serial output are coupled in the same loop; serial output can block and delay acquisition. Frames carry two chest samples at a nominal 8 Hz while the timer runs at 10 Hz. Replace the boolean with a counter or timestamped acquisition queue, track overruns, and decouple sampling from serial transmission using bounded buffers. Include sequence numbers/timestamps in a versioned protocol.

### 4. Sensor initialization and status

The sketch currently calls `oximeter.begin()` twice, configures the device even if initialization fails, and starts Timer1 regardless of accelerometer connection status. Implement one-time initialization with explicit sensor state, and do not present a failed/unavailable sensor as a live stream. Add connection, stale-data, and signal-quality states in the app.

### 5. Stale physiological values

The app stores the last valid SpO2 value but does not expire it when updates stop. A disconnected or poor-quality sensor can therefore leave an old value visible. Add acquisition timestamps and freshness timeouts; render stale/missing/invalid distinctly and suppress physiological alarm decisions based on stale values. Apply the same principle to heart rate and chest movement.

### 6. Clocking and gaps in the web engine

The engine derives elapsed time from the number of received chest samples. This assumes uninterrupted, perfectly paced frames. Dropped frames or serial delays distort pause duration and trend timing. Move to source timestamps or a monotonic receive clock with explicit gap detection. Do not silently interpret missing samples as absent respiratory effort.

### 7. Clinical thresholds and event definition

The current 10 s warning, 20 s apnea threshold, HR limits (100/180 bpm), and SpO2 limit (80%) are prototype constants, not validated neonatal alarm settings. The AAP clinical report defines an apnea spell as at least 20 s, or a shorter pause accompanied by bradycardia (<100 bpm), cyanosis, or pallor; classification depends on respiratory effort and airflow. The current code records only pauses over 20 s after movement resumes and does not formally combine shorter pauses with bradycardia/desaturation into an apnea event. Keep clinical definitions separate from configurable engineering test thresholds and obtain neonatal specialist input before proposing alarm logic.

### 8. Data privacy

Patient names, bed identifiers, and episode data are persisted in browser `localStorage` without encryption. Avoid real patient identifiers in this prototype. Before any clinical research, define data minimization, access controls, retention, export handling, and institution-approved storage. Browser-side encryption alone is not a complete clinical data-security architecture.

## Validation plan (required before any claim of readiness)

- Run all Node tests on Node 20+ and add deterministic tests for sensor timeouts, sample gaps, invalid/out-of-range frames, sequence loss, reconnect/disconnect, and sustained/motion-contaminated respiratory signals.
- Compile firmware for Arduino Mega 2560 with the exact documented library versions; inspect SRAM use and confirm types against the selected board's ABI.
- Bench-test the MAX30102 stream with a logic analyzer or timestamped acquisition logs to establish the actual output sample rate, FIFO overflow rate, and timing jitter.
- Compare PPG-derived pulse rate and SpO2 with a suitable calibrated reference/functional tester across controlled pulse rates, saturation levels, motion, and low perfusion. Do not claim neonatal accuracy from a generic MAX30102 breakout.
- Compare respiratory detection with a calibrated respiratory simulator and an independent airflow/respiratory effort reference. Include different orientations, strap tensions, amplitudes, rates, pauses, and motion artifacts.
- For offline algorithm research, use an appropriate neonatal dataset such as the PhysioNet Preterm Infant Cardio-Respiratory Signals Database, while recognizing that retrospective dataset performance is not equivalent to hardware or clinical validation.
- Document software verification, bench validation, and clinical validation as three separate evidence levels.

## Primary references

- SparkFun MAX3010x library, `spo2_algorithm.h`: https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library/blob/master/src/spo2_algorithm.h
- SparkFun MAX3010x library, `Example8_SPO2.ino`: https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library/blob/master/examples/Example8_SPO2/Example8_SPO2.ino
- Eichenwald EC; AAP Committee on Fetus and Newborn. Apnea of Prematurity. *Pediatrics*. 2016;137(1):e20153757. https://doi.org/10.1542/peds.2015-3757
- FDA. Pulse Oximeters — Premarket Notification Submissions (510(k)s): Guidance for Industry and FDA Staff. https://www.fda.gov/regulatory-information/search-fda-guidance-documents/pulse-oximeters-premarket-notification-submissions-510ks-guidance-industry-and-drug
- PhysioNet Preterm Infant Cardio-Respiratory Signals Database: https://physionet.org/content/picsdb/

## Current disposition

**Not for clinical use.** The changes in this branch are limited to software correctness and data integrity. The SpO2 timing contract, acquisition architecture, respiratory sensing validity, timestamping, signal quality/freshness, and verification remain unresolved.
