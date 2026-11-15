# vl53l1x_library (vendored)

[grrtzm/vl53l1x_library](https://github.com/grrtzm/vl53l1x_library) 0.3.1 from the
ESP component registry: STMicroelectronics' VL53L1X ULP API ported to ESP-IDF's
new I2C master driver. BSD-3-Clause; see `LICENSE` and `LICENSES/`.

It's copied here for one change: upstream's I2C calls wait forever (timeout -1), so
a hung bus would stop the sensor task for good. `src/vl53l1x_ulp_platform_esp.c`
gives them a 50 ms timeout instead (`I2C_TIMEOUT_MS`). Nothing else differs.
To update, copy the new release over this folder and reapply that change.
