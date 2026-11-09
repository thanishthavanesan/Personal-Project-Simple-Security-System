# ESP32 Smart Security System Description

A security system built on an ESP32. Scan an RFID card to arm or disarm a servo door lock. If motion is detected while armed, an alarm sounds until an authorized card is scanned. A web dashboard lets you monitor and control it from your phone or computer.

<img width="5712" height="4284" alt="IMG_7844" src="https://github.com/user-attachments/assets/25b2eb18-e689-40bb-b4ca-31b9d79d2aff" />
<img width="5707" height="3969" alt="IMG_7845" src="https://github.com/user-attachments/assets/803883a2-61ff-4a88-b35c-a23f8c92cdac" />

# Features

- Arm/disarm with an RFID card
- Unauthorized cards trigger a buzzer and red LED, and get logged
- Motion alarm with buzzer, flashing LED, and relay output
- OLED display and RGB LED show the system status
- Web dashboard with status, last scanned card, and event log

# Hardware Implementd

- ESP32
- RC522 RFID reader
- HC-SR501 PIR motion sensor
- SG90 servo (door lock)
- SSD1306 OLED display
- Active buzzer
- RGB LED
- Relay module

# Pins Used

| Part | Pin |
|---|---|
| RFID RST / SS | 27 / 5 |
| Servo | 13 |
| PIR | 14 |
| Buzzer | 25 |
| RGB (R / G / B) | 15 / 2 / 26 |
| Relay | 4 |

# Libraries Used 

Install these in the Arduino IDE: MFRC522, Adafruit GFX, Adafruit SSD1306, ESP32Servo, ArduinoJson. Requires ESP32 board package 3.x or newer.

# Setup Steps

1. Wire the parts using the pin table above.
2. Open `security_system.ino` in the Arduino IDE.
3. Enter your WiFi name and password in the code.
4. Replace the sample card IDs with your own. Scan a card and read its ID in the Serial Monitor.
5. Upload to the ESP32. The Serial Monitor shows its IP address.
6. Type that IP address into a browser to open the dashboard.

## Status Guide

|  State   |     LED      |   Door   |
|----------|--------------|----------|
| Disarmed | Green        | Unlocked |
| Armed    | Blue         | Locked   |
| Alarm    | Flashing red | Locked   |


# Limitations

The dashboard has no password, so only use it on a trusted network. RFID card IDs can also be copied, so this is a learning project, not a real security product.
