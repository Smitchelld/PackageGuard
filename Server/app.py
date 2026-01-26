import sqlite3
import json
import paho.mqtt.client as mqtt
from flask import Flask, render_template, request, redirect, url_for, session
import threading
from datetime import datetime
from werkzeug.security import generate_password_hash, check_password_hash

app = Flask(__name__)
app.secret_key = "guard_pro_ultimate_key_2026"

# --- KONFIGURACJA ---
MQTT_BROKER = "192.168.137.1"
MQTT_PORT = 1883
DB_NAME = "guard_data.db"
MQTT_CLIENT = None


def get_db_connection():
    conn = sqlite3.connect(DB_NAME, timeout=20, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    conn = get_db_connection()
    c = conn.cursor()
    c.execute('''CREATE TABLE IF NOT EXISTS users (
        id INTEGER PRIMARY KEY,
        login TEXT UNIQUE,
        password TEXT
    )''')
    c.execute('''CREATE TABLE IF NOT EXISTS devices (
        mac TEXT PRIMARY KEY,
        owner_id TEXT,
        name TEXT,
        shock_threshold_g REAL DEFAULT 0.4,
        temp_min_c REAL DEFAULT 0.0,
        temp_max_c REAL DEFAULT 40.0,
        hum_max_percent REAL DEFAULT 80.0,
        pres_min_hpa REAL DEFAULT 950.0,
        pres_max_hpa REAL DEFAULT 1050.0,
        lux_min REAL DEFAULT 0.0,
        lux_max REAL DEFAULT 100.0,
        bat_min_v REAL DEFAULT 3.4,
        shock_alarm_enabled INTEGER DEFAULT 1,
        temp_alarm_enabled INTEGER DEFAULT 0,
        hum_alarm_enabled INTEGER DEFAULT 0,
        pres_alarm_enabled INTEGER DEFAULT 0,
        light_alarm_enabled INTEGER DEFAULT 0,
        bat_alarm_enabled INTEGER DEFAULT 1,
        action_buzzer_enabled INTEGER DEFAULT 1,
        action_motor_enabled INTEGER DEFAULT 1,
        action_led_enabled INTEGER DEFAULT 1,
        stealth_mode_enabled INTEGER DEFAULT 0,
        status_interval_sec INTEGER DEFAULT 60
    )''')
    c.execute('''CREATE TABLE IF NOT EXISTS telemetry (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        mac TEXT,
        ts TEXT,
        temp REAL,
        hum REAL,
        pres REAL,
        lux REAL,
        ax REAL,
        ay REAL,
        az REAL,
        g REAL,
        bat REAL,
        armed INTEGER,
        alarms INTEGER
    )''')
    c.execute('''CREATE TABLE IF NOT EXISTS events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        mac TEXT,
        ts TEXT,
        type TEXT,
        val REAL
    )''')
    conn.commit()
    try:
        c.execute(
            "INSERT OR IGNORE INTO users (login, password) VALUES (?, ?)",
            ("unassigned", None)
        )
        conn.commit()
    except Exception:
        pass
    conn.close()


# --- MQTT LOGIC ---
def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        parts = msg.topic.split('/')
        if len(parts) < 4:
            return

        owner = parts[1]
        mac = parts[2].upper()
        msg_type = parts[3]

        # --- FACTORY RESET / UNASSIGN ---
        if owner == "unassigned":
            conn = get_db_connection()
            conn.execute("DELETE FROM devices WHERE mac=?", (mac,))
            conn.commit()
            conn.close()
            print(f"[MQTT] Device {mac} unassigned / removed")
            return

        conn = get_db_connection()

        if msg_type == "data":
            raw_pres = data.get('pres', 0)
            pres_hpa = raw_pres / 100.0 if raw_pres > 5000 else raw_pres

            conn.execute(
                '''INSERT INTO telemetry
                (mac, ts, temp, hum, pres, lux, ax, ay, az, g, bat, armed, alarms)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
                (
                    mac,
                    datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                    data.get('temp', 0),
                    data.get('hum', 0),
                    pres_hpa,
                    data.get('lux', 0),
                    data.get('ax', 0),
                    data.get('ay', 0),
                    data.get('az', 0),
                    data.get('g', 0),
                    data.get('bat', 0),
                    1 if data.get('armed') else 0,
                    data.get('alarms', 0)
                )
            )

        elif msg_type == "event":
            conn.execute(
                '''INSERT INTO events (mac, ts, type, val)
                VALUES (?, ?, ?, ?)''',
                (
                    mac,
                    datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                    data.get('type', 'UNKNOWN'),
                    data.get('val', 0)
                )
            )

        conn.commit()
        conn.close()

    except Exception as e:
        print(f"MQTT Error: {e}")


def mqtt_thread():
    global MQTT_CLIENT
    MQTT_CLIENT = mqtt.Client()
    MQTT_CLIENT.on_message = on_message
    try:
        MQTT_CLIENT.connect(MQTT_BROKER, MQTT_PORT, 60)
        MQTT_CLIENT.subscribe("packageguard/+/+/data")
        MQTT_CLIENT.subscribe("packageguard/+/+/event")
        MQTT_CLIENT.loop_forever()
    except Exception:
        print("Błąd MQTT: Broker niedostępny")


def send_mqtt_cmd(mac, payload):
    conn = get_db_connection()
    dev = conn.execute(
        "SELECT owner_id FROM devices WHERE mac=?",
        (mac.upper(),)
    ).fetchone()
    conn.close()

    owner = dev['owner_id'] if dev else "user1"
    MQTT_CLIENT.publish(
        f"packageguard/{owner}/{mac.upper()}/cmd",
        json.dumps(payload)
    )


# --- WEB ROUTES ---

@app.route('/')
def index():
    if 'user' not in session:
        return redirect(url_for('login'))

    conn = get_db_connection()
    devices = conn.execute(
        "SELECT * FROM devices WHERE owner_id=?",
        (session['user'],)
    ).fetchall()

    stats = {}
    events = {}

    for d in devices:
        mac = d['mac']
        stats[mac] = conn.execute(
            "SELECT * FROM telemetry WHERE mac=? ORDER BY id DESC LIMIT 1",
            (mac,)
        ).fetchone()

        events[mac] = conn.execute(
            "SELECT * FROM events WHERE mac=? ORDER BY id DESC LIMIT 5",
            (mac,)
        ).fetchall()

    conn.close()

    return render_template(
        "index.html",
        user=session['user'],
        devices=devices,
        stats=stats,
        events=events
    )


@app.route('/login', methods=['GET', 'POST'])
def login():
    msg = ""
    if request.method == 'POST':
        user = get_db_connection().execute(
            "SELECT * FROM users WHERE login=?",
            (request.form.get('login'),)
        ).fetchone()

        if user and check_password_hash(
            user['password'],
            request.form.get('password')
        ):
            session['user'] = user['login']
            return redirect(url_for('index'))

        msg = "Błędne dane logowania"

    return render_template("login.html", msg=msg)


@app.route('/register', methods=['GET', 'POST'])
def register():
    if request.method == 'POST':
        conn = get_db_connection()
        try:
            conn.execute(
                "INSERT INTO users (login, password) VALUES (?, ?)",
                (
                    request.form.get('login'),
                    generate_password_hash(request.form.get('password'))
                )
            )
            conn.commit()
            return redirect(url_for('login'))
        except Exception:
            return "Błąd: Login zajęty"
        finally:
            conn.close()

    return render_template("register.html")


@app.route('/logout')
def logout():
    session.pop('user', None)
    return redirect(url_for('login'))


@app.route('/delete/<mac>')
def delete_device(mac):
    if 'user' not in session:
        return "403", 403

    conn = get_db_connection()
    conn.execute(
        "DELETE FROM devices WHERE mac=? AND owner_id=?",
        (mac.upper(), session['user'])
    )
    conn.commit()
    conn.close()

    return redirect(url_for('index'))


@app.route('/device/<mac>', methods=['GET', 'POST'])
def device_config(mac):
    if 'user' not in session:
        return redirect(url_for('login'))

    conn = get_db_connection()

    if request.method == 'POST':
        d = request.form
        f = lambda k, v: float(d.get(k, v) or v)
        i = lambda k, v: int(d.get(k, v) or v)
        b = lambda k: 1 if k in d else 0

        conn.execute(
            '''UPDATE devices SET
               shock_threshold_g=?,
               temp_min_c=?,
               temp_max_c=?,
               hum_max_percent=?,
               pres_min_hpa=?,
               pres_max_hpa=?,
               lux_min=?,
               lux_max=?,
               bat_min_v=?,
               shock_alarm_enabled=?,
               temp_alarm_enabled=?,
               hum_alarm_enabled=?,
               pres_alarm_enabled=?,
               light_alarm_enabled=?,
               bat_alarm_enabled=?,
               action_buzzer_enabled=?,
               action_motor_enabled=?,
               action_led_enabled=?,
               stealth_mode_enabled=?,
               status_interval_sec=?
               WHERE mac=? AND owner_id=?''',
            (
                f('shock_g', 0.4),
                f('temp_min', 0),
                f('temp_max', 40),
                f('hum_max', 80),
                f('pres_min', 900),
                f('pres_max', 1100),
                f('lux_min', 0),
                f('lux_max', 100),
                f('bat_min', 3.4),
                b('shock_en'),
                b('temp_en'),
                b('hum_en'),
                b('pres_en'),
                b('light_en'),
                b('bat_en'),
                b('buzz_en'),
                b('mot_en'),
                b('led_en'),
                b('stealth_en'),
                i('interval', 60),
                mac.upper(),
                session['user']
            )
        )

        conn.commit()

        payload = {
            "config": {
                "shock_threshold_g": f('shock_g', 0.4),
                "temp_min_c": f('temp_min', 0),
                "temp_max_c": f('temp_max', 40),
                "hum_max_percent": f('hum_max', 80),
                "pres_min_hpa": f('pres_min', 900),
                "pres_max_hpa": f('pres_max', 1100),
                "lux_min": f('lux_min', 0),
                "lux_max": f('lux_max', 100),
                "bat_min_v": f('bat_min', 3.4),
                "shock_alarm_enabled": b('shock_en') == 1,
                "temp_alarm_enabled": b('temp_en') == 1,
                "hum_alarm_enabled": b('hum_en') == 1,
                "pres_alarm_enabled": b('pres_en') == 1,
                "light_alarm_enabled": b('light_en') == 1,
                "bat_alarm_enabled": b('bat_en') == 1,
                "action_buzzer_enabled": b('buzz_en') == 1,
                "action_motor_enabled": b('mot_en') == 1,
                "action_led_enabled": b('led_en') == 1,
                "stealth_mode_enabled": b('stealth_en') == 1,
                "status_interval_sec": i('interval', 60)
            }
        }

        send_mqtt_cmd(mac, payload)
        conn.close()
        return redirect(url_for('index'))

    device = conn.execute(
        "SELECT * FROM devices WHERE mac=? AND owner_id=?",
        (mac.upper(), session['user'])
    ).fetchone()
    conn.close()

    return render_template("config.html", device=device)


# --- API (ANDROID) ---

@app.route('/api/claim', methods=['POST'])
def api_claim():
    mac_ble = request.form.get('mac', '').replace(':', '').upper().strip()
    user = request.args.get('user')

    try:
        mac_wifi = f"{int(mac_ble, 16):012X}"
        conn = get_db_connection()
        conn.execute(
            "INSERT OR REPLACE INTO devices (mac, owner_id, name) VALUES (?, ?, ?)",
            (mac_wifi, user, request.form.get('name', 'Nowa Paczka'))
        )
        conn.commit()
        conn.close()
        return json.dumps({"status": "success"})
    except Exception:
        return json.dumps({"status": "error"}), 400


@app.route('/api/login', methods=['POST'])
def api_login():
    user = get_db_connection().execute(
        "SELECT * FROM users WHERE login=?",
        (request.form.get('login'),)
    ).fetchone()

    if user and check_password_hash(
        user['password'],
        request.form.get('password')
    ):
        return json.dumps({"status": "success", "user": user['login']})

    return json.dumps({"status": "error", "message": "Bad credentials"}), 401


@app.route('/api/devices')
def api_devices():
    user = request.args.get('user')
    conn = get_db_connection()

    devs = conn.execute(
        "SELECT * FROM devices WHERE owner_id=?",
        (user,)
    ).fetchall()

    out = []
    for d in devs:
        s = conn.execute(
            "SELECT * FROM telemetry WHERE mac=? ORDER BY id DESC LIMIT 1",
            (d['mac'],)
        ).fetchone()

        alms = conn.execute(
            "SELECT ts, type, val FROM events WHERE mac=? ORDER BY id DESC LIMIT 3",
            (d['mac'],)
        ).fetchall()

        dev_dict = dict(d)
        dev_dict['last_data'] = dict(s) if s else None
        dev_dict['last_alarms'] = [dict(a) for a in alms]
        out.append(dev_dict)

    conn.close()
    return json.dumps(out)


@app.route('/cmd/<mac>/<command>')
def quick_cmd(mac, command):
    if 'user' in session:
        send_mqtt_cmd(mac, {"set": command})
    return redirect(url_for('index'))

from datetime import datetime, timedelta

@app.route('/device_info/<mac>')
def device_info(mac):
    if 'user' not in session:
        return redirect(url_for('login'))

    minutes = int(request.args.get('minutes', 60))
    time_limit = datetime.now() - timedelta(minutes=minutes)

    conn = get_db_connection()
    device = conn.execute(
        "SELECT * FROM devices WHERE mac=? AND owner_id=?",
        (mac.upper(), session['user'])
    ).fetchone()

    telemetry_rows = conn.execute(
        "SELECT * FROM telemetry WHERE mac=? AND ts >= ? ORDER BY ts ASC",
        (mac.upper(), time_limit.strftime('%Y-%m-%d %H:%M:%S'))
    ).fetchall()

    telemetry = [dict(row) for row in telemetry_rows]
    conn.close()
    return render_template("info.html", device=device, telemetry=telemetry, minutes=minutes)

if __name__ == '__main__':
    init_db()
    threading.Thread(target=mqtt_thread, daemon=True).start()
    app.run(host='0.0.0.0', port=5000, debug=False)
