# RoomAssist (智管家)

智慧房間管理助手 - 整合空氣品質監測、人體偵測與 HomeKit 的多功能智慧家居設備

## 主要功能

- **環境監測**：溫濕度 (DHT22)、空氣品質 (SGP41 VOC/NOx, MQ135，特定規格)
- **人體偵測**：LD2410 毫米波雷達 + FNN 神經網路智慧判斷
- **聲音偵測**：MAX9814 麥克風
- **紅外線控制**：支援 Hitachi 冷氣、Delta/Zero 風扇
- **HomeKit 整合**：完整 Apple HomeKit 支援
- **顯示介面**：SSD1306 OLED 即時資訊顯示
- **網頁控制**：內建 Web 介面
- **雲端上傳**：ThingSpeak 資料記錄
- **OTA 更新**：支援無線韌體更新

---

This is a smart room management assistant that integrates air quality monitoring, occupancy detection, and HomeKit control capabilities.

## Console

The prompt `esp32>` would appear on the serial console. Enter `help` to see all the available commands & their usage in the emulator.

### Available Commands

- `profile-list` : Lists all the supported profiles by this homekit sdk.
- `profile <profile-name>` : Entering a valid profile Id from the profile list will lead to the board emulating the accessory selected after rebooting.
- `read-char <aid>(optional) <iid>(optional)`: Without any arguments it prints the _characteristics and services of all the accessories_. With an aid it prints all the _characteristics and services pertaining to the given accessory._ With an aid and an iid, it will print the _given characteristic or all characteristics from the given service_.
- `write-char <aid> <iid> <value>` : Writes the value to the particular characteristic
- `reset` : Factory resets the device erasing the pairing and provisioning information.
- `reset-pairings` : Erases the pairing information from the device.
- `reset-network`: Erases the network credentials from the device.
- `reboot` : Reboots the accessory.
- `auth <authid>` : Sets the authentication mechanism (None, Hardware or Software auth).

## Third-Party Libraries

This project uses the following third-party libraries, included in the `components/` directory:

- **DHT Sensor Driver**: From [UncleRus/esp-idf-lib](https://github.com/UncleRus/esp-idf-lib) (BSD License)
  - Supports DHT11, DHT22, AM2301, AM2302, AM2321, and Si7021 sensors
  - Copyright (c) 2016 Jonathan Hartsuiker, 2018 Ruslan V. Uss

- **SSD1306 OLED Driver**: From [nopnop2002/esp-idf-ssd1306](https://github.com/nopnop2002/esp-idf-ssd1306) (MIT License)
  - Supports SSD1306 and SH1106 OLED displays (128x64, 128x32, 128x128)
  - I2C and SPI interfaces supported
  - Copyright (c) 2022 nopnop2002
