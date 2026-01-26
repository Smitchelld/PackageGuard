#pragma once

const char* html_page = 
"<!DOCTYPE html>"
"<html>"
"<head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
    "<title>PackageGuard Pro Setup</title>"
    "<style>"
        "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f4f7f9; margin: 0; display: flex; align-items: center; justify-content: center; min-height: 100vh; }"
        ".container { background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 25px rgba(0,0,0,0.1); width: 100%; max-width: 400px; box-sizing: border-box; }"
        "h2 { color: #2c3e50; text-align: center; margin-bottom: 10px; }"
        "p { color: #7f8c8d; font-size: 0.9em; text-align: center; margin-bottom: 25px; }"
        ".status-info { background: #e8f4fd; color: #2980b9; padding: 10px; border-radius: 8px; font-size: 0.85em; margin-bottom: 20px; border-left: 4px solid #3498db; }"
        "label { display: block; margin-bottom: 5px; color: #34495e; font-weight: 600; font-size: 0.9em; }"
        "input[type='text'], input[type='password'] { width: 100%; padding: 12px; margin-bottom: 15px; border: 1px solid #dcdfe6; border-radius: 8px; box-sizing: border-box; transition: border-color 0.3s; }"
        "input:focus { border-color: #3498db; outline: none; }"
        "hr { border: 0; border-top: 1px solid #eee; margin: 20px 0; }"
        "button { width: 100%; padding: 14px; background-color: #3498db; color: white; border: none; border-radius: 8px; font-size: 1em; font-weight: bold; cursor: pointer; transition: background-color 0.3s; }"
        "button:hover { background-color: #2980b9; }"
        ".footer { text-align: center; margin-top: 20px; font-size: 0.75em; color: #bdc3c7; }"
    "</style>"
"</head>"
"<body>"
    "<div class='container'>"
        "<h2>PackageGuard Pro</h2>"
        "<p>Konfiguracja wstępna urządzenia</p>"
        
        "<div class='status-info'>"
            "<strong>Krok 1:</strong> Połącz urządzenie z siecią WiFi i serwerem.<br>"
            "<strong>Krok 2:</strong> Użyj aplikacji mobilnej, aby przypisać właściciela (BLE)."
        "</div>"

        "<form action='/save' method='POST'>"
            "<label>Nazwa sieci WiFi (SSID):</label>"
            "<input type='text' name='ssid' placeholder='Wpisz nazwę sieci' required>"
            
            "<label>Hasło WiFi:</label>"
            "<input type='password' name='pass' placeholder='Wpisz hasło'>"
            
            "<hr>"
            
            "<label>Adres Serwera MQTT:</label>"
            "<input type='text' name='mqtt' value='mqtt://192.168.137.1' required>"
            
            "<button type='submit'>Zapisz i Połącz</button>"
        "</form>"
        
        "<div class='footer'>"
            "ID Urządzenia: Oczekuje na parowanie BLE"
        "</div>"
    "</div>"
"</body>"
"</html>";