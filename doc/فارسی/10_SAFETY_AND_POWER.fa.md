# 10 — ایمنی، تغذیه و <code dir="ltr">Actuator</code>ها

**فارسی** | [نسخه انگلیسی](../English/10_SAFETY_AND_POWER.en.md)

## حالت امن برای مرحله فعلی

تست <code dir="ltr">Sensor</code>ها و <code dir="ltr">USB</code> باید با <code dir="ltr">Actuator</code> خاموش انجام شود:

```text
CONFIG_SHAHBAZ_ACTUATORS_ENABLE=n
```

موتور، <code dir="ltr">ESC</code>، <code dir="ltr">Servo</code> و پروانه برای پذیرش مرحله فعلی لازم نیستند.

## برق و <code dir="ltr">GPIO</code>

- منطق <code dir="ltr">ESP32-S3</code> در حوزه 3.3<code dir="ltr">V</code> است.
- 5<code dir="ltr">V</code> منطق را مستقیماً به <code dir="ltr">SDA/SCL</code> یا <code dir="ltr">GPIO</code>های برد ندهید.
- برای این پروژه <code dir="ltr">Sensor</code>ها با 3.3<code dir="ltr">V</code> استفاده می‌شوند.
- <code dir="ltr">GND</code> برد و <code dir="ltr">Sensor</code>ها باید مشترک باشد.
- مسیر دقیق 5<code dir="ltr">V/VIN/VBUS</code> روی برد فیزیکی سازگار با <code dir="ltr">YD-ESP32-S3</code> باید قبل از تغذیه هم‌زمان از چند منبع بررسی شود؛ تغذیه هم‌زمان خارجی و <code dir="ltr">USB</code> بدون بررسی برگشت جریان انجام نشود.

## <code dir="ltr">Heartbeat</code> و حالت ایمن

فرمان‌های حرکتی پشت <code dir="ltr">SafetySupervisor</code> قرار دارند. قطع <code dir="ltr">USB</code> یا <code dir="ltr">Heartbeat</code> معتبر امکان کنترل فعال را از بین می‌برد. هنگام مسلح‌بودن، توقف فرمان تازه <code dir="ltr">Actuator</code> نیز مستقل از <code dir="ltr">Heartbeat</code> خروجی‌ها را به حالت امن می‌برد؛ مقدار تولیدی این مهلت <code dir="ltr">250 ms</code> است. بنابراین ادامهٔ حلقهٔ نگهداری <code dir="ltr">USB</code> در <code dir="ltr">Android</code> نمی‌تواند در صورت توقف حلقهٔ کنترل پرواز، آخرین <code dir="ltr">PWM</code> را فعال نگه دارد. برای آزمایش رومیزی <code dir="ltr">Sensor</code>ها <code dir="ltr">Actuator</code>ها را کاملاً غیرفعال نگه دارید تا این مسیر اصلاً وارد تست نشود.

## هنگام توسعه <code dir="ltr">Actuator</code>ها

- پروانه‌ها فیزیکی جدا باشند.
- هر تست <code dir="ltr">Windows Board-level Actuator HIL</code> فقط با پرچم صریح تست انجام شود؛ این مسیر برای توسعه است و بخشی از پذیرش عملیاتی <code dir="ltr">Android</code> نیست.
- شکل موج و <code dir="ltr">pulse range</code> روی آزمایش رومیزی با تجهیزات مناسب بررسی شود.
- تست <code dir="ltr">Actuator</code> بخشی از پذیرش فعلی <code dir="ltr">Sensor</code>/<code dir="ltr">USB</code> نیست.

**مطالعه فنی بعدی:** <code dir="ltr">11_SOFTWARE_ARCHITECTURE.fa.md</code>.
## سخت‌سازی زمان اجرا که اکنون در کد <code dir="ltr">enforce</code> می‌شود

- <code dir="ltr">Peripheral</code>های فیزیکی <code dir="ltr">Actuator</code> فقط **بعد از** موفقیت اعتبارسنجی برد، حافظه، <code dir="ltr">GPIO</code> و <code dir="ltr">Evidence</code> <code dir="ltr">Initialize</code> می‌شوند.
- فعال‌کردن <code dir="ltr">Actuator</code> نیازمند بررسی <code dir="ltr">Pin</code> روی برد دقیق و رکورد <code dir="ltr">Evidence</code> مجاز است؛ <code dir="ltr">Contract Validator</code> زمان <code dir="ltr">Build</code> <code dir="ltr">ID</code> ساختگی یا نامناسب را رد می‌کند.
- <code dir="ltr">Pin</code> نامعتبر <code dir="ltr">I2C</code> هم در <code dir="ltr">app_main</code> و هم داخل <code dir="ltr">ESP-IDF I2C Adapter</code>، از جمله مسیر <code dir="ltr">Bus Recovery</code>، به‌صورت <code dir="ltr">fail-closed</code> رد می‌شود.
- در <code dir="ltr">USB Reconnect</code> یا بازشدن دوباره <code dir="ltr">CDC DTR</code>، وضعیت <code dir="ltr">RX/TX</code> و <code dir="ltr">Protocol Session</code> پیش از پذیرش <code dir="ltr">Session Token</code> تصادفی تازه پاک می‌شود.
- فرمان وابسته به <code dir="ltr">Session</code> باید <code dir="ltr">Token</code> فعلی و <code dir="ltr">Sender Timestamp</code> معتبر داخل پنجره تازگی <code dir="ltr">v2</code> داشته باشد؛ ترافیک قدیمی/بازپخش‌شده نمی‌تواند <code dir="ltr">heartbeat/control freshness</code> را تازه کند.
- <code dir="ltr">app_main</code> عضو <code dir="ltr">ESP-IDF Task Watchdog</code> است. تنظیم پیش‌فرض پروژه <code dir="ltr">Timeout</code> دو ثانیه با <code dir="ltr">panic/reset</code> است. <code dir="ltr">TaskHealthMonitor</code> نیز سلامت سطح سرویس را گزارش می‌کند و می‌تواند <code dir="ltr">CriticalTaskFailure</code> را قفل کند.

<code dir="ltr">Watchdog</code> یک مسیر ایمنی نرم‌افزاری/<code dir="ltr">Reset</code> است و در طراحی پروازی بحرانی جای مسیر مستقل سخت‌افزاری <code dir="ltr">motor-enable/kill</code> را نمی‌گیرد.
