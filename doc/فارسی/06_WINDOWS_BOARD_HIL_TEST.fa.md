# 06 — تست مستقل برد با <code dir="ltr">Windows HIL</code>

**فارسی** | [نسخه انگلیسی](../English/06_WINDOWS_BOARD_HIL_TEST.en.md)

این تست یک مسیر **مستقل برای عیب‌یابی برد و <code dir="ltr">Firmware</code>** است. هدف آن بررسی <code dir="ltr">ESP32-S3</code>، <code dir="ltr">Sensor</code>ها، <code dir="ltr">Native USB</code> و <code dir="ltr">Protocol v2</code> بدون دخالت کد نرم‌افزار <code dir="ltr">Android</code> است.

```text
SHT30 + MS5611 -> I2C -> shahbaz_interface_board -> Native USB CDC -> Windows HIL
```

**این مسیر پذیرش نهایی محصول نیست.** موفقیت این تست یعنی برد و <code dir="ltr">Firmware</code> برای ادامه تست واقعی <code dir="ltr">Android + Shahbaz</code> آماده‌اند.

## پیش‌نیازها

- تست‌های سند 05 موفق باشند.
- <code dir="ltr">Firmware</code> ساخته و روی برد بارگذاری شده باشد.
- هر دو <code dir="ltr">Sensor</code> درست متصل باشند.
- خروجی‌های فیزیکی غیرفعال باشند.
- رابط <code dir="ltr">Native USB CDC</code> در ویندوز به‌صورت <code dir="ltr">COM_USB</code> دیده شود.

## اجرای <code dir="ltr">HIL</code>

```powershell
py -3 -m pip install -r tools\requirements-test.txt
py -3 tools\windows_hil_test.py --port COM_USB --qnh-hpa 1013.25
```

یا:

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_windows_hil.ps1 -Port COM_USB -QnhHpa 1013.25
```

## مواردی که بررسی می‌شوند

- ارتباط دوطرفه <code dir="ltr">Native USB CDC</code>.
- <code dir="ltr">TimeSync</code> و <code dir="ltr">Session Token</code> غیرصفر.
- <code dir="ltr">Heartbeat</code> و رفتار ایمنی.
- <code dir="ltr">DeviceInfo</code> واقعی.
- شروع و توقف <code dir="ltr">Telemetry</code>.
- داده معتبر <code dir="ltr">SHT30</code> و <code dir="ltr">MS5611</code>.
- محاسبه مستقل ارتفاع از فشار و <code dir="ltr">QNH</code> تست.
- <code dir="ltr">Ping/Pong</code> و رد <code dir="ltr">CRC</code> خراب.
- اتصال مجدد با <code dir="ltr">Session Token</code> تازه و بدون استفاده از وضعیت قبلی.

## معیار موفقیت برد

- [ ] برد بدون <code dir="ltr">Reset</code> غیرمنتظره راه‌اندازی می‌شود.
- [ ] <code dir="ltr">COM_USB</code> برای عیب‌یابی توسعه پایدار ظاهر می‌شود.
- [ ] <code dir="ltr">Windows HIL</code> بدون خطا تمام می‌شود.
- [ ] داده <code dir="ltr">SHT30</code> معتبر و در حال تغییر است.
- [ ] داده فشار <code dir="ltr">MS5611</code> معتبر و در حال تغییر است.
- [ ] اتصال مجدد یک <code dir="ltr">Session</code> جدید ایجاد می‌کند.
- [ ] هیچ خروجی فیزیکی فعال نمی‌شود.

موفقیت این سند به معنی پذیرش کامل محصول نیست.

**مرحله بعد:** <code dir="ltr">07_ANDROID_SHAHBAZ_INTEGRATION_TEST.fa.md</code>.
