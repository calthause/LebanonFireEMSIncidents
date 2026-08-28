# Fire & EMS Incidents

A minimal PlatformIO project for ESP32 with CYD display.
CYD used is the dual USB model

## Screenshots

Drop image files into `images/` and reference them here, e.g.:

```markdown
![Dashboard](images/dashboard.jpg)
![Unit Info popup](images/unit-info-popup.jpg)
![Incident map popup](images/county-map-popup.jpg)
```

## Build & Upload

- **Build**: `pio run -e esp32dev`
- **Upload**: `pio run --target upload -e esp32dev`
- **Monitor**: `pio device monitor`

## Adding Libraries

To add libraries later, update `platformio.ini` under `lib_deps`:

```ini
lib_deps =
    lovyan03/LovyanGFX @ ^1.1.16
    <new-library-name> @ ^<version>
```

Then rebuild.
