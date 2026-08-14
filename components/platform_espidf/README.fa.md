# <code dir="ltr">ESP-IDF Platform Adapters</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این بخش قراردادهای مستقل پروژه را به سرویس‌های واقعی <code dir="ltr">ESP-IDF</code> روی <code dir="ltr">ESP32-S3</code> متصل می‌کند.

## <code dir="ltr">Adapter</code>ها اصلی

- <code dir="ltr">espidf_monotonic_clock</code>: ساعت یکنواخت میکروثانیه‌ای بر پایه <code dir="ltr">esp_timer</code>.
- <code dir="ltr">espidf_board_validator</code>: بررسی <code dir="ltr">Flash/PSRAM</code>، سیاست <code dir="ltr">GPIO</code> و <code dir="ltr">Evidence Gate</code>های پروژه، از جمله <code dir="ltr">Pin</code>/مدرک <code dir="ltr">Actuator</code>.
- <code dir="ltr">espidf_i2c_bus</code>: <code dir="ltr">I2C Master</code> واقعی با عملیات محدود و بازیابی <code dir="ltr">Bus</code>. **هم <code dir="ltr">initialize</code> و هم <code dir="ltr">recover</code> سیاست <code dir="ltr">Pin</code> برد را داخل خود <code dir="ltr">Driver</code> <code dir="ltr">enforce</code> می‌کنند**؛ بنابراین هیچ فراخواننده‌ای نمی‌تواند با دورزدن اعتبارسنجی <code dir="ltr">app_main</code> یک <code dir="ltr">Pin</code> رزروشده <code dir="ltr">USB</code>، <code dir="ltr">Pin</code> <code dir="ltr">strapping</code>، <code dir="ltr">Pin</code> ناموجود یا مسیر حافظه را <code dir="ltr">Toggle</code> کند.

<code dir="ltr">app_main</code> نیز در صورت نامعتبر بودن <code dir="ltr">Pin</code>‌های <code dir="ltr">I2C</code> اصلاً آن را <code dir="ltr">Initialize</code> نمی‌کند.
