#!/usr/bin/env python3
"""
HomeMesh placeholder entity simulator.

Publishes Home Assistant MQTT discovery configs for a set of fake
devices, then behaves like them: listens on command topics, echoes
state back retained, and pushes sensor readings on a timer.

Purpose: give you real MQTT entities to develop the panel against
before any physical device exists. The topic contract here is the
same one your ESP32 firmware will use, so nothing in the firmware
changes when real devices replace these.

Usage:
    pip install -r requirements.txt
    python ha_placeholder_entities.py            # creds from .env
    python ha_placeholder_entities.py --remove   # cleanup

Credentials are read from MQTT_USER / MQTT_PASSWORD in a .env file
next to this script (or the environment); --user/--password override.
"""

import argparse
import json
import os
import random
import signal
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

DISCOVERY_PREFIX = "homeassistant"
BASE = "homemesh/sim"
AVAILABILITY_TOPIC = f"{BASE}/status"

# One HA device per simulated entity. HA assigns an area to a
# *device*, so a single shared device block would pin all six
# entities to the same room.
#
# suggested_area is only a hint applied at first registration - edit
# the areas in ENTITIES below, or pass None to leave a device
# unassigned and place it by hand in the HA UI.
def device(slug, name, area):
    dev = {
        "identifiers": [f"homemesh_sim_{slug}"],
        "name": name,
        "model": "Placeholder Device",
        "manufacturer": "HomeMesh",
    }
    if area:
        dev["suggested_area"] = area
    return dev


def light(slug, name, area, brightness=False):
    cfg = {
        "name": name,
        "unique_id": f"homemesh_sim_{slug}",
        "command_topic": f"{BASE}/{slug}/set",
        "state_topic": f"{BASE}/{slug}/state",
        "payload_on": "ON",
        "payload_off": "OFF",
        "availability_topic": AVAILABILITY_TOPIC,
        "device": device(slug, name, area),
    }
    if brightness:
        cfg["brightness_command_topic"] = f"{BASE}/{slug}/brightness/set"
        cfg["brightness_state_topic"] = f"{BASE}/{slug}/brightness/state"
        cfg["brightness_scale"] = 255
    return ("light", slug, cfg)


def switch(slug, name, area):
    return ("switch", slug, {
        "name": name,
        "unique_id": f"homemesh_sim_{slug}",
        "command_topic": f"{BASE}/{slug}/set",
        "state_topic": f"{BASE}/{slug}/state",
        "payload_on": "ON",
        "payload_off": "OFF",
        "availability_topic": AVAILABILITY_TOPIC,
        "device": device(slug, name, area),
    })


def sensor(slug, name, unit, device_class, area):
    return ("sensor", slug, {
        "name": name,
        "unique_id": f"homemesh_sim_{slug}",
        "state_topic": f"{BASE}/{slug}/state",
        "unit_of_measurement": unit,
        "device_class": device_class,
        "state_class": "measurement",
        "availability_topic": AVAILABILITY_TOPIC,
        "device": device(slug, name, area),
    })


ENTITIES = [
    light("living_light", "Living Room Light", "Living Room", brightness=True),
    light("kitchen_light", "Kitchen Light", "Kitchen"),
    light("bedroom_light", "Bedroom Light", "Bedroom"),
    switch("fan_switch", "Ceiling Fan", "Bedroom"),
    sensor("temperature", "Room Temperature", "\u00b0C", "temperature", "Living Room"),
    sensor("humidity", "Room Humidity", "%", "humidity", "Living Room"),
]

# Runtime state of the fake devices.
state = {
    "living_light": "OFF",
    "living_light_brightness": 128,
    "kitchen_light": "OFF",
    "bedroom_light": "OFF",
    "fan_switch": "OFF",
    "temperature": 24.5,
    "humidity": 55.0,
}

running = True


def discovery_topic(component, slug):
    return f"{DISCOVERY_PREFIX}/{component}/homemesh_sim/{slug}/config"


def on_connect(client, userdata, flags, rc, properties=None):
    if rc != 0:
        print(f"Connection failed, rc={rc}")
        return

    print("Connected to broker.")

    # Announce availability BEFORE discovery, so entities never
    # appear in an unavailable state on first registration.
    client.publish(AVAILABILITY_TOPIC, "online", retain=True)

    for component, slug, cfg in ENTITIES:
        client.publish(discovery_topic(component, slug),
                       json.dumps(cfg), retain=True)
        print(f"  registered {component}.{slug}")

    # Subscribe to every command topic.
    for component, slug, cfg in ENTITIES:
        if "command_topic" in cfg:
            client.subscribe(cfg["command_topic"])
        if "brightness_command_topic" in cfg:
            client.subscribe(cfg["brightness_command_topic"])

    publish_all_states(client)
    print("\nReady. Toggle entities in HA and watch them here.\n")


def publish_all_states(client):
    for component, slug, cfg in ENTITIES:
        if component == "sensor":
            client.publish(cfg["state_topic"],
                           str(state[slug]), retain=True)
        else:
            client.publish(cfg["state_topic"], state[slug], retain=True)
        if "brightness_state_topic" in cfg:
            client.publish(cfg["brightness_state_topic"],
                           str(state[f"{slug}_brightness"]), retain=True)


def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()
    print(f"  <- {topic} = {payload}")

    for component, slug, cfg in ENTITIES:
        if topic == cfg.get("command_topic"):
            state[slug] = payload
            # Retained, so a panel that reboots gets correct state
            # immediately instead of showing stale or blank values.
            client.publish(cfg["state_topic"], payload, retain=True)
            print(f"  -> {cfg['state_topic']} = {payload}")

        elif topic == cfg.get("brightness_command_topic"):
            state[f"{slug}_brightness"] = int(payload)
            client.publish(cfg["brightness_state_topic"],
                           payload, retain=True)
            print(f"  -> {cfg['brightness_state_topic']} = {payload}")


def drift_sensors(client):
    """Nudge the sensor readings so the panel has changing data."""
    state["temperature"] = round(
        max(18.0, min(32.0, state["temperature"] + random.uniform(-0.3, 0.3))), 1)
    state["humidity"] = round(
        max(30.0, min(80.0, state["humidity"] + random.uniform(-1.0, 1.0))), 1)

    for component, slug, cfg in ENTITIES:
        if component == "sensor":
            client.publish(cfg["state_topic"], str(state[slug]), retain=True)


def remove_entities(client):
    """Empty retained payload on a discovery topic deletes the entity."""
    for component, slug, cfg in ENTITIES:
        client.publish(discovery_topic(component, slug), "", retain=True)
        print(f"  removed {component}.{slug}")


def load_env(path=Path(__file__).with_name(".env")):
    """Load KEY=VALUE lines into os.environ without overriding real env vars."""
    if not path.exists():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        os.environ.setdefault(key.strip(), value.strip().strip("\"'"))


def main():
    load_env()

    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="homeassistant.local", help="broker host")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--user", default=os.environ.get("MQTT_USER"))
    ap.add_argument("--password", default=os.environ.get("MQTT_PASSWORD"))
    ap.add_argument("--remove", action="store_true",
                    help="delete the entities from HA and exit")
    args = ap.parse_args()

    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:
        client = mqtt.Client()   # paho 1.x

    if args.user:
        client.username_pw_set(args.user, args.password)

    if args.remove:
        client.connect(args.host, args.port, 60)
        client.loop_start()
        time.sleep(1)
        remove_entities(client)
        time.sleep(1)
        client.loop_stop()
        return

    # Last will: if this script dies, HA marks the entities
    # unavailable rather than showing stale values forever.
    client.will_set(AVAILABILITY_TOPIC, "offline", retain=True)
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"Connecting to {args.host}:{args.port} ...")
    client.connect(args.host, args.port, 60)
    client.loop_start()

    def stop(sig, frame):
        global running
        running = False

    signal.signal(signal.SIGINT, stop)

    last = 0
    while running:
        if time.time() - last > 10:
            drift_sensors(client)
            last = time.time()
        time.sleep(0.2)

    print("\nShutting down.")
    client.publish(AVAILABILITY_TOPIC, "offline", retain=True)
    time.sleep(0.5)
    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    main()
