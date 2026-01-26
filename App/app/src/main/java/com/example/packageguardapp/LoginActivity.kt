package com.example.packageguardapp

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import retrofit2.Call
import retrofit2.Callback
import retrofit2.Response
import retrofit2.Retrofit
import retrofit2.converter.gson.GsonConverterFactory
import androidx.core.net.toUri
import androidx.core.content.edit

class LoginActivity : AppCompatActivity() {
    private lateinit var api: ApiService

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_login)

        val prefs = getSharedPreferences("AppPrefs", MODE_PRIVATE)
        if (prefs.contains("username")) {
            startActivity(Intent(this, MainActivity::class.java))
            finish()
            return
        }

        api = Retrofit.Builder()
            .baseUrl("http://192.168.137.1:5000")
            .addConverterFactory(GsonConverterFactory.create())
            .build().create(ApiService::class.java)

        findViewById<Button>(R.id.btnLogin).setOnClickListener {
            val login = findViewById<EditText>(R.id.etLogin).text.toString()
            val pass = findViewById<EditText>(R.id.etPass).text.toString()

            if (login.isNotEmpty() && pass.isNotEmpty()) {
                doLogin(login, pass)
            } else {
                Toast.makeText(this, "Wpisz login i hasło", Toast.LENGTH_SHORT).show()
            }
        }

        findViewById<Button>(R.id.btnRegister).setOnClickListener {
            val browserIntent = Intent(Intent.ACTION_VIEW,
                "http://192.168.137.1:5000/register".toUri())
            startActivity(browserIntent)
        }
    }

    private fun doLogin(l: String, p: String) {
        // Wywołanie API
        api.loginUser(l, p).enqueue(object : Callback<LoginResponse> {
            override fun onResponse(call: Call<LoginResponse>, response: Response<LoginResponse>) {
                val body = response.body()

                if (response.isSuccessful && body != null && body.status == "success") {
                    val prefs = getSharedPreferences("AppPrefs", MODE_PRIVATE)
                    prefs.edit { putString("username", body.user ?: l) }

                    Toast.makeText(this@LoginActivity, "Zalogowano!", Toast.LENGTH_SHORT).show()

                    startActivity(Intent(this@LoginActivity, MainActivity::class.java))
                    finish()
                } else {
                    val errorMsg = body?.message ?: "Błąd logowania"
                    Toast.makeText(this@LoginActivity, errorMsg, Toast.LENGTH_SHORT).show()
                }
            }

            override fun onFailure(call: Call<LoginResponse>, t: Throwable) {
                Toast.makeText(this@LoginActivity, "Błąd połączenia: ${t.message}", Toast.LENGTH_SHORT).show()
            }
        })
    }
}