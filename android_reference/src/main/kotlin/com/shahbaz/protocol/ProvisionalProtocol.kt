package com.shahbaz.protocol

/**
 * Framework-independent reference for Shahbaz wire protocol revision 2.
 *
 * The legacy file/object name is retained for source compatibility. The codec
 * recognizes only enumerated message IDs; arming and actuator safety is enforced
 * by the firmware state machine, heartbeat freshness, and explicit app UX.
 */
object ProvisionalWire {
    const val VERSION: Int = 2
    const val HEADER_LENGTH: Int = 22
    const val CRC_LENGTH: Int = 4
    const val MAX_PAYLOAD_LENGTH: Int = 512
    const val MAX_DECODED_FRAME_LENGTH: Int = HEADER_LENGTH + MAX_PAYLOAD_LENGTH + CRC_LENGTH
    const val MAX_COBS_ENCODED_FRAME_LENGTH: Int =
        MAX_DECODED_FRAME_LENGTH + (MAX_DECODED_FRAME_LENGTH / 254) + 1
    const val MAX_DELIMITED_FRAME_LENGTH: Int = MAX_COBS_ENCODED_FRAME_LENGTH + 1
    const val DELIMITER: Byte = 0
}

enum class MessagePriority(val wireValue: Int) {
    CRITICAL(0),
    HIGH(1),
    NORMAL(2),
    LOW(3);

    companion object {
        fun fromWire(value: Int): MessagePriority =
            values().firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("invalid priority $value")
    }
}

enum class MessageType(val wireValue: Int) {
    DEVICE_INFO_REQUEST(0x0001),
    DEVICE_INFO_RESPONSE(0x0002),
    START_TELEMETRY(0x0010),
    STOP_TELEMETRY(0x0011),
    SET_SENSOR_RATE(0x0012),
    SENSOR_SAMPLE(0x0020),
    DEVICE_STATUS_REQUEST(0x0030),
    DEVICE_STATUS_RESPONSE(0x0031),
    HEARTBEAT(0x0040),
    HEARTBEAT_ACK(0x0041),
    PING(0x0042),
    PONG(0x0043),
    TIME_SYNC_REQUEST(0x0050),
    TIME_SYNC_RESPONSE(0x0051),
    COMMAND_ACK(0x0060),
    COMMAND_NACK(0x0061),
    PROTOCOL_ERROR(0x0062),
    SAFETY_STATE(0x0063),
    EMERGENCY_STOP(0x0070),
    DISARM(0x0071),

    // Physical-control messages. Firmware applies fail-safe state/heartbeat gates.
    ARM_REQUEST(0x8000),
    ARM_CONFIRM(0x8001),
    ACTUATOR_COMMAND(0x8010),
    MOTOR_COMMAND(0x8011),
    SERVO_COMMAND(0x8012),
    SET_CONTROL_MODE(0x8013),
    MOTOR_FRAME_COMMAND(0x8014),
    ;

    companion object {
        fun fromWire(value: Int): MessageType =
            values().firstOrNull { it.wireValue == value }
                ?: throw ProtocolException(
                    "unknown message type 0x${value.toString(16).padStart(4, '0')}",
                )
    }
}

enum class MessagePolicy {
    SUPPORTED_CURRENT_PHASE,
    RECEIVE_ONLY,
}

private val hostOutboundTypes = setOf(
    MessageType.DEVICE_INFO_REQUEST,
    MessageType.START_TELEMETRY,
    MessageType.STOP_TELEMETRY,
    MessageType.SET_SENSOR_RATE,
    MessageType.DEVICE_STATUS_REQUEST,
    MessageType.HEARTBEAT,
    MessageType.PING,
    MessageType.TIME_SYNC_REQUEST,
    MessageType.EMERGENCY_STOP,
    MessageType.DISARM,
    MessageType.ARM_REQUEST,
    MessageType.ARM_CONFIRM,
    MessageType.ACTUATOR_COMMAND,
    MessageType.MOTOR_COMMAND,
    MessageType.SERVO_COMMAND,
    MessageType.SET_CONTROL_MODE,
    MessageType.MOTOR_FRAME_COMMAND,
)

private val sessionBoundTypes = setOf(
    MessageType.START_TELEMETRY,
    MessageType.STOP_TELEMETRY,
    MessageType.SET_SENSOR_RATE,
    MessageType.HEARTBEAT,
    MessageType.ARM_REQUEST,
    MessageType.ARM_CONFIRM,
    MessageType.ACTUATOR_COMMAND,
    MessageType.MOTOR_COMMAND,
    MessageType.SERVO_COMMAND,
    MessageType.SET_CONTROL_MODE,
    MessageType.MOTOR_FRAME_COMMAND,
)

fun outboundPolicy(type: MessageType): MessagePolicy =
    if (type in hostOutboundTypes) MessagePolicy.SUPPORTED_CURRENT_PHASE else MessagePolicy.RECEIVE_ONLY

class ProtocolException(message: String) : IllegalArgumentException(message)

class OutboundPolicyException(message: String) : IllegalStateException(message)

data class FrameHeader(
    val messageType: MessageType,
    val priority: MessagePriority,
    val sequence: UInt,
    val senderMonotonicUs: ULong,
    val payloadLength: Int,
    val flags: Int = 0,
    val version: Int = ProvisionalWire.VERSION,
    val headerLength: Int = ProvisionalWire.HEADER_LENGTH,
    val reserved: Int = 0,
)

/** A decoded frame with a copied payload of at most 512 bytes. */
class DecodedFrame(
    val header: FrameHeader,
    payload: ByteArray,
) {
    val payload: ByteArray = payload.copyOf()
}

class OutboundRequest(
    val messageType: MessageType,
    val priority: MessagePriority,
    payload: ByteArray,
) {
    val payload: ByteArray = payload.copyOf()
}

object Crc32c {
    /** Standard reflected CRC-32C: polynomial 0x82F63B78, init/xorout 0xFFFFFFFF. */
    fun calculate(bytes: ByteArray): Int {
        var crc = -1
        for (byte in bytes) {
            crc = crc xor (byte.toInt() and 0xFF)
            repeat(8) {
                val mask = -(crc and 1)
                crc = (crc ushr 1) xor (0x82F63B78.toInt() and mask)
            }
        }
        return crc xor -1
    }
}

object Cobs {
    /** Encode without adding the revision-2 zero delimiter. */
    fun encode(input: ByteArray): ByteArray {
        if (input.size > ProvisionalWire.MAX_DECODED_FRAME_LENGTH) {
            throw ProtocolException("COBS input exceeds the fixed decoded-frame bound")
        }
        val capacity = input.size + (input.size / 254) + 1
        val output = ByteArray(capacity)
        var readIndex = 0
        var writeIndex = 1
        var codeIndex = 0
        var code = 1

        while (readIndex < input.size) {
            val value = input[readIndex].toInt() and 0xFF
            if (value == 0) {
                output[codeIndex] = code.toByte()
                codeIndex = writeIndex
                writeIndex += 1
                code = 1
                readIndex += 1
                continue
            }

            output[writeIndex] = input[readIndex]
            writeIndex += 1
            readIndex += 1
            code += 1
            if (code == 0xFF) {
                output[codeIndex] = code.toByte()
                codeIndex = writeIndex
                writeIndex += 1
                code = 1
            }
        }
        output[codeIndex] = code.toByte()
        return output.copyOf(writeIndex)
    }

    /** Strictly decode one delimiter-free body into a bounded buffer. */
    fun decode(input: ByteArray): ByteArray {
        if (input.isEmpty()) throw ProtocolException("empty COBS encoding")
        if (input.size > ProvisionalWire.MAX_COBS_ENCODED_FRAME_LENGTH) {
            throw ProtocolException("COBS input exceeds the fixed encoded-frame bound")
        }
        if (input.any { it == ProvisionalWire.DELIMITER }) {
            throw ProtocolException("COBS body contains an unexpected zero")
        }

        val output = ByteArray(ProvisionalWire.MAX_DECODED_FRAME_LENGTH)
        var readIndex = 0
        var writeIndex = 0
        while (readIndex < input.size) {
            val code = input[readIndex].toInt() and 0xFF
            readIndex += 1
            val blockLength = code - 1
            if (blockLength > input.size - readIndex) {
                throw ProtocolException("truncated COBS block")
            }
            if (blockLength > output.size - writeIndex) {
                throw ProtocolException("decoded frame exceeds the fixed bound")
            }
            repeat(blockLength) {
                val byte = input[readIndex]
                if (byte == ProvisionalWire.DELIMITER) {
                    throw ProtocolException("COBS body contains an unexpected zero")
                }
                output[writeIndex] = byte
                writeIndex += 1
                readIndex += 1
            }
            if (code != 0xFF && readIndex < input.size) {
                if (writeIndex >= output.size) {
                    throw ProtocolException("decoded frame exceeds the fixed bound")
                }
                output[writeIndex] = 0
                writeIndex += 1
            }
        }
        return output.copyOf(writeIndex)
    }
}

object SafeOutboundCodec {
    /** Encode one enumerated host-to-device request, including its final zero delimiter. */
    fun encode(
        request: OutboundRequest,
        sequence: UInt,
        senderMonotonicUs: ULong,
        sessionToken: ULong? = null,
    ): ByteArray {
        if (request.messageType !in hostOutboundTypes) {
            throw OutboundPolicyException(
                "${request.messageType.name} is device-to-host only",
            )
        }
        val wirePayload = if (request.messageType in sessionBoundTypes) {
            val token = sessionToken?.takeIf { it != 0uL } ?: throw OutboundPolicyException(
                "${request.messageType.name} requires a negotiated session token",
            )
            ByteArray(8).also { writeU64(it, 0, token) } + request.payload
        } else {
            request.payload
        }
        if (wirePayload.size > ProvisionalWire.MAX_PAYLOAD_LENGTH) {
            throw ProtocolException("payload exceeds ${ProvisionalWire.MAX_PAYLOAD_LENGTH} bytes")
        }

        val withoutCrc = ByteArray(ProvisionalWire.HEADER_LENGTH + wirePayload.size)
        withoutCrc[0] = ProvisionalWire.VERSION.toByte()
        withoutCrc[1] = ProvisionalWire.HEADER_LENGTH.toByte()
        writeU16(withoutCrc, 2, request.messageType.wireValue)
        writeU16(withoutCrc, 4, 0) // No revision-2 request defines flag bits.
        withoutCrc[6] = request.priority.wireValue.toByte()
        withoutCrc[7] = 0
        writeU32(withoutCrc, 8, sequence)
        writeU64(withoutCrc, 12, senderMonotonicUs)
        writeU16(withoutCrc, 20, wirePayload.size)
        wirePayload.copyInto(withoutCrc, ProvisionalWire.HEADER_LENGTH)

        val decoded = ByteArray(withoutCrc.size + ProvisionalWire.CRC_LENGTH)
        withoutCrc.copyInto(decoded)
        writeU32(decoded, withoutCrc.size, Crc32c.calculate(withoutCrc).toUInt())
        val encoded = Cobs.encode(decoded)
        if (encoded.size > ProvisionalWire.MAX_COBS_ENCODED_FRAME_LENGTH) {
            throw ProtocolException("COBS frame exceeds the fixed bound")
        }
        return encoded + byteArrayOf(ProvisionalWire.DELIMITER)
    }
}

object FrameDecoder {
    /** Decode exactly one delimiter-free COBS body. */
    fun decodeBody(encodedBody: ByteArray): DecodedFrame {
        if (encodedBody.size > ProvisionalWire.MAX_COBS_ENCODED_FRAME_LENGTH) {
            throw ProtocolException("COBS frame exceeds the fixed bound")
        }
        val decoded = Cobs.decode(encodedBody)
        if (decoded.size < ProvisionalWire.HEADER_LENGTH + ProvisionalWire.CRC_LENGTH) {
            throw ProtocolException("decoded frame is shorter than header plus CRC")
        }

        // Authenticate the complete header before interpreting any of its
        // fields. The physical frame boundary locates the final CRC.
        val crcOffset = decoded.size - ProvisionalWire.CRC_LENGTH
        val receivedCrc = readU32(decoded, crcOffset)
        val calculatedCrc = Crc32c.calculate(decoded.copyOfRange(0, crcOffset)).toUInt()
        if (receivedCrc != calculatedCrc) {
            throw ProtocolException(
                "CRC-32C mismatch: received=${receivedCrc.toString(16)}, " +
                    "calculated=${calculatedCrc.toString(16)}",
            )
        }

        val version = readU8(decoded, 0)
        val headerLength = readU8(decoded, 1)
        if (version != ProvisionalWire.VERSION) {
            throw ProtocolException("unsupported protocol version $version")
        }
        if (headerLength != ProvisionalWire.HEADER_LENGTH) {
            throw ProtocolException("invalid header length $headerLength")
        }
        val messageType = MessageType.fromWire(readU16(decoded, 2))
        val flags = readU16(decoded, 4)
        val priority = MessagePriority.fromWire(readU8(decoded, 6))
        val reserved = readU8(decoded, 7)
        if (reserved != 0) throw ProtocolException("reserved header byte is nonzero")
        val sequence = readU32(decoded, 8)
        val senderMonotonicUs = readU64(decoded, 12)
        val payloadLength = readU16(decoded, 20)
        if (payloadLength > ProvisionalWire.MAX_PAYLOAD_LENGTH) {
            throw ProtocolException("declared payload exceeds the fixed bound")
        }
        val expectedLength =
            ProvisionalWire.HEADER_LENGTH + payloadLength + ProvisionalWire.CRC_LENGTH
        if (decoded.size != expectedLength) {
            throw ProtocolException(
                "length mismatch: decoded=${decoded.size}, expected=$expectedLength",
            )
        }
        val payload = decoded.copyOfRange(ProvisionalWire.HEADER_LENGTH, crcOffset)
        return DecodedFrame(
            FrameHeader(
                messageType = messageType,
                priority = priority,
                sequence = sequence,
                senderMonotonicUs = senderMonotonicUs,
                payloadLength = payloadLength,
                flags = flags,
                version = version,
                headerLength = headerLength,
                reserved = reserved,
            ),
            payload,
        )
    }

    /** Decode a body followed by exactly one zero delimiter. */
    fun decodeDelimited(frame: ByteArray): DecodedFrame {
        if (frame.isEmpty() || frame.last() != ProvisionalWire.DELIMITER) {
            throw ProtocolException("frame is missing its final zero delimiter")
        }
        if (frame.copyOf(frame.size - 1).any { it == ProvisionalWire.DELIMITER }) {
            throw ProtocolException("frame contains an unexpected embedded delimiter")
        }
        return decodeBody(frame.copyOf(frame.size - 1))
    }
}

enum class StreamEventKind {
    FRAME_READY,
    EMPTY_DELIMITER,
    FRAME_REJECTED,
    OVERSIZE_DISCARDED,
}

data class StreamEvent(
    val kind: StreamEventKind,
    val frame: DecodedFrame? = null,
    val error: String? = null,
)

/**
 * Incremental bounded parser for arbitrary USB transfer fragmentation.
 *
 * After overflow, input is discarded through the next zero delimiter. One
 * instance represents one connection and must be reset on reconnect.
 */
class FrameAccumulator {
    private val encoded = ByteArray(ProvisionalWire.MAX_COBS_ENCODED_FRAME_LENGTH)
    private var size = 0
    var discardingOversizeFrame: Boolean = false
        private set

    val bufferedSize: Int
        get() = size

    fun reset() {
        size = 0
        discardingOversizeFrame = false
    }

    fun feed(chunk: ByteArray): List<StreamEvent> {
        val events = mutableListOf<StreamEvent>()
        for (byte in chunk) {
            if (byte != ProvisionalWire.DELIMITER) {
                if (discardingOversizeFrame) continue
                if (size >= encoded.size) {
                    size = 0
                    discardingOversizeFrame = true
                    continue
                }
                encoded[size] = byte
                size += 1
                continue
            }

            if (discardingOversizeFrame) {
                reset()
                events += StreamEvent(
                    StreamEventKind.OVERSIZE_DISCARDED,
                    error = "encoded frame exceeded the fixed bound",
                )
                continue
            }
            if (size == 0) {
                events += StreamEvent(StreamEventKind.EMPTY_DELIMITER)
                continue
            }

            val body = encoded.copyOf(size)
            size = 0
            try {
                events += StreamEvent(
                    StreamEventKind.FRAME_READY,
                    frame = FrameDecoder.decodeBody(body),
                )
            } catch (error: ProtocolException) {
                events += StreamEvent(StreamEventKind.FRAME_REJECTED, error = error.message)
            }
        }
        return events
    }
}

object SafeRequests {
    fun deviceInfo(): OutboundRequest = empty(MessageType.DEVICE_INFO_REQUEST)

    fun deviceStatus(): OutboundRequest = empty(MessageType.DEVICE_STATUS_REQUEST)

    fun startTelemetry(): OutboundRequest = empty(MessageType.START_TELEMETRY)

    fun stopTelemetry(): OutboundRequest = empty(MessageType.STOP_TELEMETRY)

    fun heartbeat(): OutboundRequest = empty(MessageType.HEARTBEAT)

    fun disarm(): OutboundRequest =
        OutboundRequest(MessageType.DISARM, MessagePriority.CRITICAL, byteArrayOf())

    fun emergencyStop(): OutboundRequest =
        OutboundRequest(MessageType.EMERGENCY_STOP, MessagePriority.CRITICAL, byteArrayOf())

    fun ping(token: ULong): OutboundRequest =
        OutboundRequest(MessageType.PING, MessagePriority.HIGH, u64Payload(token))

    fun timeSync(clientMonotonicUs: ULong): OutboundRequest =
        OutboundRequest(
            MessageType.TIME_SYNC_REQUEST,
            MessagePriority.HIGH,
            u64Payload(clientMonotonicUs),
        )

    fun setSensorRate(sensorId: Int, instanceId: Int, intervalUs: UInt): OutboundRequest {
        requireUnsignedInt("sensorId", sensorId, 8)
        requireUnsignedInt("instanceId", instanceId, 8)
        if (intervalUs == 0u) throw ProtocolException("intervalUs must be greater than zero")
        val payload = ByteArray(6)
        payload[0] = sensorId.toByte()
        payload[1] = instanceId.toByte()
        writeU32(payload, 2, intervalUs)
        return OutboundRequest(MessageType.SET_SENSOR_RATE, MessagePriority.HIGH, payload)
    }

    fun arm(): OutboundRequest =
        OutboundRequest(MessageType.ARM_REQUEST, MessagePriority.CRITICAL, byteArrayOf())

    /** Legacy compatibility request; production firmware rejects this by default. */
    fun motor(channel: Int, pulseUs: Int): OutboundRequest =
        pulseRequest(MessageType.MOTOR_COMMAND, channel, pulseUs, 900, 2100)

    fun servo(channel: Int, pulseUs: Int): OutboundRequest =
        pulseRequest(MessageType.SERVO_COMMAND, channel, pulseUs, 500, 2500)

    /** Legacy generic request; kind=1 is rejected by production firmware by default. */
    fun actuator(kind: Int, channel: Int, pulseUs: Int): OutboundRequest {
        requireUnsignedInt("kind", kind, 8)
        if (kind !in 1..2) throw ProtocolException("kind must be 1 (motor) or 2 (servo)")
        requireUnsignedInt("channel", channel, 8)
        val minimum = if (kind == 1) 900 else 500
        val maximum = if (kind == 1) 2100 else 2500
        if (pulseUs !in minimum..maximum) throw ProtocolException("pulseUs outside [$minimum, $maximum]")
        val payload = ByteArray(4)
        payload[0] = kind.toByte()
        payload[1] = channel.toByte()
        writeU16(payload, 2, pulseUs)
        return OutboundRequest(MessageType.ACTUATOR_COMMAND, MessagePriority.CRITICAL, payload)
    }

    /**
     * Preferred flight-control request: exactly one complete Quad-X generation.
     * The application payload is count=4 followed by canonical channels 0..3,
     * each with a little-endian u16 pulse width.
     */
    fun motorFrame(pulses: List<MotorPulse>): OutboundRequest {
        if (pulses.size != QUAD_X_MOTOR_COUNT) {
            throw ProtocolException("Quad-X motor frame must contain exactly four channels")
        }
        val byChannel = arrayOfNulls<MotorPulse>(QUAD_X_MOTOR_COUNT)
        for (pulse in pulses) {
            if (pulse.channel !in byChannel.indices) {
                throw ProtocolException("Quad-X motor channel must be in 0..3")
            }
            if (pulse.pulseUs !in 900..2100) {
                throw ProtocolException("pulseUs outside [900, 2100]")
            }
            if (byChannel[pulse.channel] != null) {
                throw ProtocolException("Quad-X motor channel ${pulse.channel} is duplicated")
            }
            byChannel[pulse.channel] = pulse
        }

        val payload = ByteArray(1 + QUAD_X_MOTOR_COUNT * 3)
        payload[0] = QUAD_X_MOTOR_COUNT.toByte()
        for (channel in byChannel.indices) {
            val pulse = byChannel[channel]
                ?: throw ProtocolException("Quad-X motor channel $channel is missing")
            val offset = 1 + channel * 3
            payload[offset] = channel.toByte()
            writeU16(payload, offset + 1, pulse.pulseUs)
        }
        return OutboundRequest(MessageType.MOTOR_FRAME_COMMAND, MessagePriority.CRITICAL, payload)
    }

    fun controlMode(mode: Int = 0): OutboundRequest {
        if (mode != 0) throw ProtocolException("only control mode 0 is supported")
        return OutboundRequest(MessageType.SET_CONTROL_MODE, MessagePriority.HIGH, byteArrayOf(0))
    }

    private fun pulseRequest(
        type: MessageType, channel: Int, pulseUs: Int, minimumUs: Int, maximumUs: Int,
    ): OutboundRequest {
        requireUnsignedInt("channel", channel, 8)
        if (pulseUs !in minimumUs..maximumUs) {
            throw ProtocolException("pulseUs outside [$minimumUs, $maximumUs]")
        }
        val payload = ByteArray(3)
        payload[0] = channel.toByte()
        writeU16(payload, 1, pulseUs)
        return OutboundRequest(type, MessagePriority.CRITICAL, payload)
    }

    private fun empty(type: MessageType): OutboundRequest =
        OutboundRequest(type, MessagePriority.HIGH, byteArrayOf())

    private fun u64Payload(value: ULong): ByteArray =
        ByteArray(8).also { writeU64(it, 0, value) }
}

data class TimeSyncResponse(
    val clientSendUs: ULong,
    val deviceRxUs: ULong,
    val deviceTxUs: ULong,
    val sessionToken: ULong,
)

/** Decode a TimeSyncResponse payload, otherwise return null. */
fun decodeTimeSyncResponse(frame: DecodedFrame): TimeSyncResponse? {
    if (
        frame.header.messageType != MessageType.TIME_SYNC_RESPONSE ||
        frame.payload.size != 32
    ) {
        return null
    }
    return TimeSyncResponse(
        readU64(frame.payload, 0),
        readU64(frame.payload, 8),
        readU64(frame.payload, 16),
        readU64(frame.payload, 24),
    )
}

enum class DeviceTarget(val wireValue: Int) {
    ESP32_S3(1);

    companion object {
        fun fromWire(value: Int): DeviceTarget =
            values().firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unsupported DeviceInfo target $value")
    }
}

/** Raw, bounded 20-byte DeviceInfoResponse decoded from Protocol v2. */
data class DeviceInfo(
    val protocolVersion: Int,
    val target: DeviceTarget,
    val supportedMotorChannels: Int,
    val supportedServoChannels: Int,
    val detectedFlashBytes: UInt,
    val detectedPsramBytes: UInt,
    val boardValidationIssueMask: UInt,
    val activeMotorChannels: Int,
    val activeServoChannels: Int,
    val actuatorAvailable: Boolean,
    val actuatorsEnabledByConfiguration: Boolean,
)

/** Validated sensor-only profile plus retained, observable advisory evidence bits. */
data class ValidatedDeviceInfo(
    val deviceInfo: DeviceInfo,
    val advisoryIssueMask: UInt,
)

object BoardValidationIssues {
    const val FATAL_MASK: UInt = 0x400Fu
    const val ADVISORY_MASK: UInt = 0x7BFF0u
    const val KNOWN_MASK: UInt = 0x7FFFFu
}

fun decodeDeviceInfoResponse(frame: DecodedFrame): DeviceInfo? {
    if (frame.header.messageType != MessageType.DEVICE_INFO_RESPONSE) return null
    if (frame.payload.size != 20) {
        throw ProtocolException("DeviceInfoResponse payload must be 20 bytes")
    }
    return DeviceInfo(
        protocolVersion = readU8(frame.payload, 0),
        target = DeviceTarget.fromWire(readU8(frame.payload, 1)),
        supportedMotorChannels = readU8(frame.payload, 2),
        supportedServoChannels = readU8(frame.payload, 3),
        detectedFlashBytes = readU32(frame.payload, 4),
        detectedPsramBytes = readU32(frame.payload, 8),
        boardValidationIssueMask = readU32(frame.payload, 12),
        activeMotorChannels = readU8(frame.payload, 16),
        activeServoChannels = readU8(frame.payload, 17),
        actuatorAvailable = readBooleanByte(frame.payload, 18, "actuator_available"),
        actuatorsEnabledByConfiguration = readBooleanByte(
            frame.payload,
            19,
            "actuators_enabled_by_config",
        ),
    )
}

fun DeviceInfo.validateSensorOnlyProfile(): ValidatedDeviceInfo {
    val fatalIssues = boardValidationIssueMask and BoardValidationIssues.FATAL_MASK
    val unknownIssues = boardValidationIssueMask and BoardValidationIssues.KNOWN_MASK.inv()
    when {
        protocolVersion != ProvisionalWire.VERSION ->
            throw ProtocolException(
                "DeviceInfo reports Protocol $protocolVersion, expected ${ProvisionalWire.VERSION}",
            )
        target != DeviceTarget.ESP32_S3 ->
            throw ProtocolException("DeviceInfo target is not ESP32-S3")
        fatalIssues != 0u ->
            throw ProtocolException(
                "DeviceInfo reports fatal board issues 0x${fatalIssues.toString(16)}",
            )
        unknownIssues != 0u ->
            throw ProtocolException(
                "DeviceInfo reports unknown board issues 0x${unknownIssues.toString(16)}",
            )
        actuatorAvailable ||
            actuatorsEnabledByConfiguration ||
            activeMotorChannels != 0 ||
            activeServoChannels != 0 ->
            throw ProtocolException(
                "sensor-only client requires actuator hardware unavailable and disabled",
            )
    }
    return ValidatedDeviceInfo(
        deviceInfo = this,
        advisoryIssueMask = boardValidationIssueMask and BoardValidationIssues.ADVISORY_MASK,
    )
}

const val QUAD_X_MOTOR_COUNT: Int = 4

data class MotorPulse(
    val channel: Int,
    val pulseUs: Int,
)

/**
 * Per-role VL53L0X lifecycle exported by the optional DeviceStatus extension.
 *
 * `DISABLED_OR_ABSENT` deliberately does not claim whether hardware is fitted:
 * the firmware cannot establish that distinction when the rangefinder feature is
 * disabled by its evidence gate. `DEGRADED` means the role was configured but is
 * not currently producing healthy samples.
 */
enum class RangefinderLifecycle(val wireValue: Int) {
    DISABLED_OR_ABSENT(0),
    INITIALIZING(1),
    LIVE(2),
    DEGRADED(3),
    ;

    companion object {
        fun fromWire(value: Int): RangefinderLifecycle =
            entries.firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unknown rangefinder lifecycle $value")
    }
}

/** Fixed mounting-order view of the four rangefinder lifecycle bytes. */
data class RangefinderLifecycleStatus(
    val ground: RangefinderLifecycle,
    val up: RangefinderLifecycle,
    val frontLeft: RangefinderLifecycle,
    val frontRight: RangefinderLifecycle,
) {
    operator fun get(role: RangefinderRole): RangefinderLifecycle = when (role) {
        RangefinderRole.GROUND -> ground
        RangefinderRole.UP -> up
        RangefinderRole.FRONT_LEFT -> frontLeft
        RangefinderRole.FRONT_RIGHT -> frontRight
    }
}

enum class DeviceSafetyState(val wireValue: Int) {
    BOOTING(0),
    DISARMED(1),
    ARMING(2),
    ARMED(3),
    FAILSAFE(4),
    FAULT(5),
    EMERGENCY_STOPPED(6),
    ;

    companion object {
        fun fromWire(value: Int): DeviceSafetyState =
            entries.firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unknown device safety state $value")
    }
}

enum class DeviceCommunicationState(val wireValue: Int) {
    DISCONNECTED(0),
    CONNECTED_INACTIVE(1),
    TRAFFIC_PRESENT_BUT_INVALID(2),
    HEARTBEAT_MISSING(3),
    HEALTHY(4),
    ;

    companion object {
        fun fromWire(value: Int): DeviceCommunicationState =
            entries.firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unknown communication state $value")
    }
}

/**
 * DeviceStatusResponse decoded from either the legacy 6-byte payload or the
 * extended 10-byte payload. A null [rangefinders] value means that the peer used
 * the legacy shape; it must not be interpreted as evidence that sensors are absent.
 */
data class DeviceStatus(
    val safetyState: DeviceSafetyState,
    val communicationState: DeviceCommunicationState,
    val telemetryEnabled: Boolean,
    val armed: Boolean,
    val sht30Online: Boolean,
    val ms5611Online: Boolean,
    val rangefinders: RangefinderLifecycleStatus?,
)

fun decodeDeviceStatusResponse(frame: DecodedFrame): DeviceStatus? {
    if (frame.header.messageType != MessageType.DEVICE_STATUS_RESPONSE) return null
    if (frame.payload.size != 6 && frame.payload.size != 10) {
        throw ProtocolException("DeviceStatusResponse payload must be 6 or 10 bytes")
    }
    val safetyState = DeviceSafetyState.fromWire(readU8(frame.payload, 0))
    val communicationState = DeviceCommunicationState.fromWire(readU8(frame.payload, 1))
    val rangefinders = if (frame.payload.size == 10) {
        RangefinderLifecycleStatus(
            ground = RangefinderLifecycle.fromWire(readU8(frame.payload, 6)),
            up = RangefinderLifecycle.fromWire(readU8(frame.payload, 7)),
            frontLeft = RangefinderLifecycle.fromWire(readU8(frame.payload, 8)),
            frontRight = RangefinderLifecycle.fromWire(readU8(frame.payload, 9)),
        )
    } else {
        null
    }
    return DeviceStatus(
        safetyState = safetyState,
        communicationState = communicationState,
        telemetryEnabled = readBooleanByte(frame.payload, 2, "telemetry_enabled"),
        armed = readBooleanByte(frame.payload, 3, "armed"),
        sht30Online = readBooleanByte(frame.payload, 4, "sht30_online"),
        ms5611Online = readBooleanByte(frame.payload, 5, "ms5611_online"),
        rangefinders = rangefinders,
    )
}

/** ApplicationAction values emitted in the fifth byte of CommandAck. */
enum class ApplicationAction(val wireValue: Int) {
    NONE(0),
    REQUEST_DEVICE_INFO(1),
    START_TELEMETRY(2),
    STOP_TELEMETRY(3),
    SET_SENSOR_RATE(4),
    REQUEST_DEVICE_STATUS(5),
    HEARTBEAT_RECEIVED(6),
    HEARTBEAT_ACK_RECEIVED(7),
    PING_RECEIVED(8),
    PONG_RECEIVED(9),
    TIME_SYNC_REQUEST_RECEIVED(10),
    EMERGENCY_STOP_APPLIED(11),
    DISARM_APPLIED(12),
    ARM_APPLIED(13),
    MOTOR_COMMAND(14),
    SERVO_COMMAND(15),
    ACTUATOR_COMMAND(16),
    SET_CONTROL_MODE(17),
    MOTOR_FRAME_COMMAND(18),
    ;

    companion object {
        fun fromWire(value: Int): ApplicationAction =
            values().firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unknown CommandAck application action $value")
    }
}

data class CommandAck(
    val requestSequence: UInt,
    val action: ApplicationAction,
)

data class CommandNack(
    val requestSequence: UInt,
    val reasonCode: Int,
    val validationErrorCode: Int,
)

fun decodeCommandAck(frame: DecodedFrame): CommandAck? {
    if (frame.header.messageType != MessageType.COMMAND_ACK) return null
    if (frame.payload.size != 5) throw ProtocolException("CommandAck payload must be 5 bytes")
    return CommandAck(
        requestSequence = readU32(frame.payload, 0),
        action = ApplicationAction.fromWire(readU8(frame.payload, 4)),
    )
}

fun decodeCommandNack(frame: DecodedFrame): CommandNack? {
    if (frame.header.messageType != MessageType.COMMAND_NACK) return null
    if (frame.payload.size != 8) throw ProtocolException("CommandNack payload must be 8 bytes")
    return CommandNack(
        requestSequence = readU32(frame.payload, 0),
        reasonCode = readU16(frame.payload, 4),
        validationErrorCode = readU16(frame.payload, 6),
    )
}

fun requireHeartbeatAck(frame: DecodedFrame): Boolean {
    if (frame.header.messageType != MessageType.HEARTBEAT_ACK) return false
    if (frame.header.priority != MessagePriority.CRITICAL) {
        throw ProtocolException("HeartbeatAck priority must be critical")
    }
    if (frame.payload.isNotEmpty()) throw ProtocolException("HeartbeatAck payload must be empty")
    return true
}

private fun requireUnsignedInt(name: String, value: Int, bits: Int) {
    val maximum = when (bits) {
        8 -> 0xFF
        16 -> 0xFFFF
        else -> throw IllegalArgumentException("unsupported width $bits")
    }
    if (value < 0 || value > maximum) {
        throw ProtocolException("$name must be in [0, $maximum]")
    }
}

private fun readU8(input: ByteArray, offset: Int): Int = input[offset].toInt() and 0xFF

private fun readBooleanByte(input: ByteArray, offset: Int, label: String): Boolean =
    when (val value = readU8(input, offset)) {
        0 -> false
        1 -> true
        else -> throw ProtocolException("$label must be encoded as 0 or 1, got $value")
    }

private fun readU16(input: ByteArray, offset: Int): Int =
    readU8(input, offset) or (readU8(input, offset + 1) shl 8)

private fun readU32(input: ByteArray, offset: Int): UInt {
    var result = 0u
    repeat(4) { index ->
        result = result or (readU8(input, offset + index).toUInt() shl (8 * index))
    }
    return result
}

private fun readU64(input: ByteArray, offset: Int): ULong {
    var result = 0uL
    repeat(8) { index ->
        val byte = readU8(input, offset + index).toULong()
        result = result or (byte shl (8 * index))
    }
    return result
}

private fun writeU16(output: ByteArray, offset: Int, value: Int) {
    requireUnsignedInt("u16", value, 16)
    output[offset] = (value and 0xFF).toByte()
    output[offset + 1] = ((value ushr 8) and 0xFF).toByte()
}

private fun writeU32(output: ByteArray, offset: Int, value: UInt) {
    repeat(4) { index ->
        output[offset + index] = ((value shr (8 * index)) and 0xFFu).toByte()
    }
}

private fun writeU64(output: ByteArray, offset: Int, value: ULong) {
    repeat(8) { index ->
        output[offset + index] = ((value shr (8 * index)) and 0xFFuL).toByte()
    }
}

// ---- Sensor telemetry decoding shared by Android clients and CLI tests. ----

enum class SensorId(val wireValue: Int) {
    SHT30(1),
    MS5611(2),
    VL53L0X(3),
    ;

    companion object {
        fun fromWire(value: Int): SensorId =
            values().firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unknown sensor id $value")
    }
}

enum class SensorFieldType(val wireValue: Int) {
    SIGNED32(1),
    UNSIGNED32(2);

    companion object {
        fun fromWire(value: Int): SensorFieldType =
            values().firstOrNull { it.wireValue == value }
                ?: throw ProtocolException("unknown sensor field type $value")
    }
}

data class SensorField(
    val fieldId: Int,
    val type: SensorFieldType,
    val rawBits: UInt,
) {
    fun signedValue(): Int {
        if (type != SensorFieldType.SIGNED32) {
            throw ProtocolException("field $fieldId is not signed32")
        }
        return rawBits.toInt()
    }

    fun unsignedValue(): UInt {
        if (type != SensorFieldType.UNSIGNED32) {
            throw ProtocolException("field $fieldId is not unsigned32")
        }
        return rawBits
    }
}

data class SensorSample(
    val sensorId: SensorId,
    val instanceId: Int,
    val sampleSequence: UInt,
    val monotonicTimestampUs: ULong,
    val validityFlags: UInt,
    val qualityFlags: UInt,
    val healthFlags: UInt,
    val fields: Map<Int, SensorField>,
)

data class Sht30Reading(
    val temperatureMilliCelsius: Int,
    val relativeHumidityMilliPercent: UInt,
)

data class Ms5611Reading(
    val pressurePa: Int,
    val temperatureMilliCelsius: Int,
)

/** Stable mounting role carried by the VL53L0X instance byte. */
enum class RangefinderRole(val instanceId: Int) {
    GROUND(0),
    UP(1),
    FRONT_LEFT(2),
    FRONT_RIGHT(3),
    ;

    companion object {
        fun fromInstanceId(value: Int): RangefinderRole =
            entries.firstOrNull { it.instanceId == value }
                ?: throw ProtocolException("unsupported VL53L0X instance $value")
    }
}

/** One control-eligible reading from a fixed-role VL53L0X sensor. */
data class Vl53l0xReading(
    val role: RangefinderRole,
    val distanceMillimeters: Int,
    val rawRangeStatus: Int,
    val signalQualityPercent: Int,
)

object SensorValidity {
    const val TRANSPORT_VALID: UInt = 0x01u
    const val CRC_VALID: UInt = 0x02u
    const val CALIBRATION_VALID: UInt = 0x04u
    const val TIMING_VALID: UInt = 0x08u
    const val PLAUSIBILITY_VALID: UInt = 0x10u
    const val COMMON_REQUIRED: UInt = 0x1Bu
}

object SensorQuality {
    const val FRESH: UInt = 0x01u
}

fun decodeSensorSample(frame: DecodedFrame): SensorSample? {
    if (frame.header.messageType != MessageType.SENSOR_SAMPLE) return null
    val payload = frame.payload
    if (payload.size < 27) throw ProtocolException("sensor sample is shorter than 27-byte prefix")

    val sensorId = SensorId.fromWire(readU8(payload, 0))
    val instanceId = readU8(payload, 1)
    val sampleSequence = readU32(payload, 2)
    val timestampUs = readU64(payload, 6)
    val validity = readU32(payload, 14)
    val quality = readU32(payload, 18)
    val health = readU32(payload, 22)
    val fieldCount = readU8(payload, 26)
    val expectedLength = 27 + fieldCount * 6
    if (payload.size != expectedLength) {
        throw ProtocolException("sensor sample length ${payload.size} != $expectedLength")
    }

    val fields = linkedMapOf<Int, SensorField>()
    var offset = 27
    repeat(fieldCount) {
        val fieldId = readU8(payload, offset)
        val fieldType = SensorFieldType.fromWire(readU8(payload, offset + 1))
        val rawBits = readU32(payload, offset + 2)
        if (fields.containsKey(fieldId)) throw ProtocolException("duplicate sensor field $fieldId")
        fields[fieldId] = SensorField(fieldId, fieldType, rawBits)
        offset += 6
    }

    return SensorSample(
        sensorId = sensorId,
        instanceId = instanceId,
        sampleSequence = sampleSequence,
        monotonicTimestampUs = timestampUs,
        validityFlags = validity,
        qualityFlags = quality,
        healthFlags = health,
        fields = fields,
    )
}

private fun SensorSample.requireUsableCommon() {
    if (instanceId != 0) throw ProtocolException("unsupported sensor instance $instanceId")
    if ((validityFlags and SensorValidity.COMMON_REQUIRED) != SensorValidity.COMMON_REQUIRED) {
        throw ProtocolException("sensor validity flags incomplete: 0x${validityFlags.toString(16)}")
    }
    if ((qualityFlags and SensorQuality.FRESH) == 0u) {
        throw ProtocolException("sensor sample is not fresh")
    }
}

/** Null (legacy producer) and UInt.MAX_VALUE both mean unknown timing for control. */
fun SensorSample.acquisitionTimeUncertaintyMicros(): UInt? = fields[8]?.unsignedValue()

private fun SensorSample.requireMeasurementFields(required: Set<Int>) {
    if (fields.keys != required && fields.keys != required + 8) {
        throw ProtocolException("sensor field set must be $required with optional timing field 8")
    }
    acquisitionTimeUncertaintyMicros() // Reject malformed type even in diagnostics.
}

fun SensorSample.asSht30Reading(): Sht30Reading {
    requireUsableCommon()
    if (sensorId != SensorId.SHT30) throw ProtocolException("sample is not SHT30")
    requireMeasurementFields(setOf(1, 2))
    val temperature = fields.getValue(1).signedValue()
    val humidity = fields.getValue(2).unsignedValue()
    if (temperature !in -40_000..125_000) {
        throw ProtocolException("SHT30 temperature is outside physical range")
    }
    if (humidity > 100_000u) throw ProtocolException("SHT30 humidity is outside 0..100%")
    return Sht30Reading(temperature, humidity)
}

fun SensorSample.asMs5611Reading(): Ms5611Reading {
    requireUsableCommon()
    if (sensorId != SensorId.MS5611) throw ProtocolException("sample is not MS5611")
    if ((validityFlags and SensorValidity.CALIBRATION_VALID) == 0u) {
        throw ProtocolException("MS5611 calibration is not valid")
    }
    requireMeasurementFields(setOf(3, 4))
    val pressure = fields.getValue(3).signedValue()
    val temperature = fields.getValue(4).signedValue()
    if (pressure !in 1_000..120_000) throw ProtocolException("MS5611 pressure is outside supported range")
    if (temperature !in -40_000..85_000) {
        throw ProtocolException("MS5611 temperature is outside plausible range")
    }
    return Ms5611Reading(pressure, temperature)
}

/** Applies the same strict VL53L0X eligibility contract as the production Android client. */
fun SensorSample.asVl53l0xReading(): Vl53l0xReading {
    if (sensorId != SensorId.VL53L0X) throw ProtocolException("sample is not VL53L0X")
    val role = RangefinderRole.fromInstanceId(instanceId)
    val required = SensorValidity.TRANSPORT_VALID or
        SensorValidity.CALIBRATION_VALID or
        SensorValidity.TIMING_VALID or
        SensorValidity.PLAUSIBILITY_VALID
    if ((validityFlags and required) != required) {
        throw ProtocolException("VL53L0X validity flags lack control evidence")
    }
    if ((validityFlags and SensorValidity.CRC_VALID) != 0u ||
        (validityFlags and 0x1Fu.inv()) != 0u
    ) {
        throw ProtocolException("VL53L0X validity flags contain unsupported claims")
    }
    if ((qualityFlags and SensorQuality.FRESH) == 0u ||
        (qualityFlags and 0x07u.inv()) != 0u
    ) {
        throw ProtocolException("VL53L0X quality flags are invalid")
    }
    if (healthFlags != 0u) throw ProtocolException("VL53L0X reports a health fault")
    requireMeasurementFields(setOf(5, 6, 7))
    val distance = fields.getValue(5).unsignedValue()
    val status = fields.getValue(6).unsignedValue()
    val quality = fields.getValue(7).unsignedValue()
    if (distance !in 30u..2_000u) {
        throw ProtocolException("VL53L0X distance is outside 30..2000 mm")
    }
    if (status != 0u && status != 11u) {
        throw ProtocolException("VL53L0X range status is not control-eligible")
    }
    if (quality != 100u) {
        throw ProtocolException("VL53L0X quality must be 100 for valid evidence")
    }
    return Vl53l0xReading(role, distance.toInt(), status.toInt(), quality.toInt())
}

/**
 * Standard-atmosphere pressure altitude using a configurable sea-level pressure (QNH).
 * The default 101325 Pa is useful for bench testing; a local QNH is required for a
 * meaningful altitude-above-mean-sea-level estimate.
 */
object BarometricAltitude {
    fun metersFromPressure(pressurePa: Int, seaLevelPressurePa: Double = 101_325.0): Double {
        if (pressurePa <= 0) throw ProtocolException("pressurePa must be positive")
        if (!seaLevelPressurePa.isFinite() || seaLevelPressurePa < 80_000.0 || seaLevelPressurePa > 110_000.0) {
            throw ProtocolException("sea-level pressure must be in 80000..110000 Pa")
        }
        val ratio = pressurePa.toDouble() / seaLevelPressurePa
        return 44_330.0 * (1.0 - Math.pow(ratio, 0.19029495718363465))
    }
}
