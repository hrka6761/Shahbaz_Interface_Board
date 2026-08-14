# مرجع <code dir="ltr">GY-63 MS5611 I2C Module</code>

**فارسی** | [نسخه انگلیسی](README.en.md)

پروژه از یک <code dir="ltr">GY-63 breakout</code> <code dir="ltr">Generic</code> استفاده می‌کند که انتظار می‌رود <code dir="ltr">MS5611-01BA03</code> روی آن قرار داشته باشد. سازنده خود <code dir="ltr">breakout</code> از روی عکس پروژه قابل شناسایی نیست؛ بنابراین اطلاعات سطح برد از مستندات رسمی <code dir="ltr">TE Connectivity MS5611</code> جدا نگه داشته شده است.

## محتوا

- <code dir="ltr">01_official_te_connectivity_chip_docs/</code> — صفحه رسمی محصول و <code dir="ltr">Datasheet</code> رسمی <code dir="ltr">MS5611</code> از <code dir="ltr">TE Connectivity</code>.
- <code dir="ltr">02_actual_project_module_photos/</code> — عکس همان <code dir="ltr">GY-63 Module</code> واقعی پروژه.

## تنظیم <code dir="ltr">I2C</code> در پروژه شهباز

در عکس پروژه <code dir="ltr">Pin</code>های <code dir="ltr">VCC</code>، <code dir="ltr">GND</code>، <code dir="ltr">SCL</code>، <code dir="ltr">SDA</code>، <code dir="ltr">CSB</code>، <code dir="ltr">SDO</code> و <code dir="ltr">PS</code> دیده می‌شوند. تنظیم پروژه به این صورت است:

```text
VCC -> 3.3 V
GND -> GND
SCL -> GPIO9
SDA -> GPIO8
PS  -> 3.3 V (HIGH, selects I2C)
CSB -> GND   (LOW, selects I2C address 0x77)
SDO -> NC    (unused in I2C mode)
```

طبق مستند رسمی <code dir="ltr">TE Connectivity</code>، حالت <code dir="ltr">PS=High</code> رابط <code dir="ltr">I2C</code> را انتخاب می‌کند. در این حالت <code dir="ltr">CSB</code> نباید شناور باشد و بیت آدرس را تعیین می‌کند. شهباز از <code dir="ltr">CSB=Low</code> و آدرس <code dir="ltr">0x77</code> استفاده می‌کند.

صرفاً به‌دلیل ادعای بعضی صفحات فروشگاهی، سازگاری خود <code dir="ltr">GY-63 breakout</code> با <code dir="ltr">5 V</code> فرض نمی‌شود. وضعیت <code dir="ltr">regulator</code>، مقاومت‌های <code dir="ltr">pull-up</code> و <code dir="ltr">level shifting</code> روی <code dir="ltr">Module</code> واقعی هنوز از طرف سازنده آن برد تأیید نشده است؛ انتخاب پروژه برای تغذیه، <code dir="ltr">3.3 V</code> است.
