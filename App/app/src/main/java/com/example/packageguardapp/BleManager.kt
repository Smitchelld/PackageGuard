package com.example.packageguardapp

import android.bluetooth.BluetoothGatt
import java.util.*

object BleManager {
    var gatt: BluetoothGatt? = null
    var connectedMac: String? = null

    fun isConnected(mac: String): Boolean {
        val cleanListMac = mac.replace(":", "").replace("-", "").uppercase().trim()
        return gatt != null && connectedMac == cleanListMac
    }

    // UUIDy (zgodne z Twoim ESP32)
    val SERVICE_UUID = UUID.fromString("c1356e62-fe2c-4861-a28a-446ada22f6d5")
    val CHAR_STATUS_UUID = UUID.fromString("c1356e63-fe2c-4861-a28a-446ada22f6d5")
    val CHAR_CMD_UUID = UUID.fromString("c1356e64-fe2c-4861-a28a-446ada22f6d5")
}