# T_Display_S3_Clock_worksgreat

This project is a clock display program for the [LilyGO T-Display S3](https://www.lilygo.cc/products/t-display-s3), written for use with the Arduino IDE.

## Features

- Designed for LilyGO T-Display S3 development board
- Written in Arduino language (.ino)
- Displays clock information on the onboard screen

## Hardware Requirements

- LilyGO T-Display S3 development board
- USB Type-C cable

## Software Requirements

- [Arduino IDE](https://www.arduino.cc/en/software)
- Board support for T-Display S3 (ESP32)
- Required libraries (install according to `#include` directives in the .ino file)

## Installation

1. Install the Arduino IDE.
2. Add support for the T-Display S3 board in Arduino IDE (add the ESP32 board manager URL in "Preferences", then install ESP32 via the "Boards Manager").
3. Download this project and open `T_Display_S3_Clock_worksgreat.ino` in Arduino IDE.
4. Install all required libraries as indicated by the `#include` statements (use "Tools" > "Manage Libraries" to search and install).
5. Select the correct board model and port.
6. Click "Upload" to flash the program to your T-Display S3.

## Usage

1. After uploading, the device will automatically start and display the clock on the screen.
2. To customize the display, edit the `.ino` file and re-upload.

## References

- [LilyGO Official Website](https://www.lilygo.cc/)
- [T-Display S3 Product Page](https://www.lilygo.cc/products/t-display-s3)
- [Arduino IDE Official Website](https://www.arduino.cc/)

## License

This project is licensed under the MIT License. See the LICENSE file for details (if available).
