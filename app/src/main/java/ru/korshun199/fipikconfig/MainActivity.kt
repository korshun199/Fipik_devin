package ru.korshun199.fipikconfig

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
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
    private lateinit var statusDot: TextView

    private val navy = Color.rgb(11, 18, 32)
    private val panel = Color.rgb(22, 32, 51)
    private val fieldColor = Color.rgb(31, 44, 67)
    private val orange = Color.rgb(255, 143, 45)
    private val textPrimary = Color.rgb(242, 246, 252)
    private val textSecondary = Color.rgb(157, 174, 198)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = navy
        window.navigationBarColor = navy
        setContentView(createScreen())
    }

    private fun createScreen(): ScrollView {
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(18), dp(20), dp(28))
            setBackgroundColor(navy)
        }

        val header = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val brand = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        brand.addView(text("FIPIK", 30f, textPrimary, Typeface.BOLD))
        brand.addView(text("FLIGHT CONTROLLER", 11f, textSecondary, Typeface.BOLD))
        header.addView(brand, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        statusDot = text("●  OFFLINE", 12f, textSecondary, Typeface.BOLD)
        header.addView(statusDot)
        content.addView(header)
        content.addView(text("Настройка полётного контроллера", 14f, textSecondary, Typeface.NORMAL), margin(0, 4, 0, 18))

        val connection = card()
        connection.addView(sectionTitle("ПОДКЛЮЧЕНИЕ", "Wi‑Fi точка доступа ESP32"))
        host = field("Адрес ESP32", "192.168.4.1")
        connection.addView(host, margin(0, 12, 0, 0))
        content.addView(connection, margin(0, 0, 0, 12))

        val radio = card()
        radio.addView(sectionTitle("ПУЛЬТ", "Каналы приёмника RX900"))
        val radioGrid = grid()
        roll = field("Крен", "0")
        pitch = field("Тангаж", "1")
        throttle = field("Газ", "2")
        yaw = field("Разворот", "3")
        arm = field("ARM", "3")
        listOf(roll, pitch, throttle, yaw, arm).forEachIndexed { index, edit ->
            radioGrid.addView(edit, gridParams(index))
        }
        radio.addView(radioGrid, margin(0, 12, 0, 0))
        content.addView(radio, margin(0, 0, 0, 12))

        val motors = card()
        motors.addView(sectionTitle("ДВИГАТЕЛИ", "Безопасные пределы PWM"))
        val motorGrid = grid()
        armedIdle = field("Холостой ход, мкс", "1100")
        maxSignal = field("Максимум, мкс", "1380")
        motorGrid.addView(armedIdle, gridParams(0))
        motorGrid.addView(maxSignal, gridParams(1))
        motors.addView(motorGrid, margin(0, 12, 0, 0))
        motors.addView(text("Винты перед проверкой снять. Изменения применяются после перезапуска ESP32.", 12f, textSecondary, Typeface.NORMAL), margin(0, 12, 0, 0))
        content.addView(motors, margin(0, 0, 0, 16))

        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val read = actionButton("ПРОЧИТАТЬ", false).apply { setOnClickListener { requestConfig(false) } }
        val save = actionButton("СОХРАНИТЬ", true).apply { setOnClickListener { requestConfig(true) } }
        actions.addView(read, LinearLayout.LayoutParams(0, dp(54), 1f).apply { rightMargin = dp(6) })
        actions.addView(save, LinearLayout.LayoutParams(0, dp(54), 1f).apply { leftMargin = dp(6) })
        content.addView(actions)

        status = text("Готово. Подключи ESP32 к сети Fipik-01.", 13f, textSecondary, Typeface.NORMAL)
        content.addView(status, margin(2, 14, 2, 0))
        return ScrollView(this).apply {
            isFillViewport = true
            addView(content)
        }
    }

    private fun sectionTitle(title: String, subtitle: String): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        box.addView(text(title, 13f, orange, Typeface.BOLD))
        box.addView(text(subtitle, 12f, textSecondary, Typeface.NORMAL), margin(0, 3, 0, 0))
        return box
    }

    private fun card(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(16), dp(16), dp(16), dp(16))
        background = rounded(panel, 16)
    }

    private fun grid(): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        weightSum = 2f
    }

    private fun gridParams(index: Int): LinearLayout.LayoutParams =
        LinearLayout.LayoutParams(0, dp(62), 1f).apply {
            if (index % 2 == 0) rightMargin = dp(5) else leftMargin = dp(5)
            if (index >= 2) topMargin = dp(10)
        }

    private fun field(hint: String, value: String): EditText = EditText(this).apply {
        setHintTextColor(textSecondary)
        setTextColor(textPrimary)
        textSize = 15f
        setSingleLine(true)
        setHint(hint)
        setText(value)
        gravity = Gravity.CENTER_VERTICAL
        inputType = InputType.TYPE_CLASS_NUMBER
        setPadding(dp(12), 0, dp(10), 0)
        background = rounded(fieldColor, 10)
    }

    private fun actionButton(title: String, primary: Boolean): Button = Button(this).apply {
        text = title
        textSize = 12f
        setTextColor(if (primary) navy else textPrimary)
        typeface = Typeface.DEFAULT_BOLD
        isAllCaps = false
        background = rounded(if (primary) orange else fieldColor, 12)
        stateListAnimator = null
    }

    private fun text(value: String, size: Float, color: Int, style: Int): TextView = TextView(this).apply {
        text = value
        textSize = size
        setTextColor(color)
        typeface = Typeface.create(Typeface.DEFAULT, style)
    }

    private fun rounded(color: Int, radius: Int): GradientDrawable = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radius).toFloat()
    }

    private fun margin(left: Int, top: Int, right: Int, bottom: Int): LinearLayout.LayoutParams =
        LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            setMargins(dp(left), dp(top), dp(right), dp(bottom))
        }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private fun requestConfig(save: Boolean) {
        val enteredHost = host.text.toString().trim().removeSuffix("/")
        val base = if (enteredHost.startsWith("http://") || enteredHost.startsWith("https://")) {
            enteredHost
        } else {
            "http://$enteredHost"
        }
        status.text = if (save) "Сохраняю настройки…" else "Читаю настройки…"
        statusDot.text = "●  CONNECTING"
        statusDot.setTextColor(orange)
        executor.execute {
            try {
                val result = if (save) putConfig(base) else getConfig(base)
                runOnUiThread {
                    status.text = result
                    statusDot.text = "●  ONLINE"
                    statusDot.setTextColor(Color.rgb(92, 214, 143))
                }
            } catch (error: Exception) {
                runOnUiThread {
                    status.text = "Ошибка подключения: ${error.message ?: "нет ответа"}"
                    statusDot.text = "●  OFFLINE"
                    statusDot.setTextColor(textSecondary)
                }
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
        return "Настройки сохранены · ESP32 перезапускается"
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
