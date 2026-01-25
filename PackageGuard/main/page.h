#pragma once

const char* html_page = 
"<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:Arial;text-align:center;margin-top:20px;} input{padding:8px;margin:5px;width:90%;box-sizing:border-box;} button{padding:10px 20px;background:#007bff;color:white;border:none;}</style></head><body>"
"<h2>PackageGuard Configuration</h2>"
"<form action='/save' method='POST'>"
"<label>WiFi SSID:</label><br><input type='text' name='ssid' placeholder='Network Name'><br>"
"<label>WiFi Password:</label><br><input type='password' name='pass' placeholder='Password'><br>"
"<hr>"
"<label>User ID / Token:</label><br><input type='text' name='uid' placeholder='Ex. user_123'><br>"
"<label>MQTT Address:</label><br><input type='text' name='mqtt' placeholder='Ex. 192.168.1.15'><br>"
"<br><button type='submit'>Save and Restart</button>"
"</form></body></html>";