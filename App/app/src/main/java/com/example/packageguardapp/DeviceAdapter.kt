package com.example.packageguardapp

import android.graphics.Color
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView
import java.util.*

class DeviceAdapter(
    private var devices: List<DeviceData>,
    private val onCommand: (String, String) -> Unit,
    private val onSettings: (DeviceData) -> Unit
) : RecyclerView.Adapter<DeviceAdapter.ViewHolder>() {

    class ViewHolder(v: View) : RecyclerView.ViewHolder(v) {
        val name: TextView = v.findViewById(R.id.deviceName)
        val mac: TextView = v.findViewById(R.id.deviceMac)
        val status: TextView = v.findViewById(R.id.statusText)

        // Pola telemetrii
        val txtTemp: TextView = v.findViewById(R.id.txtTemp)
        val txtHum: TextView = v.findViewById(R.id.txtHum)
        val txtBat: TextView = v.findViewById(R.id.txtBat)
        val txtPres: TextView = v.findViewById(R.id.txtPres)
        val txtLux: TextView = v.findViewById(R.id.txtLux)
        val txtShock: TextView = v.findViewById(R.id.txtShock)

        // Historia alarmów
        val txtAlarms: TextView = v.findViewById(R.id.txtAlarmsHistory)

        // Przyciski
        val btnArm: Button = v.findViewById(R.id.btnArm)
        val btnDisarm: Button = v.findViewById(R.id.btnDisarm)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val v = LayoutInflater.from(parent.context).inflate(R.layout.device_item, parent, false)
        return ViewHolder(v)
    }

    override fun onBindViewHolder(h: ViewHolder, position: Int) {
        val d = devices[position]
        h.name.text = d.name
        h.mac.text = d.mac

        // 1. WYŚWIETLANIE TELEMETRII (2 miejsca po przecinku)
        if (d.last_data != null) {
            val data = d.last_data
            h.txtTemp.text = "Temp: ${"%.2f".format(data.temp)}°C"
            h.txtHum.text = "Wilg: ${"%.2f".format(data.hum)}%"
            h.txtBat.text = "Bat: ${"%.2f".format(data.bat)}V"
            h.txtPres.text = "Ciśn: ${"%.2f".format(data.pres)}hPa"
            h.txtLux.text = "Lux: ${"%.2f".format(data.lux)}lx"
            h.txtShock.text = "Shock: ${"%.2f".format(data.g)}g"
        } else {
            // Brak danych
            listOf(h.txtTemp, h.txtHum, h.txtBat, h.txtPres, h.txtLux, h.txtShock).forEach { it.text = "--" }
        }

        // 2. WYŚWIETLANIE HISTORII ALARMÓW
        val alarmBuilder = StringBuilder()
        if (!d.last_alarms.isNullOrEmpty()) {
            d.last_alarms.forEach { alarm ->
                // Skracamy nazwę ALARM_SHOCK -> SHOCK
                val cleanType = alarm.type.replace("ALARM_", "")
                // Format: DATA | TYP | WARTOŚĆ
                alarmBuilder.append("${alarm.ts} | $cleanType | ${"%.2f".format(alarm.alarmValue)}\n")
            }
            h.txtAlarms.text = alarmBuilder.toString().trim()
            h.txtAlarms.setTextColor(Color.RED)
        } else {
            h.txtAlarms.text = "Brak zarejestrowanych alarmów"
            h.txtAlarms.setTextColor(Color.GRAY)
        }

        // 3. LOGIKA STATUSU (HYBRYDOWA)
        if (BleManager.isConnected(d.mac)) {
            h.status.text = "POŁĄCZONY (BLE)"
            h.status.setTextColor(Color.BLUE)
        } else {
            val isArmed = d.last_data?.armed == 1
            h.status.text = if (isArmed) "UZBROJONY" else "CZUWANIE"
            h.status.setTextColor(if (isArmed) Color.RED else Color.parseColor("#2E7D32"))
        }

        // 4. OBSŁUGA PRZYCISKÓW
        h.btnArm.setOnClickListener { onCommand(d.mac, "ARM") }
        h.btnDisarm.setOnClickListener { onCommand(d.mac, "DISARM") }

        // Kliknięcie w kafelek otwiera ustawienia
        h.itemView.setOnClickListener { onSettings(d) }
    }

    override fun getItemCount() = devices.size

    fun update(newList: List<DeviceData>) {
        devices = newList
        notifyDataSetChanged()
    }
}