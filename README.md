# Smart Canteen Management

Smart Canteen Management is an IoT-based canteen automation project built with Arduino and ESP modules.

This repository contains firmware for:
- Arduino-side logic (order handling, display/control flow, date-based flow as implemented in sketch)
- ESP-side logic (network communication and kitchen-ready workflow)

## Project Files

- `SMART_CANTEEN_ARD_WITH_DATE.ino`
- `SMART_CANTEEN_ESP_WITH_KITCHEN_READY.ino`
- `SMART_CANTEEN_ARD_WITH_DATE/SMART_CANTEEN_ARD_WITH_DATE.ino`
- `SMART_CANTEEN_ESP_WITH_KITCHEN_READY/SMART_CANTEEN_ESP_WITH_KITCHEN_READY.ino`

## Requirements

- Arduino IDE (latest stable version recommended)
- Required board package for your ESP board (for example ESP8266 or ESP32)
- USB cable and drivers for your board
- Any libraries used by the sketches (install from Arduino Library Manager if prompted)

## How To Run

1. Open Arduino IDE.
2. Open the sketch you want to upload:
   - Arduino firmware: `SMART_CANTEEN_ARD_WITH_DATE.ino`
   - ESP firmware: `SMART_CANTEEN_ESP_WITH_KITCHEN_READY.ino`
3. Select the correct board from Tools > Board.
4. Select the correct COM port from Tools > Port.
5. Click Verify to compile.
6. Click Upload to flash the board.
7. Open Serial Monitor (if required by your code) to monitor logs and status messages.

## Suggested Workflow

- Upload Arduino sketch to the Arduino board.
- Upload ESP sketch to the ESP board.
- Power both devices and verify communication based on your configured pins, serial settings, and network settings.

## Notes

- Update Wi-Fi credentials or server settings inside the ESP sketch before uploading.
- Ensure baud rates and serial interfaces match on both sides.
- Keep both sketches in sync when changing command formats or message structure.

## Git Quick Commands

After making changes:

```bash
git add .
git commit -m "Update firmware"
git push
```

## Author

Akash Krishna
