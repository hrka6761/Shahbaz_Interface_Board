# <code dir="ltr">Actuator</code> مجازی و امن (<code dir="ltr">Null Actuator</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

<code dir="ltr">Component</code> <code dir="ltr">actuator_null</code> زمانی استفاده می‌شود که خروجی‌های فیزیکی غیرفعال باشند. این <code dir="ltr">Component</code> همان <code dir="ltr">Interface</code> مشترک <code dir="ltr">Actuator</code> را پیاده‌سازی می‌کند، اما همیشه سخت‌افزار را غیرقابل‌دسترس گزارش می‌دهد و هیچ <code dir="ltr">GPIO</code> یا <code dir="ltr">PWM</code> را فعال نمی‌کند.

## فایل‌های اصلی

- <code dir="ltr">include/shahbaz/actuator/null_actuator_controller.hpp</code>: پیاده‌سازی امن <code dir="ltr">Interface</code> کنترل <code dir="ltr">Actuator</code>.
- <code dir="ltr">src/null_actuator_controller.cpp</code>: رفتار حالت بسته و ایمن بدون دسترسی به سخت‌افزار.
- <code dir="ltr">test/test_null_actuator.cpp</code>: ثابت می‌کند راه‌اندازی اولیه، <code dir="ltr">Arming</code> و فرمان‌ها نمی‌توانند خروجی را فعال کنند.
- <code dir="ltr">CMakeLists.txt</code> و <code dir="ltr">test/CMakeLists.txt</code>: ثبت <code dir="ltr">Component</code> <code dir="ltr">ESP-IDF</code> و <code dir="ltr">Build</code> تست <code dir="ltr">Host</code>.

وقتی <code dir="ltr">CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n</code> باشد، <code dir="ltr">Firmware</code> از این پیاده‌سازی استفاده می‌کند. خروجی <code dir="ltr">PWM</code> واقعی به‌صورت جداگانه در <code dir="ltr">components/actuator_espidf</code> پیاده‌سازی شده است.
