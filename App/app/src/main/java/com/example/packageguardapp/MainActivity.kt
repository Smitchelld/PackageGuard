package com.example.packageguardapp
import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.*
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import retrofit2.*
import retrofit2.converter.gson.GsonConverterFactory

class MainActivity : AppCompatActivity() {
    private lateinit var adapter: DeviceAdapter
    private lateinit var api: ApiService
    private var bluetoothAdapter: BluetoothAdapter? = null
    private var isScanning = false
    private val handler = Handler(Looper.getMainLooper())
    private var currentUser: String = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // 1. Zarządzanie sesją użytkownika
        val prefs = getSharedPreferences("AppPrefs", MODE_PRIVATE)
        currentUser = prefs.getString("username", null) ?: run {
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
            return
        }
        findViewById<TextView>(R.id.tvUserHeader).text = "Witaj, $currentUser"

        // 2. Setup Bluetooth
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = bluetoothManager.adapter

        // 3. Setup Listy (RecyclerView)
        val rv = findViewById<RecyclerView>(R.id.recyclerView)
        rv.layoutManager = LinearLayoutManager(this)
        adapter = DeviceAdapter(emptyList(),
            onCommand = { mac, cmd -> handleHybridCommand(mac, cmd) },
            onSettings = { device ->
                val i = Intent(this, SettingsActivity::class.java)
                i.putExtra("mac", device.mac)
                startActivity(i)
            }
        )
        rv.adapter = adapter

        // 4. API Retrofit (Upewnij się, że IP serwera jest poprawne)
        api = Retrofit.Builder()
            .baseUrl("http://192.168.137.1:5000")
            .addConverterFactory(GsonConverterFactory.create())
            .build().create(ApiService::class.java)

        // 5. Przyciski interfejsu
        findViewById<Button>(R.id.btnScan).setOnClickListener {
            if (hasPermissions()) startBleScan() else requestPermissions()
        }

        findViewById<Button>(R.id.btnLogout).setOnClickListener {
            prefs.edit().remove("username").apply()
            disconnectBle()
            startActivity(Intent(this, LoginActivity::class.java))
            finish()
        }

        // 6. Start pętli odświeżania danych z chmury
        startDataRefreshLoop()
    }

    private fun startDataRefreshLoop() {
        handler.post(object : Runnable {
            override fun run() {
                fetchDevicesFromServer()
                handler.postDelayed(this, 3000)
            }
        })
    }

    // --- LOGIKA HYBRYDOWA ---
    private fun handleHybridCommand(mac: String, cmd: String) {
        if (BleManager.isConnected(mac)) {
            sendBleCommand(cmd)
        } else {
            sendCloudCommand(mac, cmd)
        }
    }

    @SuppressLint("MissingPermission")
    private fun sendBleCommand(cmd: String) {
        val service = BleManager.gatt?.getService(BleManager.SERVICE_UUID)
        val characteristic = service?.getCharacteristic(BleManager.CHAR_CMD_UUID)
        if (characteristic != null) {
            characteristic.value = cmd.toByteArray()
            BleManager.gatt?.writeCharacteristic(characteristic)
            Toast.makeText(this, "Wysłano przez Bluetooth", Toast.LENGTH_SHORT).show()
        } else {
            Toast.makeText(this, "Błąd serwisu BLE", Toast.LENGTH_SHORT).show()
        }
    }

    private fun sendCloudCommand(mac: String, cmd: String) {
        api.sendCommand(mac, cmd, currentUser).enqueue(object : Callback<Void> {
            override fun onResponse(call: Call<Void>, res: Response<Void>) {
                Toast.makeText(this@MainActivity, "Wysłano (Chmura)", Toast.LENGTH_SHORT).show()
            }
            override fun onFailure(call: Call<Void>, t: Throwable) {
                Toast.makeText(this@MainActivity, "Brak połączenia z serwerem!", Toast.LENGTH_SHORT).show()
            }
        })
    }

    // --- OBSŁUGA BLUETOOTH LE ---

    @SuppressLint("MissingPermission")
    private fun startBleScan() {
        if (isScanning) return
        if (bluetoothAdapter == null || !bluetoothAdapter!!.isEnabled) {
            Toast.makeText(this, "Włącz Bluetooth!", Toast.LENGTH_SHORT).show()
            return
        }
        isScanning = true
        Toast.makeText(this, "Szukanie paczki (kliknij guzik na ESP)...", Toast.LENGTH_SHORT).show()
        handler.postDelayed({ stopScan() }, 10000)
        bluetoothAdapter?.bluetoothLeScanner?.startScan(scanCallback)
    }

    @SuppressLint("MissingPermission")
    private fun stopScan() {
        isScanning = false
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.device.name ?: ""
            if (name == "PKG_PAIR" || name.startsWith("Guard")) {
                stopScan()
                connectToDevice(result.device)
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice) {
        device.connectGatt(this, false, gattCallback)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val bleMac = gatt.device.address.replace(":", "").uppercase().trim()

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                // Konwersja BLE MAC -> WiFi MAC (odejmujemy 2)
                val wifiMac = try {
                    val macLong = bleMac.toLong(16)
                    val wifiLong = macLong - 2
                    wifiLong.toString(16).uppercase().padStart(12, '0')
                } catch (e: Exception) { bleMac }

                BleManager.gatt = gatt
                BleManager.connectedMac = wifiMac

                Log.d("BLE", "Połączono. BLE: $bleMac -> WIFI: $wifiMac")
                gatt.requestMtu(512) // Poproś o duże MTU dla JSONów
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                BleManager.gatt?.close()
                BleManager.gatt = null
                BleManager.connectedMac = null
                runOnUiThread { adapter.notifyDataSetChanged() }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                gatt.discoverServices()
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(BleManager.SERVICE_UUID)
                val char = service?.getCharacteristic(BleManager.CHAR_CMD_UUID)
                if (char != null) {
                    // Automatyczne parowanie użytkownika
                    val cmd = "PAIR:$currentUser"
                    char.value = cmd.toByteArray()
                    gatt.writeCharacteristic(char)

                    runOnUiThread {
                        Toast.makeText(this@MainActivity, "Sparowano lokalnie!", Toast.LENGTH_SHORT).show()
                        adapter.notifyDataSetChanged()
                        claimDeviceOnServer(BleManager.connectedMac ?: "")
                    }
                }
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun disconnectBle() {
        BleManager.gatt?.disconnect()
        BleManager.gatt?.close()
        BleManager.gatt = null
        BleManager.connectedMac = null
    }

    // --- API SERVER CALLS ---

    private fun fetchDevicesFromServer() {
        api.getDevices(currentUser).enqueue(object : Callback<List<DeviceData>> {
            override fun onResponse(call: Call<List<DeviceData>>, response: Response<List<DeviceData>>) {
                if (response.isSuccessful) {
                    response.body()?.let { adapter.update(it) }
                }
            }
            override fun onFailure(call: Call<List<DeviceData>>, t: Throwable) {
                Log.e("API", "Błąd pobierania listy: ${t.message}")
            }
        })
    }

    private fun claimDeviceOnServer(wifiMac: String) {
        api.claimDeviceMobile(wifiMac, "Paczka Pro", currentUser).enqueue(object : Callback<Void> {
            override fun onResponse(call: Call<Void>, response: Response<Void>) {
                fetchDevicesFromServer()
            }
            override fun onFailure(call: Call<Void>, t: Throwable) {}
        })
    }

    // --- UPRAWNIENIA (Android 12+) ---

    private fun hasPermissions(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
                    ActivityCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
                    ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        } else {
            ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestPermissions() {
        val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        ActivityCompat.requestPermissions(this, perms, 1)
    }
}