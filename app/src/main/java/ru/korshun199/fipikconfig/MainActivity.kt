package ru.korshun199.fipikconfig

import android.app.Activity
import android.os.Bundle
import android.graphics.Color
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors

class MainActivity : Activity() {
    private val executor = Executors.newSingleThreadExecutor()
    private lateinit var host: EditText
    private lateinit var armedIdle: EditText
    private lateinit var maxSignal: EditText
    private lateinit var roll: EditText
    private lateinit var pitch: EditText
    private lateinit var throttle: EditText
    private lateinit var yaw: EditText
    private lateinit var arm: EditText
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(createScreen())
    }

    private fun createScreen(): ScrollView {
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(32, 28, 32, 28)
        }
        content.addView(label("FIPIK / настройки ESP32", 24f))
        content.addView(label("Подключение по Wi‑Fi", 18f))
        host = field("Адрес ESP", "192.168.4.1")
        content.addView(host)

        content.addView(label("Пульт", 18f))
        roll = field("Крен, канал", "0")
        pitch = field("Тангаж, канал", "1")
        throttle = field("Газ, канал", "2")
        yaw = field("Разворот, канал", "3")
        arm = field("ARM, канал", "4")
        listOf(roll, pitch, throttle, yaw, arm).forEach(content::addView)

        content.addView(label("Моторы", 18f))
        armedIdle = field("Медленный ход при ARM, мкс", "1100")
        maxSignal = field("Максимальный сигнал, мкс", "1380")
        content.addView(armedIdle)
        content.addView(maxSignal)

        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val read = Button(this).apply {
            text = "Прочитать"
            setOnClickListener { requestConfig(false) }
        }
        val save = Button(this).apply {
            text = "Сохранить"
            setOnClickListener { requestConfig(true) }
        }
        buttons.addView(read, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        buttons.addView(save, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        content.addView(buttons)
        status = label("Готово. ESP ещё не подключён.", 14f)
        content.addView(status)
        return ScrollView(this).apply { addView(content) }
    }

    private fun label(text: String, size: Float): TextView = TextView(this).apply {
        this.text = text
        textSize = size
        setTextColor(Color.rgb(20, 30, 40))
        setPadding(0, 14, 0, 6)
    }

    private fun field(hint: String, value: String): EditText = EditText(this).apply {
        this.hint = hint
        setText(value)
        inputType = android.text.InputType.TYPE_CLASS_TEXT
    }

    private fun requestConfig(save: Boolean) {
        val base = host.text.toString().trim().removeSuffix("/")
        status.text = if (save) "Сохраняю…" else "Читаю…"
        executor.execute {
            try {
                val result = if (save) putConfig(base) else getConfig(base)
                runOnUiThread { status.text = result }
            } catch (error: Exception) {
                runOnUiThread { status.text = "Ошибка: ${error.message}" }
            }
        }
    }

    private fun getConfig(base: String): String {
        val json = request("$base/api/config", "GET", null)
        val radio = json.getJSONObject("radio")
        val esc = json.getJSONObject("esc")
        runOnUiThread {
            roll.setText(radio.getInt("roll_channel").toString())
            pitch.setText(radio.getInt("pitch_channel").toString())
            throttle.setText(radio.getInt("throttle_channel").toString())
            yaw.setText(radio.getInt("yaw_channel").toString())
            arm.setText(radio.getInt("arm_channel").toString())
            armedIdle.setText(esc.getInt("armed_idle_us").toString())
            maxSignal.setText(esc.getInt("motor_signal_max_us").toString())
        }
        return "Настройки прочитаны"
    }

    private fun putConfig(base: String): String {
        val json = request("$base/api/config", "GET", null)
        json.put("radio", JSONObject().apply {
            put("roll_channel", roll.text.toString().toInt())
            put("pitch_channel", pitch.text.toString().toInt())
            put("throttle_channel", throttle.text.toString().toInt())
            put("yaw_channel", yaw.text.toString().toInt())
            put("arm_channel", arm.text.toString().toInt())
        })
        json.put("esc", JSONObject().apply {
            put("armed_idle_us", armedIdle.text.toString().toInt())
            put("motor_signal_max_us", maxSignal.text.toString().toInt())
        })
        request("$base/api/config", "PUT", json.toString())
        return "Настройки сохранены"
    }

    private fun request(address: String, method: String, body: String?): JSONObject {
        val connection = (URL(address).openConnection() as HttpURLConnection).apply {
            requestMethod = method
            connectTimeout = 3000
            readTimeout = 5000
            setRequestProperty("Content-Type", "application/json")
            doInput = true
            if (body != null) {
                doOutput = true
                outputStream.use { it.write(body.toByteArray()) }
            }
        }
        val response = connection.inputStream.bufferedReader().use { it.readText() }
        connection.disconnect()
        return JSONObject(response)
    }
}
