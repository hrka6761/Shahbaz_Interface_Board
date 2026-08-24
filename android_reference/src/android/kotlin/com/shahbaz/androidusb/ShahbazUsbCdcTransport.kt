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
    companion object {
        const val SHAHBAZ_VENDOR_ID: Int = 0x303A
        const val SHAHBAZ_PRODUCT_ID: Int = 0x4001

        private const val CDC_REQUEST_TYPE_OUT: Int = 0x21
        private const val CDC_SET_LINE_CODING: Int = 0x20
        private const val CDC_SET_CONTROL_LINE_STATE: Int = 0x22
        // Firmware treats DTR as the logical Protocol v2 session signal. RTS has no
        // Shahbaz meaning, so leave it deasserted instead of driving an unrelated line.
        private const val CDC_DTR: Int = 0x0001
    }

    interface Listener {
        fun onUsbBytes(generation: Long, bytes: ByteArray)
        fun onUsbDisconnected(generation: Long)
        fun onUsbError(generation: Long, message: String, cause: Throwable? = null)
    }

    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private val running = AtomicBoolean(false)
    private val writeLock = Any()

    @Volatile private var openedDevice: UsbDevice? = null
    @Volatile private var openedGeneration: Long? = null
    @Volatile private var connection: UsbDeviceConnection? = null
    @Volatile private var communicationInterface: UsbInterface? = null
    @Volatile private var dataInterface: UsbInterface? = null
    @Volatile private var inEndpoint: UsbEndpoint? = null
    @Volatile private var outEndpoint: UsbEndpoint? = null
    @Volatile private var readerThread: Thread? = null

    fun matchingDevices(): List<UsbDevice> =
        usbManager.deviceList.values.filter(::isShahbazCdcAcmDevice)

    fun hasPermission(device: UsbDevice): Boolean = usbManager.hasPermission(device)

    /**
     * Open only after the Shahbaz application has obtained UsbManager permission.
     * Returns false rather than silently falling back to any non-CDC interface.
     */
    fun open(device: UsbDevice, generation: Long): Boolean {
        require(generation > 0) { "USB session generation must be positive" }
        close()
        if (!isShahbazCdcAcmDevice(device)) {
            listener.onUsbError(
                generation,
                "USB device is not Shahbaz 303A:4001 with a usable CDC-ACM interface",
            )
            return false
        }
        if (!usbManager.hasPermission(device)) {
            listener.onUsbError(
                generation,
                "USB permission has not been granted for shahbaz_interface_board",
            )
            return false
        }

        val endpoints = findCdcEndpoints(device)
        if (endpoints == null) {
            listener.onUsbError(
                generation,
                "USB device does not expose a usable CDC-ACM bulk interface",
            )
            return false
        }

        val opened = usbManager.openDevice(device)
        if (opened == null) {
            listener.onUsbError(generation, "UsbManager.openDevice returned null")
            return false
        }

        val (comm, data, input, output) = endpoints
        try {
            if (!opened.claimInterface(comm, true)) {
                listener.onUsbError(generation, "failed to claim CDC communication interface")
                opened.close()
                return false
            }
            if (!opened.claimInterface(data, true)) {
                opened.releaseInterface(comm)
                listener.onUsbError(generation, "failed to claim CDC data interface")
                opened.close()
                return false
            }

            openedDevice = device
            openedGeneration = generation
            connection = opened
            communicationInterface = comm
            dataInterface = data
            inEndpoint = input
            outEndpoint = output
            configureCdcAcm(opened, comm)
            startReader(generation)
            return true
        } catch (error: RuntimeException) {
            // Handles are already published before CDC configuration starts. Use the normal
            // close path so a failure after DTR assertion still emits the required state-0 edge.
            close()
            listener.onUsbError(
                generation,
                "failed to initialize Shahbaz USB CDC transport",
                error,
            )
            return false
        }
    }

    fun write(generation: Long, bytes: ByteArray): Boolean {
        if (bytes.isEmpty()) return true
        synchronized(writeLock) {
            if (openedGeneration != generation) return false
            val currentConnection = connection ?: return false
            val endpoint = outEndpoint ?: return false
            var offset = 0
            while (offset < bytes.size) {
                val transferred = currentConnection.bulkTransfer(
                    endpoint,
                    bytes,
                    offset,
                    bytes.size - offset,
                    writeTimeoutMs,
                )
                if (transferred <= 0) {
                    listener.onUsbError(
                        generation,
                        "USB write stopped after $offset/${bytes.size} bytes (result $transferred)",
                    )
                    return false
                }
                offset += transferred
            }
        }
        return true
    }

    /** Call from the app's ACTION_USB_DEVICE_DETACHED handling path. */
    fun handleDetached(device: UsbDevice) {
        val current = openedDevice
        if (current != null && current.deviceId == device.deviceId) {
            val generation = openedGeneration ?: return
            close()
            listener.onUsbDisconnected(generation)
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
        synchronized(writeLock) {
            val currentConnection = connection
            val data = dataInterface
            val comm = communicationInterface
            if (currentConnection != null) {
                // Make a close/reopen an explicit CDC logical-session edge before releasing USB.
                try {
                    if (comm != null) setControlLineState(currentConnection, comm, 0)
                } catch (_: RuntimeException) { }
                try { if (data != null) currentConnection.releaseInterface(data) } catch (_: RuntimeException) { }
                try { if (comm != null) currentConnection.releaseInterface(comm) } catch (_: RuntimeException) { }
                try { currentConnection.close() } catch (_: RuntimeException) { }
            }
            clearHandles()
        }
    }

    private fun startReader(generation: Long) {
        running.set(true)
        val thread = Thread({
            val buffer = ByteArray(2048)
            while (running.get() && openedGeneration == generation) {
                val currentConnection = connection ?: break
                val endpoint = inEndpoint ?: break
                val count = try {
                    currentConnection.bulkTransfer(endpoint, buffer, buffer.size, readTimeoutMs)
                } catch (error: RuntimeException) {
                    if (running.get() && openedGeneration == generation) {
                        listener.onUsbError(generation, "USB read failed", error)
                    }
                    break
                }
                if (count > 0 && openedGeneration == generation) {
                    listener.onUsbBytes(generation, buffer.copyOf(count))
                }
                else if (count < 0 && !running.get()) break
            }
        }, "shahbaz-usb-cdc-rx")
        thread.isDaemon = true
        readerThread = thread
        thread.start()
    }

    private fun configureCdcAcm(connection: UsbDeviceConnection, comm: UsbInterface) {
        // Clear the CDC control lines first so every open has a well-defined DTR edge.
        setControlLineState(connection, comm, 0)
        // CDC SET_LINE_CODING: 115200, 1 stop bit, no parity, 8 data bits.
        val lineCoding = byteArrayOf(
            0x00, 0xC2.toByte(), 0x01, 0x00,
            0x00, 0x00, 0x08,
        )
        val lineCodingResult = connection.controlTransfer(
            CDC_REQUEST_TYPE_OUT,
            CDC_SET_LINE_CODING,
            0,
            comm.id,
            lineCoding,
            lineCoding.size,
            writeTimeoutMs,
        )
        if (lineCodingResult != lineCoding.size) {
            throw IllegalStateException("CDC SET_LINE_CODING failed: $lineCodingResult")
        }
        // CDC SET_CONTROL_LINE_STATE: DTR only. Firmware intentionally ignores RTS.
        setControlLineState(connection, comm, CDC_DTR)
    }

    private fun setControlLineState(
        connection: UsbDeviceConnection,
        communication: UsbInterface,
        state: Int,
    ) {
        val result = connection.controlTransfer(
            CDC_REQUEST_TYPE_OUT,
            CDC_SET_CONTROL_LINE_STATE,
            state,
            communication.id,
            null,
            0,
            writeTimeoutMs,
        )
        if (result != 0) {
            throw IllegalStateException(
                "CDC SET_CONTROL_LINE_STATE 0x${state.toString(16)} failed: $result",
            )
        }
    }

    private fun clearHandles() {
        openedDevice = null
        openedGeneration = null
        connection = null
        communicationInterface = null
        dataInterface = null
        inEndpoint = null
        outEndpoint = null
        readerThread = null
    }

    private fun isShahbazCdcAcmDevice(device: UsbDevice): Boolean =
        device.vendorId == SHAHBAZ_VENDOR_ID &&
            device.productId == SHAHBAZ_PRODUCT_ID &&
            findCdcEndpoints(device) != null

    private data class Endpoints(
        val communication: UsbInterface,
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
        val control = communication ?: return null
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
        return Endpoints(control, selected, input, output)
    }
}
