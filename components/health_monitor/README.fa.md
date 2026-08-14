# پایش سلامت (<code dir="ltr">Health Monitor</code>)

**فارسی** | [نسخه انگلیسی](README.en.md)

این <code dir="ltr">Component</code> رکوردهای محدود <code dir="ltr">Liveness</code> سرویس‌های مهم را نگه می‌دارد. اکنون <code dir="ltr">app_main</code> سرویس‌های <code dir="ltr">USB RX/TX</code>، فرمان، <code dir="ltr">Sensor</code>، <code dir="ltr">Telemetry</code>، ایمنی و نگهداری را ثبت می‌کند و ماسک ناسالم بحرانی به‌صورت <code dir="ltr">CriticalTaskFailure</code> در <code dir="ltr">SafetySupervisor</code> قفل می‌شود.

چون معماری فعلی همچنان یک <code dir="ltr">Application Task</code> اصلی دارد، آشکارساز مستقل گیرکردن، <code dir="ltr">ESP-IDF Task Watchdog</code> است. <code dir="ltr">app_main</code> عضو <code dir="ltr">TWDT</code> می‌شود و فقط بعد از کامل شدن یک دور سرویس آن را <code dir="ltr">Feed</code> می‌کند. با تنظیمات پروژه، <code dir="ltr">Timeout</code> دوثانیه‌ای به مسیر <code dir="ltr">panic/reset</code> منتهی می‌شود. <code dir="ltr">Health Monitor</code> برای عیب‌یابی سطح سرویس است و جای <code dir="ltr">Watchdog</code> را نمی‌گیرد.
