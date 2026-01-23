package com.yourname.falldetector

import android.bluetooth.*
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.*

class BLEManager(
    private val context: Context,
    private val device: BluetoothDevice,
    private val onMessageReceived: (String) -> Unit
) {

    companion object {
        private const val TAG = "BLEManager"
        
        // These UUIDs MUST match the ESP32 code!
        private val SERVICE_UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
        private val CHARACTERISTIC_UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")
    }

    private var bluetoothGatt: BluetoothGatt? = null
    private var characteristic: BluetoothGattCharacteristic? = null
    private var isConnected = false
    private val handler = Handler(Looper.getMainLooper())

    private val gattCallback = object : BluetoothGattCallback() {
        
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.d(TAG, "Connected to GATT server")
                    isConnected = true
                    
                    // Discover services
                    handler.postDelayed({
                        try {
                            gatt.discoverServices()
                        } catch (e: SecurityException) {
                            Log.e(TAG, "Permission error during service discovery", e)
                        }
                    }, 600)
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    Log.d(TAG, "Disconnected from GATT server")
                    isConnected = false
                    characteristic = null
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Services discovered")
                
                // Find our service and characteristic
                val service = gatt.getService(SERVICE_UUID)
                if (service != null) {
                    characteristic = service.getCharacteristic(CHARACTERISTIC_UUID)
                    
                    if (characteristic != null) {
                        Log.d(TAG, "Characteristic found!")
                        
                        // Enable notifications
                        try {
                            gatt.setCharacteristicNotification(characteristic, true)
                            
                            // Enable notification descriptor
                            val descriptor = characteristic!!.getDescriptor(
                                UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
                            )
                            if (descriptor != null) {
                                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                                gatt.writeDescriptor(descriptor)
                                Log.d(TAG, "Notifications enabled")
                            }
                        } catch (e: SecurityException) {
                            Log.e(TAG, "Permission error enabling notifications", e)
                        }
                    } else {
                        Log.e(TAG, "Characteristic not found!")
                    }
                } else {
                    Log.e(TAG, "Service not found!")
                }
            } else {
                Log.e(TAG, "Service discovery failed: $status")
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            // This is called when ESP32 sends a notification
            val data = characteristic.value
            if (data != null && data.isNotEmpty()) {
                val message = String(data, Charsets.UTF_8)
                Log.d(TAG, "Received: $message")
                onMessageReceived(message)
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Write successful")
            } else {
                Log.e(TAG, "Write failed: $status")
            }
        }
    }

    fun connect(callback: (Boolean) -> Unit) {
        try {
            bluetoothGatt = device.connectGatt(context, false, gattCallback)
            
            // Wait a bit then check if connected
            handler.postDelayed({
                callback(isConnected)
            }, 2000)
            
        } catch (e: SecurityException) {
            Log.e(TAG, "Permission error during connection", e)
            callback(false)
        } catch (e: Exception) {
            Log.e(TAG, "Connection error", e)
            callback(false)
        }
    }

    fun disconnect() {
        try {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
            bluetoothGatt = null
            isConnected = false
            characteristic = null
        } catch (e: SecurityException) {
            Log.e(TAG, "Permission error during disconnection", e)
        }
    }

    fun sendMessage(message: String): Boolean {
        if (!isConnected || characteristic == null || bluetoothGatt == null) {
            Log.e(TAG, "Cannot send message - not connected or characteristic is null")
            return false
        }

        try {
            characteristic!!.value = message.toByteArray(Charsets.UTF_8)
            val success = bluetoothGatt!!.writeCharacteristic(characteristic)
            
            if (success) {
                Log.d(TAG, "Sending message: $message")
            } else {
                Log.e(TAG, "Failed to send message")
            }
            
            return success
            
        } catch (e: SecurityException) {
            Log.e(TAG, "Permission error sending message", e)
            return false
        } catch (e: Exception) {
            Log.e(TAG, "Error sending message", e)
            return false
        }
    }

    fun isConnected(): Boolean = isConnected
}