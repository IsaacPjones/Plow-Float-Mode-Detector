# Western Plow Float Detector

## Overview

This project is an Arduino Nano based float mode detector for a Western snow plow controller. The system listens to the controller's RS-485 data stream, looks for float-related byte-pattern behavior, and turns on an output light when float mode is detected.

The main goal was to create a small standalone device that could indicate float mode without needing a PC connected during normal operation.

This project was built through real hardware testing, signal logging, and iterative tuning. The controller protocol was not documented, so the detection logic was developed by observing actual controller behavior and matching repeated patterns during idle, float entry, float active, and float exit states.

---

## Hardware

- Arduino Nano
- RS-485 receiver/transceiver module
- Western plow controller
- External indicator light
- Breadboard and jumper wires
- Power source for controller and Arduino test setup

---

## Wiring

### RS-485 Module to Arduino Nano

- `RO` -> `D2`
- `RE` -> `GND`
- `DE` -> `GND`
- `DI` -> not used
- `VCC` -> `5V`
- `GND` -> `GND`

### Output

- `D5` -> external indicator light
- `D13` -> onboard LED mirror for testing

### Controller Side

The plow controller signal is connected to the RS-485 module input side.

---

## How It Works

The Arduino reads the incoming RS-485 byte stream using `SoftwareSerial` and stores recent bytes in a rolling buffer. It then scans that buffer for repeated adjacent byte pairs associated with float-like and idle-like controller behavior.

Two main pattern groups were observed during testing:

- **Float-like pairs** based on `FF 00` style transitions
- **Idle-like pairs** based on `63 00` style transitions

The sketch maintains a rolling count of these patterns and uses entry and exit conditions to decide when float mode should be considered active.

### Float Detection Logic

The code does not rely on a formal protocol decoder. Instead, it uses observed behavior from real testing:

- float entry produces a characteristic rise in float-like pair counts
- idle produces a stable repeated idle-like pattern
- float mode is turned on when enough float-entry evidence has accumulated
- float mode is turned off when idle evidence dominates again

This approach was chosen because the controller signal is noisy during transitions and did not provide a simple documented float-status bit.

---

## Limitations

This project works well in normal use, but it is still a heuristic detector built from observed signal patterns rather than a known protocol specification.

Known limitations include:

- repeatedly pressing the up button can temporarily resemble float-entry behavior
- if a button other than down is pressed during the short window between float mode activating and the Arduino registering that transition, the indicator light may not turn on even though float mode is active.

---

## Future Improvements

Possible next steps include:

- refining detection thresholds based on more logged controller data
- moving from breadboard prototype to a more permanent enclosure and PCB-based build
- adding a second output or diagnostic status indicator
- logging raw signal traces for easier future tuning

---
