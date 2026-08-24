# T-QT Pro animations

Repository: https://github.com/yuxb2/tqt-pro-animation

## OTA updates

1. Copy `CyberCycle/secrets.example.h` to `CyberCycle/secrets.h` and enter the
   Wi-Fi name and password. Do not commit `secrets.h`.
2. For the first OTA-capable installation, upload `CyberCycle` over USB with:
   - Board: **ESP32S3 Dev Module**
   - Flash size: **16MB**
   - Partition scheme: **16M Flash (3MB APP/9.9MB FATFS)**
3. To publish an OTA version, commit your work, then create and push a tag:

   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

GitHub Actions compiles the tag, creates a Release, and updates the public
`firmware` branch. CyberCycle checks that branch 15 seconds after startup and
then every 24 hours.
