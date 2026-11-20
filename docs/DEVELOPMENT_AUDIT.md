# NeoApnea Monitor — engineering audit (development branch)

Date: 2026-11-20

## Scope and safety status

This is a software/firmware engineering audit of the current prototype. It is not a clinical validation, a medical-device risk assessment, or evidence that the system is safe for patient monitoring. Do not connect this prototype to an infant or use its alarms for clinical decisions. Any infant study requires institutional ethics approval, qualified neonatal supervision, and an independently validated monitor as the reference.

## Changes made on this branch

1. **Breath event edge handling** — the detector now records a breath at the rising edge of the threshold crossing. Samples that remain above threshold no longer keep moving `lastBreath` forward. This corrects event bookkeeping only; it does not make a fixed raw-Z threshold physiologically valid.
2. **Serial EOF handling** — `LineSplitter.flush()` emits a final unterminated line, and the Web Serial reader flushes it when the stream ends.
3. **PPG integer width** — firmware frame storage is now `int32_t`, red samples are `uint32_t`, and IR samples are copied without narrowing through a 16-bit AVR `int`.
4. **SpO2 algorithm input timing** — firmware now preserves the 100 Hz raw PPG stream for the browser, averages each non-overlapping group of four red/IR samples to 25 Hz for the SparkFun routine, maintains a rolling 100-sample (4 s) window, computes the first estimate after 4 s, and updates every 1 s thereafter. This matches the source constants but is not validation of the oximetry result.
5. **Sensor startup gating** — firmware initializes each sensor once and does not start the acquisition timer if either sensor initialization fails.
6. **Stale/invalid reading handling** — invalid SpO2 results now clear the previously displayed value; a valid SpO2 value expires after 3 s without a new valid update, and the complete frame stream is marked lost after 2 s without a frame. The UI marks missing/stale values as idle rather than retaining a normal-looking old reading. These are conservative prototype timeouts, not clinical alarm thresholds.
7. **Regression tests added** — JavaScript tests cover sustained threshold crossings, final serial lines, invalid SpO2 clearing, and stale-stream expiry. A standalone C++11 test covers decimation warm-up, one-second rolling-window cadence, and ring-buffer chronological ordering; the C++ test was compiled and passed locally with `g++ -std=c++11 -Wall -Wextra -Werror`. The full Node suite and target Arduino firmware build remain unverified.

## High-priority issues still open

### 1. SpO2 timing contract — corrected in software, hardware verification pending

The SparkFun `spo2_algorithm.h` defines `FreqS 25` and `BUFFER_SIZE (FreqS * 4)`: exactly 100 samples representing four seconds at 25 Hz. The MAX30102 is configured for a nominal 100 FIFO samples/s (200 Hz conversion rate with two-sample on-chip averaging). Firmware now keeps the 100 Hz red stream for the browser and forms a separate 25 Hz stream by averaging non-overlapping groups of four red and IR samples. It maintains a rolling 100-sample window, calculates the first estimate after four seconds, and updates after each additional 25 samples (one second). The chronological window is copied from a ring buffer before calling the SparkFun routine.

This fixes the documented sample-rate/window mismatch in software. It does **not** validate the simple four-sample boxcar decimator as a clinically adequate anti-alias filter, establish the actual FIFO rate under real I2C/loop load, detect FIFO overflow, or validate SpO2 accuracy. Confirm the effective rate and sample loss with timestamped bench measurements; inspect FIFO overflow counters if exposed by the library. The MAX30102 breakout is not thereby converted into a validated neonatal pulse oximeter.

### 2. Chest movement is not airflow

The current detector uses one raw accelerometer axis and a fixed threshold. Its output depends on orientation, gravity projection, strap placement, infant movement, vibration, and sensor drift. A movement threshold is not a validated breath detector. Chest motion alone cannot identify obstructive apnea: respiratory effort may continue despite absent airflow. A more defensible research design needs signal conditioning, orientation/gravity handling, breath-cycle segmentation, hysteresis, signal-quality and motion-artifact states, and validation against an independent respiratory reference. Consider adding an airflow/respiratory-impedance channel if the project intends to distinguish central, obstructive, and mixed events.

### 3. Timing and data integrity in firmware

Timer1 is now configured at approximately 8.003 Hz (16 MHz / (1024 × 1953)) so two chest samples nominally align with each 0.25 s frame carrying 25 PPG samples. This removes the deliberate 10 Hz versus 8 Hz cadence mismatch in the previous sketch, but it does not prove that samples are never lost. `volatile bool accelerometerDue` remains a one-bit flag; multiple timer events can collapse into one if the main loop is delayed. Sensor reads and Serial output are coupled in the same loop; serial output can block and delay acquisition. Replace the boolean with a counter/timestamped queue, track overruns, and decouple sampling from serial transmission using bounded buffers. Include sequence numbers/timestamps in a versioned protocol.

### 4. Sensor initialization and status

Firmware now calls each sensor's initialization once and does not start the sampling timer if either sensor fails its initial connection check. Runtime disconnection, sensor reset/recovery, stale-data state, and signal-quality reporting are still not implemented in the app or protocol.

### 5. Runtime freshness and signal quality

The app previously retained the last valid SpO2 value indefinitely. The development branch now clears SpO2 on an invalid sensor result and expires it after 3 seconds without a new valid result. It also marks the entire frame stream stale after 2 seconds without a new frame, clears the displayed HR, and changes the UI to a neutral "signal lost/stale" state rather than showing an old value as current. These are engineering freshness timeouts only; they are not clinical alarm settings. Runtime sensor quality, perfusion, motion-artifact, and recovery diagnostics remain unresolved.

### 6. Clocking and gaps in the web engine

The engine derives elapsed time from the number of received chest samples. This assumes uninterrupted, perfectly paced frames. Dropped frames or serial delays distort pause duration and trend timing. Move to source timestamps or a monotonic receive clock with explicit gap detection. Do not silently interpret missing samples as absent respiratory effort.

### 7. Clinical thresholds and event definition

The current 10 s warning, 20 s apnea threshold, HR limits (100/180 bpm), and SpO2 limit (80%) are prototype constants, not validated neonatal alarm settings. The AAP clinical report defines an apnea spell as at least 20 s, or a shorter pause accompanied by bradycardia (<100 bpm), cyanosis, or pallor; classification depends on respiratory effort and airflow. The current code records only pauses over 20 s after movement resumes and does not formally combine shorter pauses with bradycardia/desaturation into an apnea event. Keep clinical definitions separate from configurable engineering test thresholds and obtain neonatal specialist input before proposing alarm logic.

### 8. Data privacy

Patient names, bed identifiers, and episode data are persisted in browser `localStorage` without encryption. Avoid real patient identifiers in this prototype. Before any clinical research, define data minimization, access controls, retention, export handling, and institution-approved storage. Browser-side encryption alone is not a complete clinical data-security architecture.

## Validation plan (required before any claim of readiness)

- Run all Node tests on Node 20+ and add deterministic tests for sensor timeouts, sample gaps, invalid/out-of-range frames, sequence loss, reconnect/disconnect, and sustained/motion-contaminated respiratory signals.
- Compile firmware for Arduino Mega 2560 with the exact documented library versions; inspect SRAM use and confirm types against the selected board's ABI. The standalone SpO2 stream helper has passed a host C++11 unit test, but this is not a substitute for compiling the complete sketch.
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

**Not for clinical use.** The software-side SpO2 sample-rate/window mismatch has been corrected, but the decimator and actual hardware timing still require bench verification. Acquisition architecture, respiratory sensing validity, timestamping, signal quality/freshness, and verification remain unresolved.
