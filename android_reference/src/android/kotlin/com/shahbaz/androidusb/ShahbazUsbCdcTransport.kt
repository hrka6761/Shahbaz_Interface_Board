package com.shahbaz.androidusb

import android.content.Context
import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager
import java.io.Closeable
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Android-framework USB CDC transport for shahbaz_interface_board.
 *
 * The Shahbaz application owns permission UX/lifecycle. After UsbManager permission is
 * granted, call [open]. The transport claims the CDC interfaces, receives bulk data on
 * a dedicated reader thread, and closes/clears the physical session on detach.
 */
class ShahbazUsbCdcTransport(
    context: Context,
    private val listener: Listener,
    private val readTimeoutMs: Int = 100,
    private val writeTimeoutMs: Int = 1000,
) : Closeable {
    interface Listener {
        fun onUsbBytes(bytes: ByteArray)
        fun onUsbDisconnected()
        fun onUsbError(message: String, cause: Throwable? = null)
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val running = AtomicBoolean(false)
    private val writeLock = Any()

    @Volatile private var openedDevice: UsbDevice? = null
    @Volatile private var connection: UsbDeviceConnection? = null
    @Volatile private var communicationInterface: UsbInterface? = null
    @Volatile private var dataInterface: UsbInterface? = null
    @Volatile private var inEndpoint: UsbEndpoint? = null
    @Volatile private var outEndpoint: UsbEndpoint? = null
    @Volatile private var readerThread: Thread? = null

    fun matchingDevices(): List<UsbDevice> =
        usbManager.deviceList.values.filter(::isCdcAcmDevice)

    fun hasPermission(device: UsbDevice): Boolean = usbManager.hasPermission(device)

    /**
     * Open only after the Shahbaz application has obtained UsbManager permission.
     * Returns false rather than silently falling back to any non-CDC interface.
     */
    fun open(device: UsbDevice): Boolean {
        close()
        if (!usbManager.hasPermission(device)) {
            listener.onUsbError("USB permission has not been granted for shahbaz_interface_board")
            return false
        }

        val endpoints = findCdcEndpoints(device)
        if (endpoints == null) {
            listener.onUsbError("USB device does not expose a usable CDC-ACM bulk interface")
            return false
        }

        val opened = usbManager.openDevice(device)
        if (opened == null) {
            listener.onUsbError("UsbManager.openDevice returned null")
            return false
        }

        val (comm, data, input, output) = endpoints
        try {
            if (comm != null && !opened.claimInterface(comm, true)) {
                listener.onUsbError("failed to claim CDC communication interface")
                opened.close()
                return false
            }
            if (!opened.claimInterface(data, true)) {
                if (comm != null) opened.releaseInterface(comm)
                listener.onUsbError("failed to claim CDC data interface")
                opened.close()
                return false
            }

            openedDevice = device
            connection = opened
            communicationInterface = comm
            dataInterface = data
            inEndpoint = input
            outEndpoint = output
            configureCdcAcm(opened, comm)
            startReader()
            return true
        } catch (error: RuntimeException) {
            try { opened.close() } catch (_: RuntimeException) { }
            clearHandles()
            listener.onUsbError("failed to initialize Shahbaz USB CDC transport", error)
            return false
        }
    }

    fun write(bytes: ByteArray): Boolean {
        if (bytes.isEmpty()) return true
        val currentConnection = connection ?: return false
        val endpoint = outEndpoint ?: return false
        synchronized(writeLock) {
            val transferred = currentConnection.bulkTransfer(endpoint, bytes, bytes.size, writeTimeoutMs)
            if (transferred != bytes.size) {
                listener.onUsbError("USB write incomplete: $transferred/${bytes.size} bytes")
                return false
            }
        }
        return true
    }

    /** Call from the app's ACTION_USB_DEVICE_DETACHED handling path. */
    fun handleDetached(device: UsbDevice) {
        val current = openedDevice
        if (current != null && current.deviceId == device.deviceId) {
            close()
            listener.onUsbDisconnected()
        }
    }

    override fun close() {
        running.set(false)
        val thread = readerThread
        if (thread != null && thread !== Thread.currentThread()) {
            try { thread.join(readTimeoutMs.toLong() + 150L) } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }
        val currentConnection = connection
        val data = dataInterface
        val comm = communicationInterface
        if (currentConnection != null) {
            try { if (data != null) currentConnection.releaseInterface(data) } catch (_: RuntimeException) { }
            try { if (comm != null) currentConnection.releaseInterface(comm) } catch (_: RuntimeException) { }
            try { currentConnection.close() } catch (_: RuntimeException) { }
        }
        clearHandles()
    }

    private fun startReader() {
        running.set(true)
        val thread = Thread({
            val buffer = ByteArray(2048)
            while (running.get()) {
                val currentConnection = connection ?: break
                val endpoint = inEndpoint ?: break
                val count = try {
                    currentConnection.bulkTransfer(endpoint, buffer, buffer.size, readTimeoutMs)
                } catch (error: RuntimeException) {
                    if (running.get()) listener.onUsbError("USB read failed", error)
                    break
                }
                if (count > 0) listener.onUsbBytes(buffer.copyOf(count))
                else if (count < 0 && !running.get()) break
            }
        }, "shahbaz-usb-cdc-rx")
        thread.isDaemon = true
        readerThread = thread
        thread.start()
    }

    private fun configureCdcAcm(connection: UsbDeviceConnection, comm: UsbInterface?) {
        if (comm == null) return
        // CDC SET_LINE_CODING: 115200, 1 stop bit, no parity, 8 data bits.
        val lineCoding = byteArrayOf(
            0x00, 0xC2.toByte(), 0x01, 0x00,
            0x00, 0x00, 0x08,
        )
        connection.controlTransfer(0x21, 0x20, 0, comm.id, lineCoding, lineCoding.size, 1000)
        // CDC SET_CONTROL_LINE_STATE: DTR + RTS. TinyUSB does not use baud rate for framing.
        connection.controlTransfer(0x21, 0x22, 0x0003, comm.id, null, 0, 1000)
    }

    private fun clearHandles() {
        openedDevice = null
        connection = null
        communicationInterface = null
        dataInterface = null
        inEndpoint = null
        outEndpoint = null
        readerThread = null
    }

    private fun isCdcAcmDevice(device: UsbDevice): Boolean = findCdcEndpoints(device) != null

    private data class Endpoints(
        val communication: UsbInterface?,
        val data: UsbInterface,
        val input: UsbEndpoint,
        val output: UsbEndpoint,
    )

    private fun findCdcEndpoints(device: UsbDevice): Endpoints? {
        var communication: UsbInterface? = null
        var data: UsbInterface? = null
        for (index in 0 until device.interfaceCount) {
            val intf = device.getInterface(index)
            when (intf.interfaceClass) {
                UsbConstants.USB_CLASS_COMM -> if (communication == null) communication = intf
                UsbConstants.USB_CLASS_CDC_DATA -> if (data == null) data = intf
            }
        }
        val selected = data ?: return null
        var input: UsbEndpoint? = null
        var output: UsbEndpoint? = null
        for (index in 0 until selected.endpointCount) {
            val endpoint = selected.getEndpoint(index)
            if (endpoint.type != UsbConstants.USB_ENDPOINT_XFER_BULK) continue
            when (endpoint.direction) {
                UsbConstants.USB_DIR_IN -> input = endpoint
                UsbConstants.USB_DIR_OUT -> output = endpoint
            }
        }
        if (input == null || output == null) return null
        return Endpoints(communication, selected, input, output)
    }
}
