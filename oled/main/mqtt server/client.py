import paho.mqtt.client as mqtt
import json
import time

# --- KONFIGURACJA (ZMIEŃ TO!) ---
USER_ID = "TUTAJ_WPISZ_TWOJE_UID_Z_FORMULARZA"
MAC_SUFFIX = "TUTAJ_WPISZ_3_OSTATNIE_CZLONY_MAC_Z_EKRANU"  # Np. 4A5B6C
# Jeśli nie znasz MAC, skrypt wypisze go po odebraniu pierwszej wiadomości

BROKER = "broker.hivemq.com"
PORT = 1883

# Tematy (zgodne z kodem C)
TOPIC_DATA = f"packageguard/{USER_ID}/+/data"
TOPIC_CMD_BASE = f"packageguard/{USER_ID}"  # Suffix /MAC/cmd dodamy dynamicznie


def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ Połączono z brokerem HiveMQ!")
        client.subscribe(TOPIC_DATA)
        print(f"📡 Subskrypcja tematu: {TOPIC_DATA}")
        print("Czekam na dane z urządzenia...")
    else:
        print(f"❌ Błąd połączenia, kod: {rc}")


def on_message(client, userdata, msg):
    try:
        # Wyciągamy MAC z tematu: packageguard/user/MAC/data
        parts = msg.topic.split('/')
        device_mac = parts[2]

        payload = json.loads(msg.payload.decode())

        print("\n" + "=" * 30)
        print(f"📦 ODEBRANO DANE [ID: {device_mac}]")
        print(f"🏠 Status: {'UZBROJONY' if payload['armed'] else 'ROZBROJONY'}")
        print(f"🚨 Licznik naruszeń: {payload['alarms']}")
        print(f"💡 Światło: {payload['lux']} lx")
        print(f"🌡 Temperatura: {payload['temp']}°C")
        print(f"🔄 Przeciążenie: {payload['acc']} g")
        print("=" * 30)
        print("Wpisz 'ARM', 'DISARM' lub 'EXIT': ", end='', flush=True)

        # Zapisujemy MAC, żeby wiedzieć gdzie wysłać komendy
        userdata['last_mac'] = device_mac
    except Exception as e:
        print(f"Błąd parsowania: {e}")


# Stan dla przechowywania MAC adresu
state = {'last_mac': None}

client = mqtt.Client(userdata=state)
client.on_connect = on_connect
client.on_message = on_message

client.connect(BROKER, PORT, 60)
client.loop_start()

try:
    while True:
        cmd = input("Wpisz komendę (ARM / DISARM / EXIT): ").strip().upper()

        if cmd == "EXIT":
            break

        if state['last_mac'] is None:
            print("⚠️ Nie znam jeszcze adresu MAC Twojego urządzenia. Poczekaj na pierwszą wiadomość z ESP32.")
            continue

        if cmd in ["ARM", "DISARM"]:
            # Budujemy temat: packageguard/user/MAC/cmd
            target_topic = f"packageguard/{USER_ID}/{state['last_mac']}/cmd"
            # Budujemy JSON: {"set":"ARM"}
            msg = json.dumps({"set": cmd})
            client.publish(target_topic, msg)
            print(f"📤 Wysłano {cmd} do {target_topic}")
        else:
            print("❌ Nieznana komenda.")

except KeyboardInterrupt:
    pass

client.loop_stop()
client.disconnect()