# 07 — تست یکپارچه‌سازی <code dir="ltr">Android + Shahbaz</code>

**فارسی** | [نسخه انگلیسی](../English/07_ANDROID_SHAHBAZ_INTEGRATION_TEST.en.md)

این سند **تست کامل مسیر عملیاتی مرحله فعلی محصول** است.

```text
SHT30 + MS5611
      -> I2C
      -> shahbaz_interface_board / ESP32-S3
      -> Native USB CDC / Shahbaz Protocol v2
      -> Android phone (USB Host)
      -> Shahbaz Android application
```

ویندوز بخشی از این مسیر نیست.

## کد لازم در سمت <code dir="ltr">Android</code>

نرم‌افزار شهباز باید رفتاری معادل فایل‌های زیر داشته باشد:

- <code dir="ltr">android_reference/src/main/kotlin/com/shahbaz/protocol/ProvisionalProtocol.kt</code>
- <code dir="ltr">android_reference/src/main/kotlin/com/shahbaz/protocol/ShahbazLinkSession.kt</code>
- <code dir="ltr">android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazUsbCdcTransport.kt</code>
- <code dir="ltr">android_reference/src/android/kotlin/com/shahbaz/androidusb/ShahbazInterfaceBoardClient.kt</code>

این کد از <code dir="ltr">UsbManager</code>، رابط <code dir="ltr">CDC</code>، <code dir="ltr">bulk IN/OUT</code>، مرز واقعی اتصال و قطع اتصال، <code dir="ltr">Protocol v2</code>، <code dir="ltr">Heartbeat</code>، نوسازی دوره‌ای <code dir="ltr">TimeSync</code>، رمزگشایی داده <code dir="ltr">Sensor</code> و محاسبه ارتفاع با <code dir="ltr">QNH</code> استفاده می‌کند.

## 1. مجوز <code dir="ltr">USB</code> و انتخاب دستگاه

1. <code dir="ltr">Native USB</code> برد را با کابل یا مبدل دارای قابلیت <code dir="ltr">USB Host/OTG</code> به گوشی متصل کنید.
2. نرم‌افزار شهباز باید یک دستگاه <code dir="ltr">CDC</code> سازگار پیدا کند.
3. نرم‌افزار باید مجوز <code dir="ltr">USB</code> را به‌صورت صریح از کاربر بگیرد.
4. دستگاه فقط بعد از دریافت مجوز باز شود.
5. رابط <code dir="ltr">USB-UART</code> مخصوص توسعه نباید به‌عنوان لینک عملیاتی انتخاب شود.

## 2. ایجاد <code dir="ltr">Session</code>

پس از بازشدن <code dir="ltr">USB</code>، نرم‌افزار باید وضعیت قبلی را پاک کند، <code dir="ltr">TimeSyncRequest</code> را با زمان یکنواخت <code dir="ltr">Android</code> بفرستد، پاسخ را بررسی کند، <code dir="ltr">Session Token</code> غیرصفر بگیرد، <code dir="ltr">DeviceInfo</code> بخواهد، <code dir="ltr">Telemetry</code> را شروع کند و <code dir="ltr">Heartbeat</code> و نوسازی دوره‌ای <code dir="ltr">TimeSync</code> را ادامه دهد.

## 3. داده زنده در نرم‌افزار شهباز

در خود نرم‌افزار شهباز بررسی شود که دمای <code dir="ltr">SHT30</code>، رطوبت <code dir="ltr">SHT30</code>، فشار <code dir="ltr">MS5611</code> و در صورت استفاده دمای داخلی <code dir="ltr">MS5611</code> به‌صورت پایدار و منطقی به‌روز می‌شوند.

## 4. <code dir="ltr">QNH</code> و ارتفاع بارومتریک

برد فشار را ارسال می‌کند و مالک <code dir="ltr">QNH</code> عملیاتی نیست. یک مقدار معلوم <code dir="ltr">QNH</code> در نرم‌افزار شهباز تنظیم شود و این رابطه بررسی شود:

```text
barometric altitude = f(MS5611 pressure, Shahbaz-app QNH)
```

تغییر <code dir="ltr">QNH</code> باید ارتفاع محاسبه‌شده را تغییر دهد، اما فشار خام برد نباید تغییر کند.

## 5. قطع و اتصال مجدد

پس از قطع <code dir="ltr">USB</code>، نرم‌افزار باید فوراً برد را قطع‌شده بداند و <code dir="ltr">Token</code> و وضعیت قبلی را کنار بگذارد. پس از اتصال مجدد و دریافت مجوز لازم، یک <code dir="ltr">TimeSync</code> و <code dir="ltr">Session</code> جدید ایجاد شود. <code dir="ltr">Token</code> جدید باید غیرصفر و متعلق به اتصال جدید باشد و داده قدیمی نباید معتبر تلقی شود.

## 6. خطاهای چرخه عمر

حداقل این موارد بررسی شوند:

- رد مجوز <code dir="ltr">USB</code> باعث <code dir="ltr">Crash</code> نشود.
- قطع کابل هنگام <code dir="ltr">Telemetry</code> باعث <code dir="ltr">Crash</code> نشود.
- تغییر وضعیت <code dir="ltr">foreground/background</code> اتصال بسته‌شده را دوباره استفاده نکند.
- <code dir="ltr">SessionMismatch</code> باعث ایجاد <code dir="ltr">Session</code> تازه شود.
- <code dir="ltr">StaleOrExpired</code> با <code dir="ltr">TimeSync</code> تازه و زمان فعلی مدیریت شود.

## معیار موفقیت <code dir="ltr">Android + Shahbaz</code>

- [ ] گوشی رابط <code dir="ltr">Native USB CDC</code> را پیدا می‌کند.
- [ ] نرم‌افزار شهباز مجوز <code dir="ltr">USB</code> را می‌گیرد و لینک را باز می‌کند.
- [ ] <code dir="ltr">TimeSync</code> موفق است.
- [ ] <code dir="ltr">Session Token</code> غیرصفر ایجاد می‌شود.
- [ ] <code dir="ltr">Heartbeat</code> سالم می‌ماند.
- [ ] داده زنده <code dir="ltr">SHT30</code> وارد نرم‌افزار شهباز می‌شود.
- [ ] فشار زنده <code dir="ltr">MS5611</code> وارد نرم‌افزار شهباز می‌شود.
- [ ] ارتفاع از فشار و <code dir="ltr">QNH</code> نرم‌افزار شهباز محاسبه می‌شود.
- [ ] تغییر <code dir="ltr">QNH</code> فشار خام را تغییر نمی‌دهد.
- [ ] اتصال مجدد یک <code dir="ltr">Session</code> پاک و تازه می‌سازد.
- [ ] رد مجوز و خطاهای اتصال ایمن مدیریت می‌شوند.
- [ ] هیچ خروجی فیزیکی فعال نمی‌شود.

فقط پس از موفقیت این سند، پذیرش کامل مرحله فعلی بررسی شود.

**مرحله بعد:** <code dir="ltr">08_COMPLETE_SYSTEM_ACCEPTANCE.fa.md</code>.
