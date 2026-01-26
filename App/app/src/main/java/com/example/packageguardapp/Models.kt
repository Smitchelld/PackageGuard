package com.example.packageguardapp

import com.google.gson.annotations.SerializedName

data class DeviceData(
    val mac: String,
    val name: String,
    val last_data: Telemetry?,
    val last_alarms: List<AlarmEvent>?,
    val owner_id: String
)

data class Telemetry(
    val temp: Double,
    val hum: Double,
    val pres: Double,
    val bat: Double,
    val lux: Double,
    val g: Double,
    val armed: Int
)

data class AlarmEvent(
    val ts: String,
    val type: String,
    @SerializedName("val") val alarmValue: Double
)

data class LoginResponse(
    val status: String,
    val user: String?,
    val message: String?
)