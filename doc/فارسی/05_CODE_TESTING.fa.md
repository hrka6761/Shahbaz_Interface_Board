# 05 — تست کد و قراردادها

**فارسی** | [نسخه انگلیسی](../English/05_CODE_TESTING.en.md)

این تست‌ها باید قبل از <code dir="ltr">Windows HIL</code> و قبل از مسیر عملیاتی <code dir="ltr">Android</code> اجرا شوند.

## تست‌های <code dir="ltr">C++</code>

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_host_tests.ps1
```

## تست مستقل <code dir="ltr">Python Protocol/HIL codec</code>

```powershell
py -3 tools\windows_hil_test.py --self-test
```

این فرمان بدون برد نیز قابل اجرا است و <code dir="ltr">Protocol codec</code>، داده <code dir="ltr">Sensor</code> و ریاضیات <code dir="ltr">QNH</code> را بررسی می‌کند.

## تست <code dir="ltr">Kotlin Protocol/Session</code>

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_kotlin_protocol_tests.ps1
```

این تست <code dir="ltr">Protocol v2</code>، <code dir="ltr">CRC/COBS</code>، ساختار <code dir="ltr">Session Token</code>، مرز اتصال <code dir="ltr">USB</code>، داده <code dir="ltr">Sensor</code> و محاسبه ارتفاع را بررسی می‌کند.

## <code dir="ltr">Validator</code>ها

```powershell
py -3 tools\validate_firmware_contract.py
py -3 tools\check_firmware_safety.py
py -3 tools\validate_graphics_inputs.py
py -3 tools\check_bilingual_docs.py
```

<code dir="ltr">validate_firmware_contract.py</code> اکنون قرارداد محصول را نیز بررسی می‌کند: <code dir="ltr">shahbaz_interface_board</code> یک <code dir="ltr">USB Device</code> است، <code dir="ltr">Android + Shahbaz</code> مسیر عملیاتی است و ویندوز فقط برای توسعه و <code dir="ltr">HIL</code> استفاده می‌شود.

## ساخت واقعی <code dir="ltr">ESP-IDF</code>

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

موفقیت تست‌های مستقل جای ساخت واقعی هدف را نمی‌گیرد.

## مرز موفقیت

این تست‌ها عملکرد فیزیکی <code dir="ltr">USB</code>، سیم‌کشی واقعی، چرخه مجوز <code dir="ltr">Android USB</code> و یکپارچه‌سازی واقعی نرم‌افزار شهباز را اثبات نمی‌کنند. این موارد در سندهای 06 و 07 بررسی می‌شوند.

**مرحله بعد:** <code dir="ltr">06_WINDOWS_BOARD_HIL_TEST.fa.md</code>.
