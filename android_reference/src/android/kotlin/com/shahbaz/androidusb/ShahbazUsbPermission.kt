package com.shahbaz.androidusb

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager

/** Permission helper intentionally separated from the transport so the Shahbaz app owns UX/lifecycle. */
object ShahbazUsbPermission {
    fun action(context: Context): String = "${context.packageName}.SHAHBAZ_USB_PERMISSION"

    fun request(context: Context, device: UsbDevice) {
        val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        val intent = Intent(action(context)).setPackage(context.packageName)
        val pending = PendingIntent.getBroadcast(
            context,
            device.deviceId,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        manager.requestPermission(device, pending)
    }

    /** Returns the granted device for the permission broadcast, otherwise null. */
    fun grantedDevice(context: Context, intent: Intent): UsbDevice? {
        if (intent.action != action(context)) return null
        val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
        if (!granted) return null
        @Suppress("DEPRECATION")
        return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE) as? UsbDevice
    }
}
