# 11 — معماری نرم‌افزار

**فارسی** | [نسخه انگلیسی](../English/11_SOFTWARE_ARCHITECTURE.en.md)

این سند ساختار سخت‌سازی‌شده زمان اجرا و مرز یکپارچه‌سازی عملیاتی <code dir="ltr">Android</code> را توضیح می‌دهد. کاربر عادی پیش از تکمیل اسناد 00 تا 10 به آن نیاز ندارد.

## مسیر عملیاتی محصول

```text
validated ESP-IDF I2C adapter
  -> SHT30 / MS5611 drivers
  -> sensor scheduler
  -> normalized SensorSample
  -> telemetry serializer
  -> protocol-v2 frame
  -> native USB CDC transport
  -> Android UsbManager / CDC bulk transport
  -> ShahbazLinkSession
  -> Shahbaz Android application data model/UI
```

<code dir="ltr">Windows HIL</code> یک <code dir="ltr">Client</code> عیب‌یابی جداگانه روی همان <code dir="ltr">Protocol</code> است و جزو مسیر عملیاتی محصول نیست.

<code dir="ltr">I2C Adapter</code> سیاست <code dir="ltr">GPIO</code> برد را هم در <code dir="ltr">initialize()</code> و هم در <code dir="ltr">recover()</code> <code dir="ltr">enforce</code> می‌کند. <code dir="ltr">app_main</code> نیز در صورت <code dir="ltr">Pin</code> نامعتبر اصلاً <code dir="ltr">I2C</code> را <code dir="ltr">Initialize</code> نمی‌کند؛ بنابراین <code dir="ltr">Bus Recovery</code> نمی‌تواند راه دورزدن سیاست <code dir="ltr">Pin</code> باشد.

## مسیر فرمان و <code dir="ltr">Session</code>

```text
USB CDC RX
  -> physical-mount / CDC-DTR epoch detection
  -> logical-session RX/TX flush
  -> COBS/CRC frame decoder
  -> ProtocolEngine session-token check
  -> sender monotonic-time freshness mapping
  -> CommandDispatcher sequence/schema/state checks
  -> SafetySupervisor
  -> requested subsystem
```

هر بار بازشدن منطقی <code dir="ltr">CDC</code> یک <code dir="ltr">Session Token</code> تصادفی 64 بیتی تازه می‌گیرد. <code dir="ltr">TinyUSB mount</code> پیکربندی فیزیکی را دنبال می‌کند، درحالی‌که <code dir="ltr">mounted && DTR</code> اتصال نرم‌افزار را تعریف می‌کند. پرچم‌ها و <code dir="ltr">connection epoch</code> در یک نمای اتمیک منسجم منتشر می‌شوند تا حتی جفت بستن/بازکردن میان دو دور حلقه اصلی همه وضعیت‌های <code dir="ltr">Session</code> را پاک کند. پذیرش <code dir="ltr">Session</code> تا تکمیل پاک‌سازی <code dir="ltr">FIFO</code> در سمت <code dir="ltr">callback</code> صبر می‌کند و نما را پس از پاک‌سازی و ساخت <code dir="ltr">Token</code> دوباره بررسی می‌کند. مسیر <code dir="ltr">RX</code> به‌جای بازنشانی هم‌زمان یک جریان بدون برچسب، از بخش‌های ثابت صف <code dir="ltr">FreeRTOS</code> دارای <code dir="ltr">epoch</code> استفاده می‌کند و پس از خواندن و بلافاصله پیش از تحویل به <code dir="ltr">Protocol</code> دوباره بررسی می‌شود. آیتم‌های <code dir="ltr">TX</code> نیز برچسب <code dir="ltr">epoch</code> دارند و یک محافظ نوشتن بدون انتظار به‌همراه پاک‌سازی <code dir="ltr">FIFO</code> در <code dir="ltr">callback</code> و پس از نوشتن، هر دو ترتیب رقابت را پوشش می‌دهد. فرمان وابسته به <code dir="ltr">Session</code> منطقی قبلی پیش از <code dir="ltr">Dispatch</code> رد می‌شود. <code dir="ltr">TimeSync</code> ساعت یکنواخت فرستنده را به زمان دریافت دستگاه نگاشت می‌کند و <code dir="ltr">Frame</code> قدیمی پیش از آن‌که <code dir="ltr">heartbeat/control freshness</code> عادی را تازه کند رد می‌شود.

## ترتیب امن راه‌اندازی

```text
runtime board/memory/GPIO/evidence validation
  -> SafetySupervisor starts in safe state
  -> latch fatal profile mismatch if present
  -> initialize physical actuator PWM only when validation/evidence passed
  -> initialize I2C only when pin policy passed
  -> initialize USB transport
  -> construct protocol engine with runtime DeviceInfo facts
  -> subscribe app_main to Task Watchdog
  -> enter service loop
```

ساخت شیء <code dir="ltr">Actuator Controller</code> دیگر <code dir="ltr">LEDC</code> را لمس نمی‌کند. <code dir="ltr">forceSafe()</code> پیش از موفقیت <code dir="ltr">Initialize</code> فقط وضعیت نرم‌افزاری را امن می‌کند؛ بنابراین اعتبارسنجی برد پیش از تنظیم هر خروجی فیزیکی <code dir="ltr">PWM</code> انجام می‌شود.

## سلامت زمان اجرا

معماری فعلی یک <code dir="ltr">Main Service Task</code> دارد. <code dir="ltr">TaskHealthMonitor</code> نقاط <code dir="ltr">Liveness</code> برای ایمنی، <code dir="ltr">USB RX/TX</code>، فرمان، <code dir="ltr">Sensor</code>، <code dir="ltr">Telemetry</code> و نگهداری را ثبت می‌کند. ناسالم شدن رکورد بحرانی خطای ایمنی را قفل می‌کند.

آشکارساز مستقل گیرکردن کل حلقه، <code dir="ltr">ESP-IDF Task Watchdog</code> است. <code dir="ltr">app_main</code> عضو آن می‌شود و فقط بعد از تکمیل یک دور سرویس آن را <code dir="ltr">Feed</code> می‌کند. تنظیم پروژه <code dir="ltr">Timeout</code> دو ثانیه با <code dir="ltr">panic/reset</code> است. برای طراحی پروازی بحرانی همچنان یک مسیر سخت‌افزاری مستقل <code dir="ltr">output-enable/kill</code> توصیه می‌شود.

## <code dir="ltr">DeviceInfo</code> واقعی

<code dir="ltr">ProtocolEngine</code> یک <code dir="ltr">DeviceRuntimeInfo</code> را از نتیجه واقعی اعتبارسنجی برد و وضعیت <code dir="ltr">Initialize</code> شدن <code dir="ltr">Actuator</code> دریافت می‌کند. بنابراین <code dir="ltr">DeviceInfoResponse</code> به‌جای قابلیت فعال <code dir="ltr">hard-code</code>، <code dir="ltr">Flash/PSRAM</code> تشخیص‌داده‌شده، ماسک خطاهای برد، کانال‌های پشتیبانی‌شده/فعال، دسترس‌پذیری <code dir="ltr">Actuator</code> و وضعیت تنظیم آن را گزارش می‌کند.

## اصول معماری

- مالکیت و مجوز <code dir="ltr">Pin</code> <code dir="ltr">I2C</code> مرکزی و <code dir="ltr">fail-closed</code> است.
- <code dir="ltr">Sensor Driver</code>ها از <code dir="ltr">State Machine</code> و <code dir="ltr">Timeout</code> محدود استفاده می‌کنند.
- خرابی یک <code dir="ltr">Sensor</code> نباید <code dir="ltr">USB/Heartbeat</code> یا <code dir="ltr">Sensor</code> دیگر را متوقف کند.
- <code dir="ltr">Buffer</code> و صف‌ها محدود هستند.
- <code dir="ltr">Parser</code> بعد از <code dir="ltr">Frame</code> خراب <code dir="ltr">resync</code> می‌شود.
- اتصال فیزیکی مجدد یا بستن/بازکردن <code dir="ltr">CDC DTR</code> یک مرز سخت <code dir="ltr">Protocol Session</code> است.
- <code dir="ltr">Timestamp</code> قدیمی یا <code dir="ltr">Token</code> اتصال قبلی نمی‌تواند سلامت لینک/کنترل را تازه کند.
- <code dir="ltr">Actuator</code> از مسیر <code dir="ltr">Sensor</code> جدا و فقط پس از <code dir="ltr">Validation/Evidence Gate</code> <code dir="ltr">Initialize</code> می‌شود.
- زمان اجرای پایدار نباید به تخصیص نامحدود حافظه وابسته باشد.
- <code dir="ltr">tools/validate_firmware_contract.py</code> ثابت‌های <code dir="ltr">Firmware/Hardware</code> و ادعاهای <code dir="ltr">Evidence</code> را بررسی می‌کند.

## بخش‌های مهم مخزن

```text
main/                       composition and app_main
components/                 firmware implementations
test/host/                  board-independent C++ tests
tools/                      build/test/HIL/contract validators
android_reference/src/main/ framework-independent Kotlin protocol/session core
android_reference/src/android/ Android UsbManager CDC transport + Shahbaz app adapter
```

برای قالب دقیق پیام و <code dir="ltr">Session v2</code> به <code dir="ltr">12_USB_PROTOCOL.fa.md</code> بروید.
