# مرجع برد توسعه <code dir="ltr">ESP32-S3 N16R8</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

این پوشه برد توسعه مورد استفاده پروژه را مستند می‌کند. نزدیک‌ترین مرجع رسمی که با اطلاعات فعلی پروژه تطابق دارد **<code dir="ltr">VCC-GND Studio YD-ESP32-S3</code>** مجهز به **<code dir="ltr">ESP32-S3-WROOM-1-N16R8</code>** است. جزئیات سطح برد <code dir="ltr">VCC-GND</code> تا زمانی که برد فیزیکی خود پروژه با عکس و شماتیک رسمی تطبیق داده نشود، در وضعیت مرجع کاندید باقی می‌ماند.

## محتوای پوشه

- <code dir="ltr">01_official_vcc_gnd_board_docs/</code> — مخزن رسمی <code dir="ltr">VCC-GND Studio</code>، لینک شماتیک، تصاویر رسمی برد و منابع رسمی <code dir="ltr">WCH CH343</code> برای <code dir="ltr">USB-to-UART</code> جداگانه.
- <code dir="ltr">02_official_espressif_esp32_s3_docs/</code> — <code dir="ltr">Datasheet</code>های رسمی <code dir="ltr">ESP32-S3-WROOM-1</code> و <code dir="ltr">ESP32-S3 SoC</code> و مستندات رسمی <code dir="ltr">native USB</code>.
- <code dir="ltr">03_actual_project_board_photos/</code> — عکس <code dir="ltr">front/back</code> برد دقیقاً همان نمونه‌ای که در پروژه استفاده می‌شود؛ این عکس‌ها برای تأیید فیزیکی <code dir="ltr">board revision</code> لازم هستند.

## نکات مهم برای پروژه

- طراحی رسمی <code dir="ltr">YD-ESP32-S3</code> دو <code dir="ltr">Type-C Connector</code> دارد: یکی مستقیم به <code dir="ltr">native USB</code> خود <code dir="ltr">ESP32-S3</code> متصل است و دیگری از <code dir="ltr">WCH CH343P USB-to-UART bridge</code> عبور می‌کند.
- در <code dir="ltr">native USB</code>، سیگنال <code dir="ltr">D-</code> روی <code dir="ltr">GPIO19</code> و <code dir="ltr">D+</code> روی <code dir="ltr">GPIO20</code> است.
- در مرجع رسمی برد <code dir="ltr">YD</code>، <code dir="ltr">WS2812 RGB LED</code> روی <code dir="ltr">GPIO48</code> و <code dir="ltr">UART0</code> روی <code dir="ltr">GPIO43/GPIO44</code> قرار دارد.
- برای گونه‌های دارای <code dir="ltr">8-line flash/PSRAM</code>، مستند رسمی <code dir="ltr">YD</code> اعلام می‌کند که <code dir="ltr">GPIO35</code>، <code dir="ltr">GPIO36</code> و <code dir="ltr">GPIO37</code> در داخل استفاده می‌شوند و برای استفاده خارجی در دسترس نیستند.
- <code dir="ltr">Espressif</code> گونه <code dir="ltr">ESP32-S3-WROOM-1-N16R8</code> را با <code dir="ltr">16 MB Quad SPI flash</code> و <code dir="ltr">8 MB Octal SPI PSRAM</code> تعریف می‌کند.

برای ارتباط <code dir="ltr">USB</code> شهباز از <code dir="ltr">native ESP32-S3 USB Type-C</code> استفاده کنید، نه <code dir="ltr">CH343P USB-to-UART Port</code>.
