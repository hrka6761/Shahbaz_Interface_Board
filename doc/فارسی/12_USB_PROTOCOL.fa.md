# 12 — <code dir="ltr">Protocol USB</code> شهباز

**فارسی** | [نسخه انگلیسی](../English/12_USB_PROTOCOL.en.md)

## <code dir="ltr">Transport</code> و نسخه

<code dir="ltr">Firmware</code> از <code dir="ltr">USB CDC-ACM</code> داخلی استفاده می‌کند. روی این <code dir="ltr">Byte Stream</code>، **<code dir="ltr">Shahbaz wire protocol v2</code>** با <code dir="ltr">COBS</code> قاب‌بندی و با <code dir="ltr">CRC-32C</code> محافظت می‌شود.

بدنه <code dir="ltr">Decode</code>شده همچنان <code dir="ltr">Header</code> ثابت 22 بایتی <code dir="ltr">little-endian</code>، حداکثر 512 بایت <code dir="ltr">Payload</code> و 4 بایت <code dir="ltr">CRC-32C</code> دارد. <code dir="ltr">Protocol v2</code> عمداً برای ترافیک کنترلی با <code dir="ltr">v1</code> سازگاری سیمی ندارد، چون <code dir="ltr">v2</code> برای فرمان‌های وابسته به <code dir="ltr">Session</code> یک <code dir="ltr">Session Token</code> مذاکره‌شده اضافه می‌کند.

## اصول <code dir="ltr">Frame</code>

هر <code dir="ltr">Frame</code> شامل نسخه، نوع، اولویت، شماره توالی، زمان یکنواخت فرستنده، طول <code dir="ltr">Payload</code>، خود <code dir="ltr">Payload</code> و <code dir="ltr">CRC</code> است. <code dir="ltr">Decoder</code> باید <code dir="ltr">Frame</code> خراب یا بدساخت را رد کند و بدون راه‌اندازی مجدد دستگاه روی جداکننده صفر بعدی <code dir="ltr">resync</code> شود.

شماره‌های توالی داخل <code dir="ltr">Session</code> منطقی فعلی <code dir="ltr">CDC</code> بررسی می‌شوند. <code dir="ltr">Frame</code> ردشده به‌علت <code dir="ltr">CRC</code> شماره توالی خود را مصرف یا <code dir="ltr">commit</code> نمی‌کند.

## ایجاد <code dir="ltr">Session</code>

<code dir="ltr">USB Host</code> تازه متصل‌شده فوراً اجازه ارسال فرمان‌های وابسته به <code dir="ltr">Session</code> را ندارد. <code dir="ltr">TinyUSB mount</code> فقط به این معناست که گوشی دستگاه <code dir="ltr">USB</code> را پیکربندی کرده است؛ <code dir="ltr">Session</code> منطقی <code dir="ltr">Protocol</code> پس از فعال‌شدن <code dir="ltr">CDC DTR</code> توسط <code dir="ltr">Host</code> باز می‌شود.

1. پس از در اختیار گرفتن رابط‌های <code dir="ltr">CDC</code>، <code dir="ltr">Host</code> ابتدا <code dir="ltr">DTR</code> را غیرفعال و سپس فعال می‌کند. بستن و بازکردن <code dir="ltr">DTR</code> روی کابلی که همچنان متصل است عمداً یک <code dir="ltr">Session</code> منطقی تازه می‌سازد.
2. در هر تغییر <code dir="ltr">mount/unmount</code> فیزیکی یا <code dir="ltr">DTR</code>، <code dir="ltr">Firmware</code> حافظه‌های <code dir="ltr">RX/TX FIFO</code> در <code dir="ltr">TinyUSB</code> را پاک، صف <code dir="ltr">RX</code> دارای برچسب <code dir="ltr">epoch</code> را تخلیه و صف <code dir="ltr">TX</code>، وضعیت <code dir="ltr">Parser</code>، شماره توالی، وضعیت <code dir="ltr">Telemetry Session</code> و نگاشت زمان را بازنشانی می‌کند. <code dir="ltr">Session</code> فقط هنگامی پذیرفته می‌شود که <code dir="ltr">USB</code> همچنان <code dir="ltr">mounted</code>، <code dir="ltr">DTR</code> فعال و پاک‌سازی مرز کامل شده باشد. هر بخش <code dir="ltr">RX</code> و <code dir="ltr">Frame</code> صف‌شده <code dir="ltr">TX</code>، <code dir="ltr">connection epoch</code> خود را دارد تا رقابت میان <code dir="ltr">callback/main loop</code> نتواند ترافیک را وارد <code dir="ltr">Session</code> بعدی کند.
3. <code dir="ltr">Firmware</code> با <code dir="ltr">Hardware RNG</code> خود <code dir="ltr">ESP32</code> یک <code dir="ltr">Session Token</code> غیرصفر 64 بیتی تازه می‌سازد.
4. <code dir="ltr">Host</code> یک <code dir="ltr">TimeSyncRequest</code> می‌فرستد و زمان یکنواخت خود را هم در <code dir="ltr">Header</code> و هم در <code dir="ltr">Payload</code> هشت‌بایتی درخواست قرار می‌دهد.
5. <code dir="ltr">TimeSyncResponse</code> دارای 32 بایت است:

```text
u64 client_send_us
u64 device_rx_us
u64 device_tx_us
u64 session_token
```

6. <code dir="ltr">Host</code> باید تا زمان بسته‌شدن <code dir="ltr">DTR</code> یا پایان اتصال فیزیکی <code dir="ltr">USB</code> این <code dir="ltr">Token</code> را برای همه درخواست‌های وابسته به <code dir="ltr">Session</code> استفاده کند.

<code dir="ltr">Token</code> مربوط به <code dir="ltr">Session</code> منطقی قبلی با <code dir="ltr">CommandNack / SessionMismatch</code> رد می‌شود. <code dir="ltr">ShahbazLinkSession</code> سمت <code dir="ltr">Android</code> نیز هر 30 ثانیه <code dir="ltr">TimeSync</code> را تازه می‌کند تا اختلاف تدریجی ساعت <code dir="ltr">Host</code> و دستگاه در <code dir="ltr">Session</code>های طولانی محدود بماند؛ خود <code dir="ltr">Session Token</code> تا زمانی که همان <code dir="ltr">Session</code> منطقی <code dir="ltr">CDC</code> باز است تغییر نمی‌کند.

<code dir="ltr">Session Token</code> مکانیزم اتصال پیام به <code dir="ltr">Session</code> و جلوگیری از پذیرش داده قدیمی است و **جایگزین احراز هویت <code dir="ltr">Host</code> یا رمزنگاری نیست**. این مکانیزم ترافیک بافرشده/کنترلی اتصال فیزیکی یا <code dir="ltr">Session</code> منطقی قبلی را از <code dir="ltr">Session</code> جدید جدا می‌کند، اما در برابر <code dir="ltr">Host</code> مخربی که هم‌اکنون متصل است و <code dir="ltr">Token</code> جاری را می‌داند، حفاظت احراز هویت ایجاد نمی‌کند.

## فرمان‌های وابسته به <code dir="ltr">Session</code>

<code dir="ltr">Payload</code> پیام‌های زیر در مسیر <code dir="ltr">Host</code> به دستگاه روی سیم ابتدا این پیشوند را دارد:

```text
u64 session_token
```

و سپس <code dir="ltr">Payload</code> منطقی پیام قرار می‌گیرد:

- <code dir="ltr">StartTelemetry</code>
- <code dir="ltr">StopTelemetry</code>
- <code dir="ltr">SetSensorRate</code>
- <code dir="ltr">Heartbeat</code>
- <code dir="ltr">HeartbeatAck</code>
- <code dir="ltr">ArmRequest</code>
- <code dir="ltr">ArmConfirm</code>
- <code dir="ltr">ActuatorCommand</code>
- <code dir="ltr">MotorCommand</code>
- <code dir="ltr">ServoCommand</code>
- <code dir="ltr">SetControlMode</code>

<code dir="ltr">DeviceInfoRequest</code>، <code dir="ltr">DeviceStatusRequest</code>، <code dir="ltr">Ping</code> و <code dir="ltr">TimeSyncRequest</code> برای عیب‌یابی/ایجاد <code dir="ltr">Session</code> بدون <code dir="ltr">Token</code> باقی می‌مانند.

<code dir="ltr">EmergencyStop</code> و <code dir="ltr">Disarm</code> عمداً **<code dir="ltr">Safety Override</code> بدون <code dir="ltr">Token</code>** هستند. حتی یک <code dir="ltr">Frame</code> غیرکانونی یا قدیمی از این دو نوع اجازه دارد وضعیت ایمن‌تر را درخواست کند، ولی فقط <code dir="ltr">Frame</code> معتبر و کانونی می‌تواند وضعیت عادی <code dir="ltr">Freshness/Sequence</code> را جلو ببرد.

## تازگی زمان فرستنده

بعد از <code dir="ltr">TimeSync</code> معتبر، <code dir="ltr">Firmware</code> ساعت یکنواخت فرستنده را به زمان دریافت دستگاه نگاشت می‌کند. فرمان‌هایی که به زمان همگام نیاز دارند فقط در پنجره <code dir="ltr">Freshness</code> نسخه 2 پذیرفته می‌شوند:

```text
maximum sender age       = 250 ms
maximum future tolerance = 50 ms
```

<code dir="ltr">Timestamp</code> قدیمی، زمانی که از مبنای همگام‌سازی عقب‌تر باشد، یا زمانی که بیش از حد در آینده باشد با <code dir="ltr">StaleOrExpired</code> رد می‌شود. <code dir="ltr">Frame</code> قدیمی/منقضی‌شده، تازگی <code dir="ltr">valid frame</code>، <code dir="ltr">heartbeat</code> یا فرمان کنترلی را تازه نمی‌کند. یک <code dir="ltr">TimeSyncRequest</code> که ساعت فرستنده در آن رو به جلو حرکت کرده باشد اجازه دارد نگاشت زمانی پیرشده را جایگزین کند، حتی اگر همان نگاشت قدیمی زمان جدید را قدیمی تشخیص دهد؛ اما عقب‌گرد ساعت فرستنده به مقداری کمتر از مبنای همگام‌سازی موجود همچنان رد می‌شود. به این ترتیب همگام‌سازی دوره‌ای می‌تواند <code dir="ltr">Clock Drift</code> انباشته را ترمیم کند، بدون آنکه پذیرش مجدد پیام‌های بازپخش‌شده باز شود.

## <code dir="ltr">DeviceInfoResponse v2</code>

<code dir="ltr">DeviceInfoResponse</code> اکنون 20 بایت است و به‌جای ادعاهای <code dir="ltr">hard-code</code>، واقعیت‌های <code dir="ltr">Runtime</code> را گزارش می‌کند:

```text
u8  protocol_version
u8  target_family                 # 1 = ESP32-S3
u8  supported_motor_channels
u8  supported_servo_channels
u32 detected_flash_bytes
u32 detected_psram_bytes
u32 board_validation_issue_mask
u8  active_motor_channels
u8  active_servo_channels
u8  actuator_available
u8  actuators_enabled_by_config
```

در <code dir="ltr">Build</code> پیش‌فرض <code dir="ltr">Sensor</code>/<code dir="ltr">USB</code>، <code dir="ltr">Protocol</code> ظرفیت منطقی طراحی‌شدهٔ 4 موتور و 2 <code dir="ltr">Servo</code> را گزارش می‌کند، اما تعداد کانال فعال صفر و <code dir="ltr">actuator_available=0</code> است. پروفایل پیش‌فرض <code dir="ltr">SHAHBAZ_ACTUATOR_BACKEND=null</code> کد خروجی فیزیکی و <code dir="ltr">LEDC</code> را از تصویر حذف می‌کند. در ساخت مجاز دارای خروجی، کانال‌ها فقط پس از موفقیت <code dir="ltr">LEDC PWM Backend</code> و عبور از اعتبارسنجی برد و مدرک <code dir="ltr">Actuator</code> فعال می‌شوند.

## فرمان‌های <code dir="ltr">Actuator</code>

خروجی فیزیکی <code dir="ltr">PWM</code> به‌صورت پیش‌فرض غیرفعال است. فعال‌سازی آن به <code dir="ltr">SHAHBAZ_ACTUATOR_BACKEND=espidf</code>، <code dir="ltr">CONFIG_SHAHBAZ_ACTUATORS_ENABLE=y</code>، پرچم بررسی فیزیکی، رکورد مدرک مجاز، <code dir="ltr">GPIO</code>های یکتا و معتبر و موفقیت اعتبارسنجی زمان اجرا نیاز دارد.

در حالت مسلح، دست‌کم یک فرمان پذیرفته‌شدهٔ موتور، <code dir="ltr">Servo</code> یا <code dir="ltr">Actuator</code> عمومی باید <code dir="ltr">Control-command Watchdog</code> مستقل را حداکثر هر <code dir="ltr">250 ms</code> تازه کند. <code dir="ltr">Heartbeat</code> و <code dir="ltr">SetControlMode</code> این زمان را تازه نمی‌کنند. پایان مهلت فوراً خروجی امن را اعمال و دلیل ایمنی مربوط را قفل می‌کند.

## پیام‌های اصلی

- <code dir="ltr">DeviceInfoRequest / DeviceInfoResponse</code>
- <code dir="ltr">DeviceStatusRequest / DeviceStatusResponse</code>
- <code dir="ltr">TimeSyncRequest / TimeSyncResponse</code>
- <code dir="ltr">Heartbeat / HeartbeatAck</code>
- <code dir="ltr">StartTelemetry / StopTelemetry</code>
- <code dir="ltr">SetSensorRate</code>
- <code dir="ltr">Ping / Pong</code>
- <code dir="ltr">CommandAck / CommandNack</code>
- فرمان‌های <code dir="ltr">Actuator/Control</code> که در پیکربندی رومیزی فعلی به‌صورت پیش‌فرض غیرفعال‌اند

## <code dir="ltr">SensorSample</code>

شناسه‌های فعلی:

```text
sensor 1 = SHT30
sensor 2 = MS5611
instance = 0
```

قالب <code dir="ltr">SensorSample</code> با رفتن به <code dir="ltr">Protocol v2</code> تغییر نکرده است:

```text
u8  sensor_id
u8  instance_id
u32 sample_sequence
u64 monotonic_timestamp_us
u32 validity_flags
u32 quality_flags
u32 health_flags
u8  field_count
repeated field_count times: u8 field_id, u8 field_type, u32 raw_value
```

<code dir="ltr">SHT30</code> دمای محیط و رطوبت نسبی را گزارش می‌کند. <code dir="ltr">MS5611</code> فشار و دمای داخلی را گزارش می‌کند. در مسیر عملیاتی، **نرم‌افزار شهباز در <code dir="ltr">Android</code>** مالک <code dir="ltr">QNH</code> است و ارتفاع را از فشار محاسبه می‌کند. <code dir="ltr">Windows HIL</code> فقط برای عیب‌یابی مستقل می‌تواند همین محاسبه را تکرار کند.

## رفتار الزامی <code dir="ltr">Android/Shahbaz Client</code>

- بعد از هر اتصال فیزیکی مجدد، پیش از فرمان‌های وابسته به <code dir="ltr">Session</code> یک <code dir="ltr">TimeSyncRequest</code> تازه انجام شود.
- <code dir="ltr">Session Token</code> اتصال قبلی هرگز دوباره استفاده نشود.
- <code dir="ltr">Sender Timestamp</code>ها یکنواخت و جاری باشند.
- در <code dir="ltr">Session</code>های طولانی، <code dir="ltr">TimeSync</code> دوره‌ای انجام شود تا <code dir="ltr">Clock Drift</code> با فاصله مطمئن داخل پنجره تازگی بماند.
- <code dir="ltr">SessionMismatch</code>، <code dir="ltr">StaleOrExpired</code>، خطای <code dir="ltr">CRC</code> و خطای شماره توالی باید رد قطعی فرمان تلقی شوند، نه مجوز ارسال دوباره داده قدیمی.
- در اتصال مجدد، وضعیت فرمان/<code dir="ltr">Session</code> سمت <code dir="ltr">Client</code> نیز مانند دستگاه پاک شود.

هر تغییر بعدی <code dir="ltr">Protocol</code> باید هم‌زمان تست‌های <code dir="ltr">C++</code>، تست‌های <code dir="ltr">Kotlin Protocol/Session</code>، کد یکپارچه‌سازی <code dir="ltr">Android</code>، <code dir="ltr">Python HIL/self-test</code> و این سند را به‌روزرسانی کند. <code dir="ltr">tools/validate_firmware_contract.py</code> بررسی می‌کند که نسخه‌های <code dir="ltr">C++</code>، <code dir="ltr">Kotlin</code> و <code dir="ltr">Python</code> بی‌صدا از هم جدا نشوند.

**مرحله بعد برای توسعه‌دهنده:** <code dir="ltr">13_DEVELOPER_EXTENSION_GUIDE.fa.md</code>.
