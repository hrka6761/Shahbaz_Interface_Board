package com.shahbaz.protocol

/**
 * Stateful host-side session coordinator for the operational Shahbaz Android link.
 *
 * This class has no Android framework dependency so its session/reset/freshness-facing
 * behavior can be unit-tested on the host. One instance represents one physical USB
 * attachment. Call [onUsbAttached] and [onUsbDetached] at the real USB lifecycle edges.
 */
class ShahbazLinkSession(
    private val monotonicUs: () -> ULong,
    initialQnhHpa: Double = 1013.25,
) {
    companion object {
        const val TIME_SYNC_REFRESH_US: ULong = 30_000_000uL
        const val MAX_OUTSTANDING_TIME_SYNCS: Int = 4
    }

    sealed class Event {
        data class TimeSynchronized(val token: ULong) : Event()
        data class DeviceInfoValidated(val info: ValidatedDeviceInfo) : Event()
        object HeartbeatAcknowledged : Event()
        object StartTelemetryAcknowledged : Event()
        data class SessionReady(
            val token: ULong,
            val deviceInfo: ValidatedDeviceInfo,
        ) : Event()
        data class SessionRejected(val reason: String) : Event()
        data class FrameReceived(val frame: DecodedFrame) : Event()
        data class Sht30(val reading: Sht30Reading) : Event()
        data class Ms5611(
            val reading: Ms5611Reading,
            val barometricAltitudeMeters: Double,
        ) : Event()
        data class Vl53l0x(val reading: Vl53l0xReading) : Event()
        data class DeviceStatusReceived(val status: DeviceStatus) : Event()
        data class ProtocolRejected(val reason: String) : Event()
    }

    private enum class StartupStage {
        DETACHED,
        AWAITING_TIME_SYNC,
        TIME_SYNCHRONIZED,
        AWAITING_HEARTBEAT_ACK,
        HEARTBEAT_ACKNOWLEDGED,
        AWAITING_START_TELEMETRY_ACK,
        START_TELEMETRY_ACKNOWLEDGED,
        READY,
        FAILED,
    }

    private data class EncodedFrame(
        val sequence: UInt,
        val bytes: ByteArray,
    )

    private val accumulator = FrameAccumulator()
    private var nextSequence: UInt = 1u
    private val pendingTimeSyncClientUs = mutableListOf<ULong>()
    private var lastSuccessfulTimeSyncHostUs: ULong? = null
    private var startupStage: StartupStage = StartupStage.DETACHED
    private var pendingDeviceInfoSequence: UInt? = null
    private var pendingHeartbeatSequence: UInt? = null
    private var pendingStartTelemetrySequence: UInt? = null

    var connected: Boolean = false
        private set

    var sessionToken: ULong? = null
        private set

    var deviceInfo: ValidatedDeviceInfo? = null
        private set

    var qnhHpa: Double = initialQnhHpa
        set(value) {
            if (!value.isFinite() || value !in 800.0..1100.0) {
                throw ProtocolException("QNH must be in 800..1100 hPa")
            }
            field = value
        }

    init {
        qnhHpa = initialQnhHpa
    }

    fun onUsbAttached() {
        resetSessionState()
        connected = true
        startupStage = StartupStage.AWAITING_TIME_SYNC
    }

    fun onUsbDetached() {
        resetSessionState()
        connected = false
    }

    fun buildTimeSync(): ByteArray {
        requireConnected()
        requireStartupNotFailed()
        val now = monotonicUs()
        if (now !in pendingTimeSyncClientUs) {
            if (pendingTimeSyncClientUs.size == MAX_OUTSTANDING_TIME_SYNCS) {
                pendingTimeSyncClientUs.removeAt(0)
            }
            pendingTimeSyncClientUs += now
        }
        return encode(SafeRequests.timeSync(now), sessionBound = false)
    }

    fun buildDeviceInfoRequest(): ByteArray {
        requireConnected()
        requireStartupNotFailed()
        val encoded = encodeFrame(SafeRequests.deviceInfo(), sessionBound = false)
        if (
            startupStage == StartupStage.TIME_SYNCHRONIZED ||
            startupStage == StartupStage.AWAITING_HEARTBEAT_ACK
        ) {
            pendingDeviceInfoSequence = encoded.sequence
        }
        return encoded.bytes
    }

    fun buildDeviceStatusRequest(): ByteArray =
        encodeConnected(SafeRequests.deviceStatus(), sessionBound = false)

    fun buildHeartbeat(): ByteArray {
        requireConnected()
        requireStartupNotFailed()
        val encoded = encodeFrame(SafeRequests.heartbeat(), sessionBound = true)
        if (
            startupStage == StartupStage.TIME_SYNCHRONIZED ||
            startupStage == StartupStage.AWAITING_HEARTBEAT_ACK
        ) {
            pendingHeartbeatSequence = encoded.sequence
            startupStage = StartupStage.AWAITING_HEARTBEAT_ACK
        }
        return encoded.bytes
    }

    fun buildStartTelemetry(): ByteArray {
        requireConnected()
        requireStartupNotFailed()
        if (
            startupStage != StartupStage.HEARTBEAT_ACKNOWLEDGED &&
            startupStage != StartupStage.AWAITING_START_TELEMETRY_ACK &&
            startupStage != StartupStage.READY
        ) {
            throw OutboundPolicyException(
                "StartTelemetry requires the startup HeartbeatAck",
            )
        }
        val encoded = encodeFrame(SafeRequests.startTelemetry(), sessionBound = true)
        if (startupStage != StartupStage.READY) {
            pendingStartTelemetrySequence = encoded.sequence
            startupStage = StartupStage.AWAITING_START_TELEMETRY_ACK
        }
        return encoded.bytes
    }

    fun buildStopTelemetry(): ByteArray =
        encodeConnected(SafeRequests.stopTelemetry(), sessionBound = true)

    fun buildSetSensorRate(sensorId: Int, instanceId: Int, intervalUs: UInt): ByteArray =
        encodeConnected(
            SafeRequests.setSensorRate(sensorId, instanceId, intervalUs),
            sessionBound = true,
        )

    fun buildDisarm(): ByteArray =
        encodeConnected(SafeRequests.disarm(), sessionBound = false)

    fun buildEmergencyStop(): ByteArray =
        encodeConnected(SafeRequests.emergencyStop(), sessionBound = false)

    fun timeSyncRefreshDue(nowUs: ULong = monotonicUs()): Boolean {
        if (pendingTimeSyncClientUs.isNotEmpty()) return false
        val last = lastSuccessfulTimeSyncHostUs ?: return true
        return nowUs >= last && nowUs - last >= TIME_SYNC_REFRESH_US
    }

    fun feedUsbBytes(bytes: ByteArray): List<Event> {
        if (!connected) return emptyList()
        val output = mutableListOf<Event>()
        for (streamEvent in accumulator.feed(bytes)) {
            when (streamEvent.kind) {
                StreamEventKind.FRAME_READY -> {
                    val frame = streamEvent.frame ?: continue
                    handleFrame(frame, output)
                }
                StreamEventKind.FRAME_REJECTED,
                StreamEventKind.OVERSIZE_DISCARDED ->
                    output += Event.ProtocolRejected(streamEvent.error ?: "invalid USB frame")
                StreamEventKind.EMPTY_DELIMITER -> Unit
            }
        }
        return output
    }

    private fun handleFrame(frame: DecodedFrame, output: MutableList<Event>) {
        when (frame.header.messageType) {
            MessageType.TIME_SYNC_RESPONSE -> handleTimeSyncResponse(frame, output)
            MessageType.HEARTBEAT_ACK -> handleHeartbeatAck(frame, output)
            MessageType.COMMAND_ACK -> handleCommandAck(frame, output)
            MessageType.COMMAND_NACK -> handleCommandNack(frame, output)
            MessageType.DEVICE_INFO_RESPONSE -> handleDeviceInfoResponse(frame, output)
            MessageType.DEVICE_STATUS_RESPONSE -> handleDeviceStatusResponse(frame, output)
            MessageType.SENSOR_SAMPLE -> handleSensorSample(frame, output)
            else -> output += Event.FrameReceived(frame)
        }
    }

    private fun handleTimeSyncResponse(frame: DecodedFrame, output: MutableList<Event>) {
        val sync = decodeTimeSyncResponse(frame)
        if (
            sync == null ||
            sync.clientSendUs !in pendingTimeSyncClientUs ||
            sync.sessionToken == 0uL
        ) {
            output += Event.ProtocolRejected("invalid or unsolicited TimeSyncResponse")
            return
        }
        val establishedToken = sessionToken
        if (establishedToken != null && sync.sessionToken != establishedToken) {
            pendingTimeSyncClientUs.clear()
            rejectSession(output, "TimeSyncResponse changed the session token without USB detach")
            return
        }

        val firstSynchronization = establishedToken == null
        sessionToken = sync.sessionToken
        pendingTimeSyncClientUs.clear()
        lastSuccessfulTimeSyncHostUs = monotonicUs()
        if (firstSynchronization) {
            startupStage = StartupStage.TIME_SYNCHRONIZED
            output += Event.TimeSynchronized(sync.sessionToken)
        }
        output += Event.FrameReceived(frame)
    }

    private fun handleDeviceInfoResponse(frame: DecodedFrame, output: MutableList<Event>) {
        if (pendingDeviceInfoSequence == null && deviceInfo == null) {
            rejectSession(output, "unsolicited DeviceInfoResponse")
            return
        }
        val validated = try {
            val decoded = decodeDeviceInfoResponse(frame)
                ?: throw ProtocolException("frame is not DeviceInfoResponse")
            decoded.validateSensorOnlyProfile()
        } catch (error: ProtocolException) {
            rejectSession(output, error.message ?: "invalid DeviceInfoResponse")
            return
        }

        val existing = deviceInfo
        if (existing != null && existing != validated) {
            rejectSession(output, "DeviceInfo changed within one logical USB session")
            return
        }
        pendingDeviceInfoSequence = null
        if (existing == null) {
            deviceInfo = validated
            output += Event.DeviceInfoValidated(validated)
        }
        maybeEmitReady(output)
        output += Event.FrameReceived(frame)
    }

    private fun handleHeartbeatAck(frame: DecodedFrame, output: MutableList<Event>) {
        try {
            requireHeartbeatAck(frame)
        } catch (error: ProtocolException) {
            if (startupStage == StartupStage.AWAITING_HEARTBEAT_ACK) {
                rejectSession(output, error.message ?: "invalid startup HeartbeatAck")
            } else {
                output += Event.ProtocolRejected(error.message ?: "invalid HeartbeatAck")
            }
            return
        }
        if (startupStage == StartupStage.AWAITING_HEARTBEAT_ACK) {
            pendingHeartbeatSequence = null
            startupStage = StartupStage.HEARTBEAT_ACKNOWLEDGED
            output += Event.HeartbeatAcknowledged
        }
        output += Event.FrameReceived(frame)
    }

    private fun handleCommandAck(frame: DecodedFrame, output: MutableList<Event>) {
        val ack = try {
            decodeCommandAck(frame)
                ?: throw ProtocolException("frame is not CommandAck")
        } catch (error: ProtocolException) {
            if (startupStage == StartupStage.AWAITING_START_TELEMETRY_ACK) {
                rejectSession(output, error.message ?: "invalid startup CommandAck")
            } else {
                output += Event.ProtocolRejected(error.message ?: "invalid CommandAck")
            }
            return
        }

        if (startupStage == StartupStage.AWAITING_START_TELEMETRY_ACK) {
            val expected = pendingStartTelemetrySequence
            if (
                expected == null ||
                ack.requestSequence != expected ||
                ack.action != ApplicationAction.START_TELEMETRY
            ) {
                rejectSession(
                    output,
                    "CommandAck did not acknowledge the pending StartTelemetry request",
                )
                return
            }
            pendingStartTelemetrySequence = null
            startupStage = StartupStage.START_TELEMETRY_ACKNOWLEDGED
            output += Event.StartTelemetryAcknowledged
            maybeEmitReady(output)
        }
        output += Event.FrameReceived(frame)
    }

    private fun maybeEmitReady(output: MutableList<Event>) {
        val info = deviceInfo ?: return
        if (startupStage != StartupStage.START_TELEMETRY_ACKNOWLEDGED) return
        startupStage = StartupStage.READY
        output += Event.SessionReady(requireSessionToken(), info)
    }

    private fun handleCommandNack(frame: DecodedFrame, output: MutableList<Event>) {
        val nack = try {
            decodeCommandNack(frame)
                ?: throw ProtocolException("frame is not CommandNack")
        } catch (error: ProtocolException) {
            rejectSession(output, error.message ?: "invalid CommandNack")
            return
        }
        val matchesStartupRequest = nack.requestSequence == pendingDeviceInfoSequence ||
            nack.requestSequence == pendingHeartbeatSequence ||
            nack.requestSequence == pendingStartTelemetrySequence
        if (startupStage != StartupStage.READY && !matchesStartupRequest) {
            output += Event.ProtocolRejected(
                "unrelated CommandNack for sequence ${nack.requestSequence}",
            )
            return
        }
        rejectSession(
            output,
            "CommandNack for sequence ${nack.requestSequence}: " +
                "reason=${nack.reasonCode}, validation=${nack.validationErrorCode}",
        )
    }

    private fun handleSensorSample(frame: DecodedFrame, output: MutableList<Event>) {
        if (startupStage != StartupStage.READY) {
            output += Event.ProtocolRejected(
                "SensorSample arrived before the current session became Ready",
            )
            return
        }
        try {
            val sample = decodeSensorSample(frame)
                ?: throw ProtocolException("frame is not SensorSample")
            when (sample.sensorId) {
                SensorId.SHT30 -> output += Event.Sht30(sample.asSht30Reading())
                SensorId.MS5611 -> {
                    val reading = sample.asMs5611Reading()
                    val altitude = BarometricAltitude.metersFromPressure(
                        reading.pressurePa,
                        qnhHpa * 100.0,
                    )
                    output += Event.Ms5611(reading, altitude)
                }
                SensorId.VL53L0X -> output += Event.Vl53l0x(sample.asVl53l0xReading())
            }
            output += Event.FrameReceived(frame)
        } catch (error: ProtocolException) {
            output += Event.ProtocolRejected(error.message ?: "invalid sensor sample")
        }
    }

    private fun handleDeviceStatusResponse(frame: DecodedFrame, output: MutableList<Event>) {
        try {
            val status = decodeDeviceStatusResponse(frame)
                ?: throw ProtocolException("frame is not DeviceStatusResponse")
            output += Event.DeviceStatusReceived(status)
            output += Event.FrameReceived(frame)
        } catch (error: ProtocolException) {
            output += Event.ProtocolRejected(error.message ?: "invalid DeviceStatusResponse")
        }
    }

    private fun rejectSession(output: MutableList<Event>, reason: String) {
        startupStage = StartupStage.FAILED
        pendingDeviceInfoSequence = null
        pendingHeartbeatSequence = null
        pendingStartTelemetrySequence = null
        output += Event.SessionRejected(reason)
    }

    private fun encodeConnected(request: OutboundRequest, sessionBound: Boolean): ByteArray {
        requireConnected()
        return encode(request, sessionBound)
    }

    private fun encode(request: OutboundRequest, sessionBound: Boolean): ByteArray =
        encodeFrame(request, sessionBound).bytes

    private fun encodeFrame(request: OutboundRequest, sessionBound: Boolean): EncodedFrame {
        val sequence = nextSequence
        nextSequence += 1u
        return EncodedFrame(
            sequence = sequence,
            bytes = SafeOutboundCodec.encode(
                request = request,
                sequence = sequence,
                senderMonotonicUs = monotonicUs(),
                sessionToken = if (sessionBound) requireSessionToken() else null,
            ),
        )
    }

    private fun requireConnected() {
        if (!connected) throw OutboundPolicyException("USB is not attached")
    }

    private fun requireStartupNotFailed() {
        if (startupStage == StartupStage.FAILED) {
            throw OutboundPolicyException("protocol session has failed; detach/reopen is required")
        }
    }

    private fun requireSessionToken(): ULong =
        sessionToken ?: throw OutboundPolicyException("TimeSync/session token is not established")

    private fun resetSessionState() {
        accumulator.reset()
        nextSequence = 1u
        pendingTimeSyncClientUs.clear()
        lastSuccessfulTimeSyncHostUs = null
        startupStage = StartupStage.DETACHED
        pendingDeviceInfoSequence = null
        pendingHeartbeatSequence = null
        pendingStartTelemetrySequence = null
        sessionToken = null
        deviceInfo = null
    }
}
