import paho.mqtt.client as mqtt
import json
import sys
import datetime

# --- KONFIGURACJA ---
BROKER = "192.168.137.1"
PORT = 1883
TOPIC_ROOT = "packageguard/#"

current_device_id = None


# --- KOLORY ---
class Col:
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    RED = '\033[91m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    BOLD = '\033[1m'
    RESET = '\033[0m'


def get_readable_time(ts):
    """Konwertuje timestamp Unix na czytelną godzinę"""
    if ts is None:
        return "--:--:--"
    try:
        return datetime.datetime.fromtimestamp(int(ts)).strftime('%H:%M:%S')
    except:
        return "Błąd czasu"


def safe_float(value, default=0.0):
    """Bezpieczna konwersja na float - chroni przed None (null)"""
    if value is None:
        return default
    try:
        return float(value)
    except:
        return default


def on_connect(client, userdata, flags, rc):
    print(f"{Col.GREEN}✅ POŁĄCZONO Z BROKEREM (Kod: {rc}){Col.RESET}")
    print(f"📡 Nasłuchuję na: {TOPIC_ROOT}")
    print(f"{Col.YELLOW}STEROWANIE:{Col.RESET} 'a' (Uzbrój), 'd' (Rozbrój), 'q' (Wyjście)")
    print("=" * 60)
    client.subscribe(TOPIC_ROOT)


def on_message(client, userdata, msg):
    global current_device_id
    topic = msg.topic

    try:
        raw_payload = msg.payload.decode('utf-8')
        data = json.loads(raw_payload)
    except json.JSONDecodeError:
        return

    # Rozbicie tematu
    parts = topic.split('/')
    if len(parts) >= 4:
        owner = parts[1]      # <-- tu jest user / owner
        device_id = parts[2]  # <-- tu jest MAC / ID urządzenia
        category = parts[3]
        current_device_id = device_id
    else:
        return

    packet_time = get_readable_time(data.get('ts'))

    # --- DANE (DATA) ---
    if category == "data":
        temp = safe_float(data.get('temp'))
        hum = safe_float(data.get('hum'))
        lux = safe_float(data.get('lux'))
        bat = safe_float(data.get('bat'))

        arm = data.get('armed', False)
        alarm_cnt = data.get('alarms', 0)

        status_icon = "🛡️  UZBROJONY" if arm else "🔓 ROZBROJONY"
        status_col = Col.GREEN if arm else Col.CYAN

        print(f"{Col.BLUE}📊 [{packet_time}] RAPORT (ID: {device_id}, USER: {owner}){Col.RESET}")
        print(f"   🔋 Bateria: {bat:.2f} V")
        print(f"   🌡️  Pogoda:  {temp:.1f}°C  |  💧 {hum:.0f}%")
        print(f"   ☀️  Światło: {lux:.0f} lux")
        print(f"   {status_col}{status_icon}{Col.RESET} | ⚠️  Alarmy: {alarm_cnt}")
        print("-" * 50)

    # --- ALARMY (EVENT) ---
    elif category == "event":
        ev_type = data.get('type', 'UNKNOWN')
        val = safe_float(data.get('val'))

        print(f"\n{Col.RED}{Col.BOLD}🚨🚨 ALARM WYKRYTY! 🚨🚨{Col.RESET}")
        print(f"   ⏰ Czas:   {packet_time}")
        print(f"   📦 ID:     {device_id}")
        print(f"   👤 User:   {owner}")

        if ev_type == "ALARM_MOTION":
            print(f"   💥 Powód:  WSTRZĄS")
            print(f"   📉 Siła:   {val:.2f} g")
        elif ev_type == "ALARM_LIGHT":
            print(f"   🔦 Powód:  OTWARCIE (Światło)")
            print(f"   ☀️  Jasność: {val:.0f} lux")
        else:
            print(f"   ❓ Typ:    {ev_type} (Val: {val})")
        print("=" * 50 + "\n")

    # --- KOMENDY (CMD) ---
    elif category == "cmd":
        cmd = data.get('set', 'UNKNOWN')
        print(f"{Col.YELLOW}⚙️  [CMD] Wysłano do {device_id} (USER: {owner}): {cmd}{Col.RESET}")


def send_command(cmd_type):
    if current_device_id is None:
        print(f"{Col.RED}⚠️  Brak ID urządzenia. Poczekaj na dane.{Col.RESET}")
        return

    topic = f"packageguard/user1/{current_device_id}/cmd"
    payload = json.dumps({"set": cmd_type})

    client.publish(topic, payload, qos=1)
    print(f"{Col.YELLOW}✈️  Wysłano: {cmd_type} -> {current_device_id}{Col.RESET}")


# --- START ---
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(BROKER, PORT, 60)
    client.loop_start()

    while True:
        user_input = input()
        cmd = user_input.strip().lower()

        if cmd == 'a':
            send_command("ARM")
        elif cmd == 'd':
            send_command("DISARM")
        elif cmd == 'q':
            break

except KeyboardInterrupt:
    print("\nZakończono.")
finally:
    client.loop_stop()
    client.disconnect()