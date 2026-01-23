package com.yourname.falldetector

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.speech.tts.TextToSpeech
import android.telephony.SmsManager
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.text.SimpleDateFormat
import java.util.*

class MainActivity : AppCompatActivity(), TextToSpeech.OnInitListener {

    // UI Elements
    private lateinit var tvStatus: TextView
    private lateinit var btnConnect: Button
    private lateinit var btnDisconnect: Button
    private lateinit var etPhoneNumber: EditText
    private lateinit var btnSaveContact: Button
    private lateinit var tvMessages: TextView
    private lateinit var btnTestCall: Button
    private lateinit var btnTestSMS: Button

    // Bluetooth
    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bleManager: BLEManager? = null
    
    // TTS
    private var tts: TextToSpeech? = null
    
    // Storage
    private lateinit var prefs: SharedPreferences
    
    // Emergency contact
    private var emergencyContact: String = "+919876543210"

    // Permissions
    private val PERMISSION_REQUEST_CODE = 100
    private val REQUIRED_PERMISSIONS = arrayOf(
        Manifest.permission.BLUETOOTH,
        Manifest.permission.BLUETOOTH_ADMIN,
        Manifest.permission.ACCESS_FINE_LOCATION,
        Manifest.permission.CALL_PHONE,
        Manifest.permission.SEND_SMS
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // Initialize UI
        initializeUI()

        // Initialize Bluetooth
        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = bluetoothManager.adapter

        // Initialize TTS
        tts = TextToSpeech(this, this)

        // Initialize SharedPreferences
        prefs = getSharedPreferences("FallDetectorPrefs", Context.MODE_PRIVATE)

        // Load saved emergency contact
        emergencyContact = prefs.getString("emergency_contact", "+919876543210") ?: "+919876543210"
        etPhoneNumber.setText(emergencyContact)

        // Check permissions
        checkPermissions()

        // Setup listeners
        setupListeners()

        addMessage("App started")
    }

    private fun initializeUI() {
        tvStatus = findViewById(R.id.tvStatus)
        btnConnect = findViewById(R.id.btnConnect)
        btnDisconnect = findViewById(R.id.btnDisconnect)
        etPhoneNumber = findViewById(R.id.etPhoneNumber)
        btnSaveContact = findViewById(R.id.btnSaveContact)
        tvMessages = findViewById(R.id.tvMessages)
        btnTestCall = findViewById(R.id.btnTestCall)
        btnTestSMS = findViewById(R.id.btnTestSMS)

        tvMessages.text = ""
    }

    private fun setupListeners() {
        btnConnect.setOnClickListener {
            connectToDevice()
        }

        btnDisconnect.setOnClickListener {
            disconnectDevice()
        }

        btnSaveContact.setOnClickListener {
            saveEmergencyContact()
        }

        btnTestCall.setOnClickListener {
            testEmergencyCall()
        }

        btnTestSMS.setOnClickListener {
            testEmergencySMS()
        }
    }

    private fun checkPermissions() {
        val permissionsToRequest = mutableListOf<String>()

        for (permission in REQUIRED_PERMISSIONS) {
            if (ContextCompat.checkSelfPermission(this, permission) 
                != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(permission)
            }
        }

        // Add Android 12+ specific permissions
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN)
                != PackageManager.PERMISSION_GRANTED) {
                permissionsToRequest.add(Manifest.permission.BLUETOOTH_SCAN)
            }
        }

        if (permissionsToRequest.isNotEmpty()) {
            ActivityCompat.requestPermissions(
                this,
                permissionsToRequest.toTypedArray(),
                PERMISSION_REQUEST_CODE
            )
        }
    }

    private fun connectToDevice() {
        if (bluetoothAdapter == null) {
            Toast.makeText(this, "Bluetooth not available", Toast.LENGTH_SHORT).show()
            return
        }

        if (!bluetoothAdapter!!.isEnabled) {
            Toast.makeText(this, "Please enable Bluetooth", Toast.LENGTH_SHORT).show()
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            startActivityForResult(enableBtIntent, 1)
            return
        }

        // Get paired devices
        val pairedDevices: Set<BluetoothDevice>? = try {
            bluetoothAdapter!!.bondedDevices
        } catch (e: SecurityException) {
            Toast.makeText(this, "Bluetooth permission denied", Toast.LENGTH_SHORT).show()
            return
        }

        if (pairedDevices.isNullOrEmpty()) {
            Toast.makeText(this, "No paired devices found", Toast.LENGTH_SHORT).show()
            return
        }

        // Find "FallDetector" device
        var targetDevice: BluetoothDevice? = null
        for (device in pairedDevices) {
            if (device.name == "FallDetector") {
                targetDevice = device
                break
            }
        }

        if (targetDevice == null) {
            // Show dialog to select device
            val deviceNames = pairedDevices.map { it.name }.toTypedArray()
            AlertDialog.Builder(this)
                .setTitle("Select Device")
                .setItems(deviceNames) { _, which ->
                    val selectedDevice = pairedDevices.elementAt(which)
                    connectToBLEDevice(selectedDevice)
                }
                .show()
        } else {
            connectToBLEDevice(targetDevice)
        }
    }

    private fun connectToBLEDevice(device: BluetoothDevice) {
        addMessage("Connecting to ${device.name}...")
        
        bleManager = BLEManager(this, device) { message ->
            runOnUiThread {
                handleBLEMessage(message)
            }
        }

        bleManager?.connect { success ->
            runOnUiThread {
                if (success) {
                    updateConnectionStatus(true)
                    addMessage("Connected to ${device.name}")
                    speak("Connected to fall detector")
                } else {
                    updateConnectionStatus(false)
                    addMessage("Connection failed")
                    Toast.makeText(this, "Connection failed", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun disconnectDevice() {
        bleManager?.disconnect()
        updateConnectionStatus(false)
        addMessage("Disconnected")
        speak("Disconnected")
    }

    private fun updateConnectionStatus(connected: Boolean) {
        if (connected) {
            tvStatus.text = "Connected"
            tvStatus.setTextColor(ContextCompat.getColor(this, android.R.color.holo_green_dark))
            btnConnect.isEnabled = false
            btnDisconnect.isEnabled = true
        } else {
            tvStatus.text = "Not Connected"
            tvStatus.setTextColor(ContextCompat.getColor(this, android.R.color.holo_red_dark))
            btnConnect.isEnabled = true
            btnDisconnect.isEnabled = false
        }
    }

    private fun handleBLEMessage(message: String) {
        addMessage("ESP32: $message")

        // Parse JSON message
        try {
            if (message.contains("\"type\"")) {
                when {
                    message.contains("\"fall_detected\"") -> {
                        handleFallDetected()
                    }
                    message.contains("\"countdown\"") -> {
                        val countdown = extractCountdown(message)
                        handleCountdown(countdown)
                    }
                    message.contains("\"emergency_call\"") -> {
                        handleEmergencyCall()
                    }
                    message.contains("\"canceled\"") -> {
                        handleCanceled()
                    }
                    message.contains("\"monitoring_resumed\"") -> {
                        speak("Monitoring resumed")
                    }
                }
            }
        } catch (e: Exception) {
            addMessage("Error parsing message: ${e.message}")
        }
    }

    private fun extractCountdown(message: String): Int {
        // Extract countdown value from JSON
        val regex = "\"countdown\":(\\d+)".toRegex()
        val match = regex.find(message)
        return match?.groupValues?.get(1)?.toInt() ?: 0
    }

    private fun handleFallDetected() {
        addMessage("⚠️ FALL DETECTED!")
        speak("Fall detected! Emergency call will be made in 15 seconds. Press cancel button on device to stop.")
        
        // Show notification
        Toast.makeText(this, "FALL DETECTED! Countdown started", Toast.LENGTH_LONG).show()
    }

    private fun handleCountdown(seconds: Int) {
        if (seconds % 5 == 0 && seconds > 0) {  // Announce every 5 seconds
            speak("Emergency call in $seconds seconds")
        }
    }

    private fun handleEmergencyCall() {
        addMessage("🚨 EMERGENCY CALL ACTIVATED!")
        speak("Emergency! Calling contact now")

        // Make call
        makeEmergencyCall()

        // Send SMS with location
        sendEmergencySMS()

        // Send acknowledgment to ESP32
        bleManager?.sendMessage("ACK:CALLING")
    }

    private fun handleCanceled() {
        addMessage("✅ Emergency canceled by user")
        speak("Emergency canceled")
        
        bleManager?.sendMessage("ACK:CANCELED")
    }

    private fun makeEmergencyCall() {
        try {
            val callIntent = Intent(Intent.ACTION_CALL)
            callIntent.data = Uri.parse("tel:$emergencyContact")
            startActivity(callIntent)
            addMessage("Calling $emergencyContact...")
        } catch (e: SecurityException) {
            addMessage("Call permission denied")
            Toast.makeText(this, "Permission required to make calls", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            addMessage("Call failed: ${e.message}")
        }
    }

    private fun sendEmergencySMS() {
        try {
            val location = "Unknown"  // TODO: Get actual GPS location
            val message = "EMERGENCY ALERT! Fall detected at ${getCurrentTime()}. Location: $location. Please check on me immediately!"

            val smsManager = SmsManager.getDefault()
            smsManager.sendTextMessage(emergencyContact, null, message, null, null)

            addMessage("SMS sent to $emergencyContact")
        } catch (e: SecurityException) {
            addMessage("SMS permission denied")
        } catch (e: Exception) {
            addMessage("SMS failed: ${e.message}")
        }
    }

    private fun saveEmergencyContact() {
        emergencyContact = etPhoneNumber.text.toString()
        
        if (emergencyContact.isEmpty()) {
            Toast.makeText(this, "Please enter a phone number", Toast.LENGTH_SHORT).show()
            return
        }

        prefs.edit().putString("emergency_contact", emergencyContact).apply()
        
        Toast.makeText(this, "Contact saved", Toast.LENGTH_SHORT).show()
        speak("Emergency contact saved")
        addMessage("Emergency contact updated: $emergencyContact")
    }

    private fun testEmergencyCall() {
        if (emergencyContact.isEmpty()) {
            Toast.makeText(this, "Please set emergency contact first", Toast.LENGTH_SHORT).show()
            return
        }

        AlertDialog.Builder(this)
            .setTitle("Test Emergency Call")
            .setMessage("This will call $emergencyContact. Continue?")
            .setPositiveButton("Yes") { _, _ ->
                makeEmergencyCall()
            }
            .setNegativeButton("No", null)
            .show()
    }

    private fun testEmergencySMS() {
        if (emergencyContact.isEmpty()) {
            Toast.makeText(this, "Please set emergency contact first", Toast.LENGTH_SHORT).show()
            return
        }

        AlertDialog.Builder(this)
            .setTitle("Test Emergency SMS")
            .setMessage("This will send test SMS to $emergencyContact. Continue?")
            .setPositiveButton("Yes") { _, _ ->
                try {
                    val message = "TEST: This is a test emergency alert from Fall Detector app. Time: ${getCurrentTime()}"
                    val smsManager = SmsManager.getDefault()
                    smsManager.sendTextMessage(emergencyContact, null, message, null, null)
                    Toast.makeText(this, "Test SMS sent", Toast.LENGTH_SHORT).show()
                } catch (e: Exception) {
                    Toast.makeText(this, "SMS failed: ${e.message}", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("No", null)
            .show()
    }

    private fun addMessage(message: String) {
        val timestamp = getCurrentTime()
        val logMessage = "[$timestamp] $message\n"
        tvMessages.append(logMessage)

        // Auto-scroll to bottom
        val scrollView = tvMessages.parent as? android.widget.ScrollView
        scrollView?.post {
            scrollView.fullScroll(android.view.View.FOCUS_DOWN)
        }
    }

    private fun getCurrentTime(): String {
        val sdf = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
        return sdf.format(Date())
    }

    private fun speak(text: String) {
        tts?.speak(text, TextToSpeech.QUEUE_ADD, null, null)
    }

    // TextToSpeech callback
    override fun onInit(status: Int) {
        if (status == TextToSpeech.SUCCESS) {
            tts?.language = Locale.US
            addMessage("Text-to-speech initialized")
        } else {
            addMessage("TTS initialization failed")
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        bleManager?.disconnect()
        tts?.stop()
        tts?.shutdown()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        
        if (requestCode == PERMISSION_REQUEST_CODE) {
            val allGranted = grantResults.all { it == PackageManager.PERMISSION_GRANTED }
            if (allGranted) {
                Toast.makeText(this, "All permissions granted", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Some permissions denied", Toast.LENGTH_SHORT).show()
            }
        }
    }
}