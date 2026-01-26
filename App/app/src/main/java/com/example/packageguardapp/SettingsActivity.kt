package com.example.packageguardapp

import android.annotation.SuppressLint
import android.os.Bundle
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject

class SettingsActivity : AppCompatActivity() {

    @SuppressLint("MissingPermission")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        val mac = intent.getStringExtra("mac") ?: ""

        findViewById<Button>(R.id.btnSaveSettings).setOnClickListener {
            try {
                val cfg = JSONObject().apply {
                    put("s_en", findViewById<Switch>(R.id.swShockEn).isChecked)
                    put("t_en", findViewById<Switch>(R.id.swTempEn).isChecked)
                    put("h_en", findViewById<Switch>(R.id.swHumEn).isChecked)
                    put("p_en", findViewById<Switch>(R.id.swPresEn).isChecked)
                    put("l_en", findViewById<Switch>(R.id.swLightEn).isChecked)
                    put("b_en", findViewById<Switch>(R.id.swBatEn).isChecked)
                    put("buz", findViewById<Switch>(R.id.swBuzzEn).isChecked)
                    put("mot", findViewById<Switch>(R.id.swMotEn).isChecked)
                    put("led", findViewById<Switch>(R.id.swLedEn).isChecked)
                    put("stl", findViewById<Switch>(R.id.swStealth).isChecked)
                    put("shk", findViewById<EditText>(R.id.etShockG).text.toString().toDouble())
                    put("tmn", findViewById<EditText>(R.id.etTempMin).text.toString().toDouble())
                    put("tmx", findViewById<EditText>(R.id.etTempMax).text.toString().toDouble())
                    put("bmn", findViewById<EditText>(R.id.etBatMin).text.toString().toDouble())
                    put("int", findViewById<EditText>(R.id.etInterval).text.toString().toInt())
                }

                if (BleManager.isConnected(mac)) {
                    val cmd = "CFG:$cfg"
                    val service = BleManager.gatt?.getService(BleManager.SERVICE_UUID)
                    val char = service?.getCharacteristic(BleManager.CHAR_CMD_UUID)
                    if (char != null) {
                        char.value = cmd.toByteArray()
                        BleManager.gatt?.writeCharacteristic(char)
                        Toast.makeText(this, "Wysłano przez BLE!", Toast.LENGTH_SHORT).show()
                        finish()
                    }
                } else {
                    Toast.makeText(this, "Brak połączenia BLE. Użyj panelu WWW.", Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                Toast.makeText(this, "Wypełnij wszystkie pola liczbami!", Toast.LENGTH_SHORT).show()
            }
        }
    }
}